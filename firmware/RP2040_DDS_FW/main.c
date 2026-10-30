#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "dds_state.h"
#include "spi_dac.h"
#include "i2c_slave.h"

typedef struct { uint32_t freq_mHz, gain_q16; } correction_t;
/* TODO: populate ONLY after bench calibration; gain is capped at DAC full scale.
 * A gain > 1 cannot extend the DAC swing beyond the physical 5 mA limit. */
static const correction_t correction[] = {
    {500000, AMP_Q16_ONE}, {1000000, AMP_Q16_ONE},
    {10000000, AMP_Q16_ONE}, {20000000, AMP_Q16_ONE},
    {30000000, AMP_Q16_ONE}
};
static uint32_t corrected_scale(const dds_request_t *r) {
    uint32_t gain = correction[0].gain_q16;
    for (unsigned i = 1; i < sizeof(correction)/sizeof(correction[0]); ++i) {
        if (r->freq_mHz <= correction[i].freq_mHz) {
            int64_t delta = (int64_t)correction[i].gain_q16 - correction[i-1].gain_q16;
            gain = correction[i-1].gain_q16 + delta * (r->freq_mHz - correction[i-1].freq_mHz) /
                   (correction[i].freq_mHz - correction[i-1].freq_mHz);
            break;
        }
        gain = correction[i].gain_q16;
    }
    uint64_t scale = (uint64_t)calc_amp_scale(r->amplitude_uA) * gain / AMP_Q16_ONE;
    return scale > AMP_Q16_ONE ? AMP_Q16_ONE : (uint32_t)scale;
}
static void core1_entry(void) {
    app_i2c_slave_init(); /* IRQ belongs to Core 1, not the DDS core. */
    i2c_slave_task();
}
int main(void) {
    /* First operations in main, including watchdog reboot: switches off. */
    gpio_init(OUT_EN_PIN); gpio_put(OUT_EN_PIN, 0); gpio_set_dir(OUT_EN_PIN, GPIO_OUT);
    gpio_init(DUMMY_EN_PIN); gpio_put(DUMMY_EN_PIN, 0); gpio_set_dir(DUMMY_EN_PIN, GPIO_OUT);
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
    spi_dac_init(); /* Both DACs at 32768 as soon as SPI is available. */
    bool watchdog_reset = watchdog_caused_reboot();
    dds_shared_init();
    dds_tables_init();
    multicore_launch_core1(core1_entry);
    spi_dac_stream_start(calc_phase_inc(DEFAULT_FREQ_MHZ), lut_table[WAVE_SINE]);
    watchdog_enable(WATCHDOG_TIMEOUT_MS, false);
    dds_request_t request;
    output_state_t state = OUTPUT_OFF;
    uint8_t fault = watchdog_reset ? FAULT_WATCHDOG : 0;
    uint32_t scale = 0, state_since = time_us_32(), ramp_since = state_since;
    uint32_t live_freq = DEFAULT_FREQ_MHZ, freq_since = state_since;
    uint32_t seen_stop = 0, seen_reset = 0;
    bool stopping = false, pending_reset = false;
    for (;;) {
        uint32_t now = time_us_32();
        dds_request_read(&request);
        if (request.reset_generation != seen_reset) {
            seen_reset = request.reset_generation; pending_reset = true;
        }
        if (request.stop_generation != seen_stop) {
            seen_stop = request.stop_generation;
            stopping = state != OUTPUT_OFF;
        }
        if (state != OUTPUT_OFF &&
            (uint32_t)(now - request.last_command_us) >= COMMAND_TIMEOUT_US) {
            fault |= FAULT_COMM_TIMEOUT;
            stopping = true;
        }
        /* ISOW7741 EN/FLT low means the isolated DAC rail is not trusted. Only
         * latch while current could flow; at boot the line may still be settling. */
        bool iso_fault = spi_dac_iso_fault();
        if (state != OUTPUT_OFF && iso_fault) {
            fault |= FAULT_ISO_SUPPLY;
            stopping = true;
        }
        if (!request.enabled && state != OUTPUT_OFF) stopping = true;
        if (stopping && state != OUTPUT_DISCONNECT) state = OUTPUT_RAMP_DOWN;
        /* Frequency may jump only while no current flows; otherwise it slews.
         * Bounded catch-up after a stall; stale credit is dropped (slower is safe). */
        uint32_t freq_target = clamp_freq_mHz(request.freq_mHz);
        if (scale == 0 || live_freq == freq_target) {
            live_freq = freq_target; freq_since = now;
        } else {
            for (unsigned i = 0; i < 4 && (uint32_t)(now - freq_since) >= FREQ_SLEW_STEP_US; ++i) {
                live_freq = slew_freq_mHz(live_freq, freq_target);
                freq_since += FREQ_SLEW_STEP_US;
            }
            if ((uint32_t)(now - freq_since) >= FREQ_SLEW_STEP_US) freq_since = now;
        }
        dds_request_t live_request = request;
        live_request.freq_mHz = live_freq; /* calibration gain follows the emitted frequency */
        uint32_t target = 0;
        switch (state) {
        case OUTPUT_OFF:
            if (pending_reset) { fault = 0; pending_reset = false; }
            if (request.enabled && !fault && !stopping && !iso_fault) {
                gpio_put(DUMMY_EN_PIN, 1);
                state = OUTPUT_PRECHARGE; state_since = now;
            }
            break;
        case OUTPUT_PRECHARGE:
            if ((uint32_t)(now - state_since) >= PRECHARGE_US && spi_dac_stream_zero()) {
                gpio_put(OUT_EN_PIN, 1); /* zero DAC amplitude confirmed */
                state = OUTPUT_CONNECT; state_since = now;
            }
            break;
        case OUTPUT_CONNECT:
            if ((uint32_t)(now - state_since) >= CONNECT_US) {
                gpio_put(DUMMY_EN_PIN, 0); state = OUTPUT_RAMP_UP;
            }
            break;
        case OUTPUT_RAMP_UP:
        case OUTPUT_ACTIVE:
            target = corrected_scale(&live_request);
            state = scale == target ? OUTPUT_ACTIVE : OUTPUT_RAMP_UP;
            break;
        case OUTPUT_RAMP_DOWN:
            if (scale == 0 && spi_dac_stream_zero()) {
                gpio_put(DUMMY_EN_PIN, 1);
                /* Hold dummy on before opening OUT, honoring switch delays. */
                state = OUTPUT_DISCONNECT; state_since = now;
            }
            break;
        case OUTPUT_DISCONNECT:
            if ((uint32_t)(now - state_since) >= DISCONNECT_US) {
                gpio_put(OUT_EN_PIN, 0);
            }
            if ((uint32_t)(now - state_since) >= 2UL * DISCONNECT_US) {
                gpio_put(DUMMY_EN_PIN, 0); state = OUTPUT_OFF; stopping = false;
                /* A disable supersedes any intervening ON until fully OFF.
                 * The ESP32 will not re-enable during its shutdown sequence. */
            }
            break;
        }
        /* Timer-based ramp, no credit accumulated while already at target. */
        if (scale == target) ramp_since = now;
        else {
            uint32_t steps = (uint32_t)(now - ramp_since) / RAMP_STEP_US;
            if (steps) {
                uint32_t distance = scale > target ? scale - target : target - scale;
                if (steps > distance) steps = distance;
                scale = scale < target ? scale + steps : scale - steps;
                ramp_since += steps * RAMP_STEP_US;
            }
        }
        if (!spi_dac_stream_service(calc_phase_inc(live_freq),
                                    lut_table[request.waveform], scale)) {
            fault |= FAULT_STREAM;
            /* Timing is no longer trustworthy. Publish fault, then allow the
             * watchdog to reset into disconnected switches and midpoint.
             * A hardware/CPU failure cannot guarantee a controlled ramp. */
            dds_status_t failed = { .freq_mHz = request.freq_mHz,
                .amplitude_uA = request.amplitude_uA, .current_uA = DAC_IOUT_MAX_UA,
                .waveform = request.waveform, .fault = fault, .state = state,
                .switches = (gpio_get(OUT_EN_PIN) ? SWITCH_OUT : 0) |
                            (gpio_get(DUMMY_EN_PIN) ? SWITCH_DUMMY : 0) };
            dds_status_write(&failed);
            for (;;) __wfi();
        }
        uint32_t live_scale = spi_dac_stream_max_scale();
        if (scale > live_scale) live_scale = scale;
        dds_status_t status = {
            .freq_mHz = request.freq_mHz, .amplitude_uA = request.amplitude_uA,
            .current_uA = (uint16_t)((live_scale * DAC_IOUT_MAX_UA + AMP_Q16_ONE - 1) / AMP_Q16_ONE),
            .waveform = request.waveform,
            .enabled = request.enabled && !stopping && !fault,
            .fault = fault, .state = state,
            .switches = (gpio_get(OUT_EN_PIN) ? SWITCH_OUT : 0) |
                        (gpio_get(DUMMY_EN_PIN) ? SWITCH_DUMMY : 0)
        };
        dds_status_write(&status);
        watchdog_update();
        /* One wake per DMA block (~819 us), no busy-loop/per-sample blocking. */
        __wfe();
    }
}
