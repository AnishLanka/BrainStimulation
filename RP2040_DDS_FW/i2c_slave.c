#include "hardware/sync.h"
#include "i2c_slave.h"
#include "dds_state.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/i2c_slave.h"

static uint8_t slave_addr, rx[RX_BUF_SIZE], tx[STATUS_BYTES];
static unsigned rx_count, tx_idx;
static bool overflow;
static uint8_t cmd;
static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t read16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static void apply_write(void) {
    if (overflow || !rx_count) return;
    dds_request_t r; dds_request_read(&r);
    switch (cmd) {
    case DDS_CMD_SET_FREQ:
        if (rx_count != 5) return;
        r.freq_mHz = clamp_freq_mHz(read32(rx+1)); break;
    case DDS_CMD_SET_WAVEFORM:
        if (rx_count != 2 || rx[1] >= WAVE_TYPE_COUNT) return;
        r.waveform = rx[1]; break;
    case DDS_CMD_SET_AMPLITUDE:
        if (rx_count != 3) return;
        r.amplitude_uA = read16(rx+1); break;
    case DDS_CMD_SET_ENABLE:
        if (rx_count != 2 || rx[1] > 1) return;
        r.enabled = rx[1] != 0;
        if (!r.enabled) ++r.stop_generation;
        break;
    case DDS_CMD_SET_ALL:
        if (rx_count != 9 || rx[7] >= WAVE_TYPE_COUNT || rx[8] > 1) return;
        r.freq_mHz = clamp_freq_mHz(read32(rx+1));
        r.amplitude_uA = read16(rx+5); r.waveform = rx[7]; r.enabled = rx[8] != 0;
        if (!r.enabled) ++r.stop_generation;
        break;
    case DDS_CMD_GET_STATUS:
        if (rx_count != 1) return;
        break;
    case DDS_CMD_RESET:
        if (rx_count != 1) return;
        /* Reset is a ramp-down request, never a waveform/current step. */
        r.enabled = false; r.amplitude_uA = 0;
        ++r.stop_generation; ++r.reset_generation;
        break;
    default: return;
    }
    if (r.amplitude_uA > DAC_IOUT_MAX_UA) r.amplitude_uA = DAC_IOUT_MAX_UA;
    r.last_command_us = time_us_32();
    dds_request_write(&r);
}
static void prepare_status(void) {
    dds_status_t s; dds_status_read(&s);
    tx[0] = s.freq_mHz; tx[1] = s.freq_mHz >> 8; tx[2] = s.freq_mHz >> 16; tx[3] = s.freq_mHz >> 24;
    tx[4] = s.amplitude_uA; tx[5] = s.amplitude_uA >> 8;
    tx[6] = s.waveform; tx[7] = s.enabled; tx[8] = s.fault;
    tx[9] = STATUS_VERSION; tx[10] = s.state;
    tx[11] = s.current_uA; tx[12] = s.current_uA >> 8;
    tx[13] = s.switches; tx[14] = STATUS_MAGIC;
}
static void handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
    switch (event) {
    case I2C_SLAVE_RECEIVE: {
        uint8_t value = i2c_read_byte_raw(i2c);
        if (!rx_count) { cmd = value; tx_idx = 0; }
        if (rx_count < RX_BUF_SIZE) rx[rx_count++] = value;
        else overflow = true; /* saturate: never wrap and accept an oversized write */
        break;
    }
    case I2C_SLAVE_REQUEST:
        if (cmd == DDS_CMD_GET_STATUS && !overflow && rx_count <= 1) {
            if (!tx_idx) {
                /* Also covers repeated-START callbacks before FINISH. */
                if (rx_count == 1) apply_write();
                prepare_status();
            }
            i2c_write_byte_raw(i2c, tx_idx < STATUS_BYTES ? tx[tx_idx++] : 0xff);
        } else i2c_write_byte_raw(i2c, 0xff);
        break;
    case I2C_SLAVE_FINISH:
        apply_write();
        rx_count = 0; overflow = false; tx_idx = 0;
        break;
    }
}
uint8_t i2c_slave_get_address(void) { return slave_addr; }
void app_i2c_slave_init(void) {
    const uint pins[] = {ADDR_PIN_0, ADDR_PIN_1, ADDR_PIN_2};
    for (uint i = 0; i < 3; ++i) { gpio_init(pins[i]); gpio_set_dir(pins[i], GPIO_IN); gpio_pull_down(pins[i]); }
    sleep_us(ADDR_SETTLE_US);
    slave_addr = I2C_BASE_ADDR | gpio_get(ADDR_PIN_0) |
                 (gpio_get(ADDR_PIN_1) << 1) | (gpio_get(ADDR_PIN_2) << 2);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_disable_pulls(I2C_SDA_PIN); gpio_disable_pulls(I2C_SCL_PIN);
    i2c_init(i2c1, I2C_BAUDRATE);
    i2c_slave_init(i2c1, slave_addr, handler);
}
void i2c_slave_task(void) { for (;;) __wfi(); }
