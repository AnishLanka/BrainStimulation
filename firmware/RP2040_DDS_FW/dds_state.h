#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "dds_tables.h"

/* Timing, wire format and board constants: single source of truth. */
#define SAMPLE_RATE_HZ 312500UL
#define FREQ_MIN_HZ 500UL
#define FREQ_MAX_HZ 30000UL
#define DEFAULT_FREQ_MHZ 1000000UL
#define DAC_IOUT_MAX_UA 5000U
#define DAC_REFERENCE_MV 3300U /* DAC8830 VREF = isolated 3.3 V rail (3.135-3.465 V), nominal only */
#define DAC_MIDPOINT 32768U
#define AMP_Q16_ONE 65536UL
#define RAMP_UA_PER_SEC 95UL
#define RAMP_STEP_US ((DAC_IOUT_MAX_UA * 1000000ULL + AMP_Q16_ONE * RAMP_UA_PER_SEC - 1) / (AMP_Q16_ONE * RAMP_UA_PER_SEC))
#define FREQ_SLEW_STEP_US 1000UL
#define FREQ_SLEW_SHIFT 10U /* 1/1024 per ms: about 1.4 octaves/s while current flows */
#define PRECHARGE_US 1000000UL
#define CONNECT_US 10000UL
#define DISCONNECT_US 10000UL /* TODO: confirm PhotoMOS release time. */
#define COMMAND_TIMEOUT_US 3000000UL /* TODO: confirm 3 s timeout. */
#define WATCHDOG_TIMEOUT_MS 500U
#define DDS_CONTROL_US 1000UL
#define SYS_CLOCK_KHZ 125000UL
/* DAC8830 (Signal Generator Rev C): one bare 16-bit code per sample, MSB first,
 * sampled on rising SCLK (SPI mode 0), loaded on the CS rising edge. No command
 * or power-down bits exist. The PL022 divides 125 MHz down to 15.625 MHz for a
 * 20 MHz request; the DAC allows 50 MHz and the ISOW7741 100 Mbps. */
#define SPI_BAUD_HZ 20000000UL
#define SPI_WORD_BITS 16U
#define DMA_SAMPLES 256U
#define DMA_WORDS DMA_SAMPLES /* one SPI word per sample */
#define DMA_TIMER_DENOM (SYS_CLOCK_KHZ * 1000UL / SAMPLE_RATE_HZ)
#define DAC_DRAIN_US 100UL /* More than SPI FIFO + serializer latency. */
/* Raspberry Pi Pico 2 W GPIO. The Rev C signal board exposes two channels on
 * J1/J2 (pins: 04 SCLK, 05 MOSI shared; 06 CS and 07 EN/FLT per channel).
 * Board-side MCU pins are symbolic in the Rev C release; these are the firmware
 * assignments and must match the harness wiring. */
#define SPI_SCK_PIN 2U   /* J1.04 / J2.04 SPI_SCLK_SYS */
#define SPI_MOSI_PIN 3U  /* J1.05 / J2.05 SPI_MOSI_SYS */
#define DAC_CS_PIN 4U    /* J1.06 SPI_CS_SYS: streamed channel, CS framed by PIO */
#define DAC_CS2_PIN 5U   /* J2.06 SPI_CS_SYS_2: parked at midpoint, GPIO driven */
#define DAC_FLT_PIN 8U   /* J1.07 FAULT_TO_PICO: ISOW7741 EN/FLT, 10k pull-up, low = fault; input only */
#define DAC_FLT2_PIN 9U  /* J2.07 FAULT_TO_PICO_2: input only, not yet evaluated */
/* TODO: Rev C has no PhotoMOS. Output disconnect is the monitor core's two-pole
 * relay, driven through the ADC board host header J201 (RUN, ARM edge,
 * HEARTBEAT out; FAULT_OK in; UART). The DUMMY->OUT precharge sequence below
 * therefore has no hardware behind it yet; these pins are placeholders until
 * the controller side of that interface is assigned (docs/RevC_Release/
 * Integration_Guide.html, "Primary-side controller interface"). */
#define OUT_EN_PIN 0U
#define DUMMY_EN_PIN 1U
/* GP23/24/25/29 belong to the CYW43 radio on the Pico 2 W and are not on the header. */
#define RADIO_PIN(p) ((p) == 23 || (p) == 24 || (p) == 25 || (p) == 29)
/* Optional per-sample debug toggle (scope frequency Fs/2). Disabled by default;
 * CS already pulses once per sample and can always be probed. Set to an unused
 * header GPIO after checking the harness wiring. */
#define SAMPLE_DEBUG_PIN (-1)
#define I2C_SDA_PIN 6U
#define I2C_SCL_PIN 7U
#define I2C_BAUDRATE 100000UL
#define I2C_BASE_ADDR 0x50U
#define ADDR_PIN_0 26U
#define ADDR_PIN_1 27U
#define ADDR_PIN_2 28U
#define ADDR_SETTLE_US 10U
#define RX_BUF_SIZE 16U
#define STATUS_BYTES 15U
#define STATUS_VERSION 2U
#define STATUS_MAGIC 0xA5U
#define SWITCH_OUT 1U
#define SWITCH_DUMMY 2U
#define DDS_CMD_SET_FREQ 0x01U
#define DDS_CMD_SET_WAVEFORM 0x02U
#define DDS_CMD_SET_AMPLITUDE 0x03U
#define DDS_CMD_SET_ENABLE 0x04U
#define DDS_CMD_SET_ALL 0x05U
#define DDS_CMD_GET_STATUS 0x06U
#define DDS_CMD_RESET 0xFFU
#define FAULT_COMM_TIMEOUT 1U
#define FAULT_STREAM 2U
#define FAULT_WATCHDOG 4U
#define FAULT_ISO_SUPPLY 8U /* ISOW7741 EN/FLT asserted while current could flow */

_Static_assert(DAC_IOUT_MAX_UA == 5000U, "5 mA full scale required");
_Static_assert(DAC_IOUT_MAX_UA * 1000000ULL <= 100ULL * AMP_Q16_ONE * RAMP_STEP_US, "Ramp exceeds 100 uA/s");
_Static_assert(SAMPLE_RATE_HZ >= 10UL * FREQ_MAX_HZ, "Need 10 samples/cycle");
_Static_assert((SYS_CLOCK_KHZ * 1000UL) % SAMPLE_RATE_HZ == 0, "Exact DMA timer divider required");
_Static_assert(DMA_TIMER_DENOM <= 65535U, "DMA divider exceeds 16 bits");
_Static_assert(FREQ_MAX_HZ * 1000ULL <= (UINT64_MAX >> 32), "Phase numerator overflow");

typedef enum { OUTPUT_OFF, OUTPUT_PRECHARGE, OUTPUT_CONNECT, OUTPUT_RAMP_UP,
               OUTPUT_ACTIVE, OUTPUT_RAMP_DOWN, OUTPUT_DISCONNECT } output_state_t;
typedef struct {
    uint32_t freq_mHz;
    uint16_t amplitude_uA;
    uint8_t waveform;
    bool enabled;
    uint32_t last_command_us;
    uint32_t stop_generation;
    uint32_t reset_generation;
} dds_request_t;
typedef struct {
    uint32_t freq_mHz;
    uint16_t amplitude_uA;
    uint16_t current_uA;
    uint8_t waveform, enabled, fault, state, switches;
} dds_status_t;

static inline uint32_t clamp_freq_mHz(uint32_t v) {
    if (v < FREQ_MIN_HZ * 1000UL) return FREQ_MIN_HZ * 1000UL;
    if (v > FREQ_MAX_HZ * 1000UL) return FREQ_MAX_HZ * 1000UL;
    return v;
}
static inline uint32_t calc_phase_inc(uint32_t freq_mHz) {
    return (uint32_t)(((uint64_t)clamp_freq_mHz(freq_mHz) << 32) / (SAMPLE_RATE_HZ * 1000ULL));
}
static inline uint32_t slew_freq_mHz(uint32_t live, uint32_t target) {
    uint32_t step = live >> FREQ_SLEW_SHIFT;
    if (!step) step = 1;
    if (live < target) return target - live > step ? live + step : target;
    return live - target > step ? live - step : target;
}
static inline uint32_t calc_amp_scale(uint16_t uA) {
    if (uA >= DAC_IOUT_MAX_UA) return AMP_Q16_ONE;
    return (uint32_t)((uint64_t)uA * AMP_Q16_ONE / DAC_IOUT_MAX_UA);
}
static inline uint16_t scale_sample(uint16_t sample, uint32_t scale) {
    /* Signed product fits int32: 32767 * 65536 = 2147418112.
     * Round magnitude symmetrically, preserving midpoint/DC symmetry. */
    int32_t v = (int32_t)sample - DAC_MIDPOINT;
    int32_t product = v * (int32_t)scale;
    int32_t scaled = product >= 0 ? product >> 16 : -((-product) >> 16);
    return (uint16_t)((int32_t)DAC_MIDPOINT + scaled);
}
void dds_shared_init(void);
void dds_request_read(dds_request_t *out);
void dds_request_write(const dds_request_t *request);
void dds_status_read(dds_status_t *out);
void dds_status_write(const dds_status_t *status);

_Static_assert(OUT_EN_PIN != DUMMY_EN_PIN && DAC_CS_PIN != DAC_CS2_PIN &&
               DAC_FLT_PIN != DAC_FLT2_PIN, "Output pin conflict");
_Static_assert(!RADIO_PIN(SPI_SCK_PIN) && !RADIO_PIN(SPI_MOSI_PIN) &&
    !RADIO_PIN(DAC_CS_PIN) && !RADIO_PIN(DAC_CS2_PIN) && !RADIO_PIN(DAC_FLT_PIN) &&
    !RADIO_PIN(DAC_FLT2_PIN) && !RADIO_PIN(OUT_EN_PIN) && !RADIO_PIN(DUMMY_EN_PIN) &&
    !RADIO_PIN(I2C_SDA_PIN) && !RADIO_PIN(I2C_SCL_PIN) && !RADIO_PIN(ADDR_PIN_0) &&
    !RADIO_PIN(ADDR_PIN_1) && !RADIO_PIN(ADDR_PIN_2), "GPIO reserved for the CYW43 radio");
_Static_assert(SAMPLE_DEBUG_PIN < 0 || (SAMPLE_DEBUG_PIN < 23 &&
    SAMPLE_DEBUG_PIN != OUT_EN_PIN && SAMPLE_DEBUG_PIN != DUMMY_EN_PIN &&
    SAMPLE_DEBUG_PIN != SPI_SCK_PIN && SAMPLE_DEBUG_PIN != SPI_MOSI_PIN &&
    SAMPLE_DEBUG_PIN != DAC_CS_PIN && SAMPLE_DEBUG_PIN != DAC_CS2_PIN &&
    SAMPLE_DEBUG_PIN != DAC_FLT_PIN && SAMPLE_DEBUG_PIN != DAC_FLT2_PIN &&
    SAMPLE_DEBUG_PIN != I2C_SDA_PIN && SAMPLE_DEBUG_PIN != I2C_SCL_PIN),
    "Debug GPIO conflicts with hardware");
