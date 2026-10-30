#include "hardware/sync.h"
#include "spi_dac.h"
#include "dds_state.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include "dac_sync.pio.h"

static uint16_t buffers[2][DMA_WORDS];
static uint32_t buffer_scale[2];
static int dma_ch[2], timer_id;
static uint sync_sm;
static uint32_t phase;
static volatile uint32_t completed;
static bool stream_failed;
static uint32_t zero_since;

/* DAC8830 wire format: one 16-bit code, MSB first, sampled on rising SCLK,
 * loaded into the DAC on the CS rising edge. spi_write16_blocking returns only
 * after the serializer is idle, so CS rises after the last bit. */
static void write_code(uint cs_pin, uint16_t code) {
    gpio_put(cs_pin, 0);
    spi_write16_blocking(spi0, &code, 1);
    gpio_put(cs_pin, 1);
}
static void cs_init(uint pin) { gpio_init(pin); gpio_put(pin, 1); gpio_set_dir(pin, GPIO_OUT); }
static void flt_init(uint pin) { gpio_init(pin); gpio_set_dir(pin, GPIO_IN); gpio_disable_pulls(pin); }
void spi_dac_init(void) {
    cs_init(DAC_CS_PIN); cs_init(DAC_CS2_PIN);
    /* ISOW7741 EN/FLT has an external 10k pull-up; never drive it from the MCU. */
    flt_init(DAC_FLT_PIN); flt_init(DAC_FLT2_PIN);
    spi_init(spi0, SPI_BAUD_HZ);
    spi_set_format(spi0, SPI_WORD_BITS, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_MOSI_PIN, GPIO_FUNC_SPI);
    /* Both DACs reset to code 0, which is -VREF on the bipolar output stage.
     * Load midpoint on the streamed channel and park the second channel there.
     * This is not a startup inhibit; hardware must hold the output off until
     * rails are valid and this write has landed. */
    write_code(DAC_CS_PIN, DAC_MIDPOINT);
    write_code(DAC_CS2_PIN, DAC_MIDPOINT);
}
void spi_dac_write(uint16_t value) { write_code(DAC_CS_PIN, value); }
bool spi_dac_iso_fault(void) { return !gpio_get(DAC_FLT_PIN); }

static void __not_in_flash_func(fill)(unsigned b, uint32_t inc, const uint16_t *lut, uint32_t scale) {
    for (unsigned i = 0; i < DMA_SAMPLES; ++i) {
        phase += inc;
        buffers[b][i] = scale_sample(lut[phase >> LUT_SHIFT], scale);
    }
    buffer_scale[b] = scale;
}
static void __isr dma_done(void) {
    uint32_t mask = (1u << dma_ch[0]) | (1u << dma_ch[1]);
    uint32_t pending = dma_hw->ints0 & mask;
    dma_hw->ints0 = pending;
    if (pending & (1u << dma_ch[0])) completed |= 1u;
    if (pending & (1u << dma_ch[1])) completed |= 2u;
    __sev();
}
void spi_dac_stream_start(uint32_t inc, const uint16_t *lut) {
    /* Hardware timer delivers one 16-bit word every 3.2 us at 125 MHz.
     * SPI drains each word in about 1 us, leaving room for CS framing. */
    hard_assert(clock_get_hz(clk_sys) == SYS_CLOCK_KHZ * 1000UL);
    sync_sm = pio_claim_unused_sm(pio0, true);
    uint16_t instructions[dac_sync_program.length];
    for (uint i = 0; i < dac_sync_program.length; ++i) instructions[i] = dac_sync_program.instructions[i];
#if SAMPLE_DEBUG_PIN < 0
    instructions[7] = pio_encode_nop(); /* replace MOV PINS, X only */
#endif
    struct pio_program program = dac_sync_program;
    program.instructions = instructions;
    uint offset = pio_add_program(pio0, &program);
    pio_sm_config cfg = dac_sync_program_get_default_config(offset);
    sm_config_set_in_pins(&cfg, SPI_SCK_PIN);
    sm_config_set_set_pins(&cfg, DAC_CS_PIN, 1);
#if SAMPLE_DEBUG_PIN >= 0
    pio_gpio_init(pio0, SAMPLE_DEBUG_PIN);
    sm_config_set_out_pins(&cfg, SAMPLE_DEBUG_PIN, 1);
    pio_sm_set_consecutive_pindirs(pio0, sync_sm, SAMPLE_DEBUG_PIN, 1, true);
#endif
    pio_gpio_init(pio0, DAC_CS_PIN);
    pio_sm_set_consecutive_pindirs(pio0, sync_sm, DAC_CS_PIN, 1, true);
    pio_sm_init(pio0, sync_sm, offset, &cfg);
    pio_sm_exec(pio0, sync_sm, pio_encode_set(pio_x, 0));
    pio_sm_set_enabled(pio0, sync_sm, true);
    fill(0, inc, lut, 0); fill(1, inc, lut, 0);
    dma_ch[0] = dma_claim_unused_channel(true);
    dma_ch[1] = dma_claim_unused_channel(true);
    timer_id = dma_claim_unused_timer(true);
    dma_timer_set_fraction(timer_id, 1, DMA_TIMER_DENOM);
    for (unsigned b = 0; b < 2; ++b) {
        dma_channel_config c = dma_channel_get_default_config(dma_ch[b]);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, dma_get_timer_dreq(timer_id));
        channel_config_set_chain_to(&c, dma_ch[b ^ 1]);
        dma_channel_configure(dma_ch[b], &c, &spi_get_hw(spi0)->dr, buffers[b], DMA_WORDS, false);
        dma_channel_set_irq0_enabled(dma_ch[b], true);
    }
    irq_set_exclusive_handler(DMA_IRQ_0, dma_done);
    irq_set_enabled(DMA_IRQ_0, true);
    dma_start_channel_mask(1u << dma_ch[0]);
}
bool spi_dac_stream_service(uint32_t inc, const uint16_t *lut, uint32_t scale) {
    uint32_t irq_state = save_and_disable_interrupts();
    uint32_t done = completed;
    completed = 0;
    restore_interrupts(irq_state);
    /* Never replay a stale buffer after a missed refill deadline.
     * Leave DAC holding its last code; main stops petting watchdog. */
    if (done == 3u || (done && !dma_channel_is_busy(dma_ch[0]) && !dma_channel_is_busy(dma_ch[1]))) {
        stream_failed = true;
    }
    if (stream_failed) return false;
    for (unsigned b = 0; b < 2; ++b) {
        if (!(done & (1u << b))) continue;
        fill(b, inc, lut, scale); /* only write the completed/inactive buffer */
        if (!dma_channel_is_busy(dma_ch[b ^ 1])) { stream_failed = true; return false; }
        dma_channel_set_read_addr(dma_ch[b], buffers[b], false);
        dma_channel_set_trans_count(dma_ch[b], DMA_WORDS, false);
    }
    if (scale || buffer_scale[0] || buffer_scale[1]) zero_since = 0;
    else if (!zero_since) zero_since = time_us_32();
    return true;
}
bool spi_dac_stream_zero(void) {
    return !stream_failed && zero_since && (uint32_t)(time_us_32() - zero_since) >= DAC_DRAIN_US;
}

uint32_t spi_dac_stream_max_scale(void) { return buffer_scale[0] > buffer_scale[1] ? buffer_scale[0] : buffer_scale[1]; }
