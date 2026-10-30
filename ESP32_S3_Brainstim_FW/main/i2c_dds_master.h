#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "gpio_pins.h"

#define DDS_CMD_SET_FREQ 0x01
#define DDS_CMD_SET_WAVEFORM 0x02
#define DDS_CMD_SET_AMPLITUDE 0x03
#define DDS_CMD_SET_ENABLE 0x04
#define DDS_CMD_SET_ALL 0x05
#define DDS_CMD_GET_STATUS 0x06
#define DDS_CMD_RESET 0xFF
typedef enum { DDS_WAVE_SINE, DDS_WAVE_SQUARE, DDS_WAVE_TRIANGLE, DDS_WAVE_SAWTOOTH } dds_waveform_t;
typedef enum { DDS_OUTPUT_OFF, DDS_OUTPUT_PRECHARGE, DDS_OUTPUT_CONNECT,
    DDS_OUTPUT_RAMP_UP, DDS_OUTPUT_ACTIVE, DDS_OUTPUT_RAMP_DOWN, DDS_OUTPUT_DISCONNECT } dds_output_state_t;
typedef struct {
    float freq_hz, amplitude_mA; /* accepted request, NOT current output */
    dds_waveform_t waveform;
    bool enabled;
    uint16_t current_amplitude_uA; /* conservative applied current, rounded UP */
    dds_output_state_t output_state;
    bool out_en, dummy_en, status_valid, fault;
} dds_slave_state_t;
typedef struct __attribute__((packed)) {
    uint8_t cmd;
    uint32_t freq_mHz;
    uint16_t amplitude_uA;
    uint8_t waveform, enabled;
} dds_set_all_pkt_t;
/* GET_STATUS v2, 15 bytes, little-endian; see docs/phase1.md.
 * Legacy 9-byte replies MUST NOT be used to authorize HV or power-off. */
typedef struct __attribute__((packed)) {
    uint32_t freq_mHz;
    uint16_t amplitude_uA;
    uint8_t waveform, enabled, fault;
    uint8_t version, output_state;
    uint16_t current_amplitude_uA;
    uint8_t switches, magic;
} dds_status_pkt_t;
_Static_assert(sizeof(dds_status_pkt_t) == DDS_STATUS_BYTES, "Status wire size");
_Static_assert(sizeof(dds_set_all_pkt_t) == 9, "Command wire size");
typedef struct {
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handles[DDS_NUM_SLAVES];
    dds_slave_state_t slave_state[DDS_NUM_SLAVES];
    bool owns_bus, initialized;
} dds_master_t;
/* The DDS worker is the sole I2C caller after startup. */
esp_err_t dds_master_init(dds_master_t *, i2c_master_bus_handle_t);
esp_err_t dds_master_init_new_bus(dds_master_t *, int, int, int, uint32_t);
esp_err_t dds_master_deinit(dds_master_t *);
esp_err_t dds_master_set_freq(dds_master_t *, uint8_t, float);
esp_err_t dds_master_set_waveform(dds_master_t *, uint8_t, dds_waveform_t);
esp_err_t dds_master_set_amplitude(dds_master_t *, uint8_t, float);
esp_err_t dds_master_set_enable(dds_master_t *, uint8_t, bool);
esp_err_t dds_master_set_all(dds_master_t *, uint8_t, const dds_slave_state_t *);
esp_err_t dds_master_broadcast_all(dds_master_t *, const dds_slave_state_t *);
esp_err_t dds_master_get_status(dds_master_t *, uint8_t, dds_slave_state_t *, bool *);
esp_err_t dds_master_reset_slave(dds_master_t *, uint8_t);
esp_err_t dds_master_reset_all(dds_master_t *);
bool dds_master_slave_present(const dds_master_t *, uint8_t);
const dds_slave_state_t *dds_master_get_local_state(const dds_master_t *, uint8_t);
bool dds_status_is_off(const dds_slave_state_t *);
const char *dds_output_state_name(dds_output_state_t);
