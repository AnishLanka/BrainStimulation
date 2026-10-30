/**
 * @file  i2c_dds_master.c
 * @brief I2C DDS Master Driver — ESP32-S3 → RP2040 Zero slave implementation.
 *
 * See i2c_dds_master.h for protocol description and integration notes.
 */

#include "i2c_dds_master.h"

#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "i2c_dds";

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/**
 * @brief Return true if slave_idx is within the valid range [0, DDS_NUM_SLAVES).
 */
static inline bool slave_valid(uint8_t idx)
{
    return idx < DDS_NUM_SLAVES;
}

static bool frequency_valid(float hz)
{
    return isfinite(hz) && hz >= DDS_MIN_FREQ_HZ && hz <= DDS_MAX_FREQ_HZ;
}

static bool amplitude_valid(float mA)
{
    return isfinite(mA) && mA >= 0.0f && mA <= DDS_MAX_AMPLITUDE_MA;
}

/**
 * @brief Register and probe all DDS_NUM_SLAVES RP2040 Zero devices on an
 *        already-open bus.
 *
 * Each candidate address is registered with the bus then immediately probed
 * with a zero-length transaction.  Addresses that do not ACK are removed and
 * their handle is set to NULL so the rest of the driver can skip them safely.
 * This prevents ESP_ERR_INVALID_STATE errors caused by sending transactions to
 * ghost addresses, which stall the I2C peripheral's hardware state machine.
 *
 * Called by both init paths after the bus handle has been stored.
 */
static esp_err_t add_slave_devices(dds_master_t *drv, uint32_t speed_hz)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .scl_speed_hz    = speed_hz,
    };

    uint8_t found = 0;

    for (uint8_t i = 0; i < DDS_NUM_SLAVES; i++) {
        dev_cfg.device_address = (uint16_t)(DDS_SLAVE_BASE_ADDR + i);

        ESP_RETURN_ON_ERROR(
            i2c_master_bus_add_device(drv->bus_handle,
                                      &dev_cfg,
                                      &drv->dev_handles[i]),
            TAG,
            "Failed to add RP2040 Zero slave %u (addr 0x%02X)",
            i, dev_cfg.device_address);

        /* Probe: address-only transaction — no data bytes.
         * A missing slave will NACK, probe returns non-OK.
         * Remove the handle so slave_write / slave_write_read skip it. */
        esp_err_t probe = i2c_master_probe(drv->bus_handle,
                                           dev_cfg.device_address,
                                           DDS_I2C_TIMEOUT_MS);
        if (probe != ESP_OK) {
            ESP_LOGW(TAG,
                     "Slave %u (0x%02X) not found on bus — handle removed",
                     i, dev_cfg.device_address);
            i2c_master_bus_rm_device(drv->dev_handles[i]);
            drv->dev_handles[i] = NULL;
        } else {
            ESP_LOGI(TAG, "Registered RP2040 Zero slave %u at I2C addr 0x%02X",
                     i, dev_cfg.device_address);
            found++;
        }
    }

    if (found == 0) {
        ESP_LOGI(TAG, "No DDS cards present");
        /* Empty slots are normal, including a completely unpopulated board. */
    }

    ESP_LOGI(TAG, "%u of %u DDS slave(s) found", found, DDS_NUM_SLAVES);
    return ESP_OK;
}

/**
 * @brief Transmit buf[0..len-1] to slave slave_idx.
 *
 * Returns ESP_ERR_NOT_FOUND immediately if the slave was not detected
 * during initialisation (handle is NULL), avoiding a transaction against
 * a non-existent device that would stall the I2C peripheral.
 */
static esp_err_t slave_write(dds_master_t  *drv,
                             uint8_t        slave_idx,
                             const uint8_t *buf,
                             size_t         len)
{
    if (drv->dev_handles[slave_idx] == NULL) {
        ESP_LOGW(TAG, "slave_write: slave %u not present — skipping", slave_idx);
        return ESP_ERR_NOT_FOUND;
    }
    return i2c_master_transmit(drv->dev_handles[slave_idx],
                               buf, len,
                               DDS_I2C_TIMEOUT_MS);
}

/**
 * @brief Write tx_buf then read rx_len bytes from slave slave_idx.
 *
 * Uses a single I2C transaction with a repeated-START between write and read
 * phases (i2c_master_transmit_receive in ESP-IDF 5.x).
 *
 * Returns ESP_ERR_NOT_FOUND immediately if the slave was not detected
 * during initialisation, avoiding a stall on the I2C peripheral.
 */
static esp_err_t slave_write_read(dds_master_t  *drv,
                                  uint8_t        slave_idx,
                                  const uint8_t *tx_buf,
                                  size_t         tx_len,
                                  uint8_t       *rx_buf,
                                  size_t         rx_len)
{
    if (drv->dev_handles[slave_idx] == NULL) {
        ESP_LOGW(TAG, "slave_write_read: slave %u not present — skipping", slave_idx);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = i2c_master_transmit_receive(drv->dev_handles[slave_idx],
                                                tx_buf, tx_len,
                                                rx_buf, rx_len,
                                                DDS_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        /* Slave likely held the bus during the read phase — reset to unblock. */
        ESP_LOGW(TAG, "slave_write_read failed (%s) — resetting I2C bus",
                 esp_err_to_name(ret));
        i2c_master_bus_reset(drv->bus_handle);
    }
    return ret;
}

/* =========================================================================
 * Init / deinit
 * ========================================================================= */

esp_err_t dds_master_init(dds_master_t *drv, i2c_master_bus_handle_t bus)
{
    ESP_RETURN_ON_FALSE(drv && bus, ESP_ERR_INVALID_ARG, TAG,
                        "drv or bus is NULL");

    memset(drv, 0, sizeof(*drv));
    drv->bus_handle = bus;
    drv->owns_bus   = false;

    ESP_RETURN_ON_ERROR(add_slave_devices(drv, DDS_I2C_SPEED_HZ),
                        TAG, "Slave device registration failed");

    drv->initialized = true;

    ESP_LOGI(TAG,
             "DDS master initialised (shared bus, %d RP2040 Zero slave(s) @ 0x%02X–0x%02X)",
             DDS_NUM_SLAVES,
             DDS_SLAVE_BASE_ADDR,
             DDS_SLAVE_BASE_ADDR + DDS_NUM_SLAVES - 1);
    return ESP_OK;
}

esp_err_t dds_master_init_new_bus(dds_master_t *drv,
                                  int           sda_gpio,
                                  int           scl_gpio,
                                  int           port_num,
                                  uint32_t      speed_hz)
{
    ESP_RETURN_ON_FALSE(drv, ESP_ERR_INVALID_ARG, TAG, "drv is NULL");
    ESP_RETURN_ON_FALSE(speed_hz > 0, ESP_ERR_INVALID_ARG, TAG,
                        "speed_hz must be > 0");

    memset(drv, 0, sizeof(*drv));

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = (i2c_port_num_t)port_num,
        .sda_io_num        = sda_gpio,
        .scl_io_num        = scl_gpio,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        /* FIX: Internal pull-ups disabled — external 2.2 kΩ resistors are
         * fitted on both sides of the ISO1641B isolator.  Enabling the
         * ESP32-S3's ~45 kΩ internal pull-ups places them in parallel with
         * the externals, lowering the effective impedance and preventing the
         * isolator's open-drain output from pulling the line low cleanly.   */
        .flags.enable_internal_pullup = false,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &drv->bus_handle),
                        TAG, "i2c_new_master_bus failed");

    drv->owns_bus = true;

    esp_err_t ret = add_slave_devices(drv, speed_hz);
    if (ret != ESP_OK) {
        /* Clean up the bus we just created before returning the error. */
        i2c_del_master_bus(drv->bus_handle);
        drv->bus_handle = NULL;
        drv->owns_bus   = false;
        return ret;
    }

    drv->initialized = true;

    ESP_LOGI(TAG,
             "DDS master initialised (new bus port=%d SDA=%d SCL=%d "
             "@%" PRIu32 " Hz, %d RP2040 Zero slave(s) @ 0x%02X–0x%02X)",
             port_num, sda_gpio, scl_gpio, speed_hz,
             DDS_NUM_SLAVES,
             DDS_SLAVE_BASE_ADDR,
             DDS_SLAVE_BASE_ADDR + DDS_NUM_SLAVES - 1);
    return ESP_OK;
}

esp_err_t dds_master_deinit(dds_master_t *drv)
{
    ESP_RETURN_ON_FALSE(drv, ESP_ERR_INVALID_ARG, TAG, "drv is NULL");

    /* Deregister every slave device handle, even if not fully initialised. */
    for (uint8_t i = 0; i < DDS_NUM_SLAVES; i++) {
        if (drv->dev_handles[i]) {
            (void)i2c_master_bus_rm_device(drv->dev_handles[i]);
            drv->dev_handles[i] = NULL;
        }
    }

    /* Only delete the bus if we created it. */
    if (drv->owns_bus && drv->bus_handle) {
        (void)i2c_del_master_bus(drv->bus_handle);
        drv->bus_handle = NULL;
    }

    drv->initialized = false;
    ESP_LOGI(TAG, "DDS master deinitialised");
    return ESP_OK;
}

/* =========================================================================
 * Per-slave write commands
 * ========================================================================= */

esp_err_t dds_master_set_freq(dds_master_t *drv,
                              uint8_t       slave_idx,
                              float         freq_hz)
{
    ESP_RETURN_ON_FALSE(drv && drv->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "Driver not initialised");
    ESP_RETURN_ON_FALSE(slave_valid(slave_idx), ESP_ERR_INVALID_ARG, TAG,
                        "Slave index %u out of range", slave_idx);

    ESP_RETURN_ON_FALSE(frequency_valid(freq_hz), ESP_ERR_INVALID_ARG, TAG,
                        "Frequency outside command limits");
    uint32_t freq_mHz = (uint32_t)(freq_hz * 1000.0f);

    /* [CMD, freq_mHz LE 4 bytes] */
    uint8_t buf[5] = {
        DDS_CMD_SET_FREQ,
        (uint8_t)(freq_mHz         & 0xFF),
        (uint8_t)((freq_mHz >> 8)  & 0xFF),
        (uint8_t)((freq_mHz >> 16) & 0xFF),
        (uint8_t)((freq_mHz >> 24) & 0xFF),
    };

    ESP_RETURN_ON_ERROR(slave_write(drv, slave_idx, buf, sizeof(buf)),
                        TAG, "set_freq failed (slave %u)", slave_idx);

    drv->slave_state[slave_idx].freq_hz = freq_hz;
    ESP_LOGD(TAG, "Slave %u: freq → %.3f Hz", slave_idx, freq_hz);
    return ESP_OK;
}

esp_err_t dds_master_set_waveform(dds_master_t  *drv,
                                  uint8_t        slave_idx,
                                  dds_waveform_t wave)
{
    ESP_RETURN_ON_FALSE(drv && drv->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "Driver not initialised");
    ESP_RETURN_ON_FALSE(slave_valid(slave_idx), ESP_ERR_INVALID_ARG, TAG,
                        "Slave index %u out of range", slave_idx);

    ESP_RETURN_ON_FALSE(wave >= DDS_WAVE_SINE && wave <= DDS_WAVE_SAWTOOTH,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid waveform");
    uint8_t buf[2] = { DDS_CMD_SET_WAVEFORM, (uint8_t)wave };

    ESP_RETURN_ON_ERROR(slave_write(drv, slave_idx, buf, sizeof(buf)),
                        TAG, "set_waveform failed (slave %u)", slave_idx);

    drv->slave_state[slave_idx].waveform = wave;
    ESP_LOGD(TAG, "Slave %u: waveform → %d", slave_idx, (int)wave);
    return ESP_OK;
}

esp_err_t dds_master_set_amplitude(dds_master_t *drv,
                                   uint8_t       slave_idx,
                                   float         amplitude_mA)
{
    ESP_RETURN_ON_FALSE(drv && drv->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "Driver not initialised");
    ESP_RETURN_ON_FALSE(slave_valid(slave_idx), ESP_ERR_INVALID_ARG, TAG,
                        "Slave index %u out of range", slave_idx);

    ESP_RETURN_ON_FALSE(amplitude_valid(amplitude_mA), ESP_ERR_INVALID_ARG, TAG,
                        "Amplitude outside command limits");
    uint16_t amp_uA = (uint16_t)(amplitude_mA * 1000.0f);

    /* [CMD, amp_uA LE 2 bytes] */
    uint8_t buf[3] = {
        DDS_CMD_SET_AMPLITUDE,
        (uint8_t)(amp_uA & 0xFF),
        (uint8_t)((amp_uA >> 8) & 0xFF),
    };

    ESP_RETURN_ON_ERROR(slave_write(drv, slave_idx, buf, sizeof(buf)),
                        TAG, "set_amplitude failed (slave %u)", slave_idx);

    drv->slave_state[slave_idx].amplitude_mA = amplitude_mA;
    ESP_LOGD(TAG, "Slave %u: amplitude → %.3f mA", slave_idx, amplitude_mA);
    return ESP_OK;
}

esp_err_t dds_master_set_enable(dds_master_t *drv,
                                uint8_t       slave_idx,
                                bool          enable)
{
    ESP_RETURN_ON_FALSE(drv && drv->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "Driver not initialised");
    ESP_RETURN_ON_FALSE(slave_valid(slave_idx), ESP_ERR_INVALID_ARG, TAG,
                        "Slave index %u out of range", slave_idx);

    uint8_t buf[2] = { DDS_CMD_SET_ENABLE, enable ? 1u : 0u };

    ESP_RETURN_ON_ERROR(slave_write(drv, slave_idx, buf, sizeof(buf)),
                        TAG, "set_enable failed (slave %u)", slave_idx);

    drv->slave_state[slave_idx].enabled = enable;
    ESP_LOGD(TAG, "Slave %u: output %s", slave_idx,
             enable ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t dds_master_set_all(dds_master_t            *drv,
                             uint8_t                  slave_idx,
                             const dds_slave_state_t *state)
{
    ESP_RETURN_ON_FALSE(drv && drv->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "Driver not initialised");
    ESP_RETURN_ON_FALSE(slave_valid(slave_idx), ESP_ERR_INVALID_ARG, TAG,
                        "Slave index %u out of range", slave_idx);
    ESP_RETURN_ON_FALSE(state != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "state is NULL");

    /* Validate before converting floats to unsigned wire fields. */
    ESP_RETURN_ON_FALSE(frequency_valid(state->freq_hz) &&
                        amplitude_valid(state->amplitude_mA) &&
                        state->waveform >= DDS_WAVE_SINE &&
                        state->waveform <= DDS_WAVE_SAWTOOTH,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid DDS parameters");

    dds_set_all_pkt_t pkt = {
        .cmd          = DDS_CMD_SET_ALL,
        .freq_mHz     = (uint32_t)(state->freq_hz      * 1000.0f),
        .amplitude_uA = (uint16_t)(state->amplitude_mA * 1000.0f),
        .waveform     = (uint8_t)state->waveform,
        .enabled      = state->enabled ? 1u : 0u,
    };

    ESP_RETURN_ON_ERROR(
        slave_write(drv, slave_idx, (const uint8_t *)&pkt, sizeof(pkt)),
        TAG, "set_all failed (slave %u)", slave_idx);

    drv->slave_state[slave_idx].freq_hz = state->freq_hz;
    drv->slave_state[slave_idx].amplitude_mA = state->amplitude_mA;
    drv->slave_state[slave_idx].waveform = state->waveform;
    drv->slave_state[slave_idx].enabled = state->enabled;

    ESP_LOGD(TAG,
             "Slave %u: all params → %.2f Hz, %.2f mA, wave=%d, en=%d",
             slave_idx, state->freq_hz, state->amplitude_mA,
             (int)state->waveform, (int)state->enabled);
    return ESP_OK;
}

esp_err_t dds_master_broadcast_all(dds_master_t            *drv,
                                   const dds_slave_state_t *state)
{
    ESP_RETURN_ON_FALSE(drv && drv->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "Driver not initialised");
    ESP_RETURN_ON_FALSE(state != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "state is NULL");

    for (uint8_t i = 0; i < DDS_NUM_SLAVES; i++) {
        if (!dds_master_slave_present(drv, i)) continue;
        ESP_RETURN_ON_ERROR(dds_master_set_all(drv, i, state),
                            TAG, "broadcast aborted at slave %u", i);
    }

    ESP_LOGI(TAG, "Broadcast complete — all %d slaves updated", DDS_NUM_SLAVES);
    return ESP_OK;
}

/* =========================================================================
 * Status readback
 * ========================================================================= */

esp_err_t dds_master_get_status(dds_master_t      *drv,
                                uint8_t            slave_idx,
                                dds_slave_state_t *out_state,
                                bool              *out_fault)
{
    ESP_RETURN_ON_FALSE(drv && drv->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "Driver not initialised");
    ESP_RETURN_ON_FALSE(slave_valid(slave_idx), ESP_ERR_INVALID_ARG, TAG,
                        "Slave index %u out of range", slave_idx);
    ESP_RETURN_ON_FALSE(out_state != NULL && out_fault != NULL,
                        ESP_ERR_INVALID_ARG, TAG,
                        "out_state or out_fault is NULL");

    const uint8_t cmd = DDS_CMD_GET_STATUS;
    dds_status_pkt_t resp = {0};
    drv->slave_state[slave_idx].status_valid = false;
    memset(out_state, 0, sizeof(*out_state));
    *out_fault = true;
    ESP_RETURN_ON_ERROR(
        slave_write_read(drv, slave_idx, &cmd, sizeof(cmd),
                         (uint8_t *)&resp, sizeof(resp)),
        TAG, "get_status failed (slave %u)", slave_idx);
    ESP_RETURN_ON_FALSE(resp.version == DDS_STATUS_VERSION && resp.magic == DDS_STATUS_MAGIC &&
        resp.freq_mHz >= (uint32_t)(DDS_MIN_FREQ_HZ * 1000) &&
        resp.freq_mHz <= (uint32_t)(DDS_MAX_FREQ_HZ * 1000) &&
        resp.amplitude_uA <= DDS_MAX_AMPLITUDE_UA && resp.current_amplitude_uA <= DDS_MAX_AMPLITUDE_UA &&
        resp.waveform <= DDS_WAVE_SAWTOOTH && resp.enabled <= 1 &&
        resp.output_state <= DDS_OUTPUT_DISCONNECT &&
        !(resp.switches & ~(DDS_SWITCH_OUT | DDS_SWITCH_DUMMY)) &&
        (resp.output_state != DDS_OUTPUT_OFF || (!resp.current_amplitude_uA && !resp.switches)),
        ESP_ERR_INVALID_RESPONSE, TAG, "Invalid/legacy status packet");
    out_state->freq_hz = resp.freq_mHz / 1000.0f;
    out_state->amplitude_mA = resp.amplitude_uA / 1000.0f;
    out_state->waveform = (dds_waveform_t)resp.waveform;
    out_state->enabled = resp.enabled != 0;
    out_state->current_amplitude_uA = resp.current_amplitude_uA;
    out_state->output_state = (dds_output_state_t)resp.output_state;
    out_state->out_en = (resp.switches & DDS_SWITCH_OUT) != 0;
    out_state->dummy_en = (resp.switches & DDS_SWITCH_DUMMY) != 0;
    out_state->status_valid = true;
    *out_fault = out_state->fault = resp.fault != 0;
    drv->slave_state[slave_idx] = *out_state;

    ESP_LOGD(TAG,
             "Slave %u status: %.3f Hz, %.3f mA, wave=%d, en=%d, fault=%d",
             slave_idx,
             out_state->freq_hz, out_state->amplitude_mA,
             (int)out_state->waveform, (int)out_state->enabled,
             (int)resp.fault);
    return ESP_OK;
}

/* =========================================================================
 * Reset
 * ========================================================================= */

esp_err_t dds_master_reset_slave(dds_master_t *drv, uint8_t slave_idx)
{
    ESP_RETURN_ON_FALSE(drv && drv->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "Driver not initialised");
    ESP_RETURN_ON_FALSE(slave_valid(slave_idx), ESP_ERR_INVALID_ARG, TAG,
                        "Slave index %u out of range", slave_idx);

    const uint8_t cmd = DDS_CMD_RESET;

    ESP_RETURN_ON_ERROR(slave_write(drv, slave_idx, &cmd, sizeof(cmd)),
                        TAG, "reset failed (slave %u)", slave_idx);

    drv->slave_state[slave_idx].enabled = false;
    drv->slave_state[slave_idx].amplitude_mA = 0;
    drv->slave_state[slave_idx].status_valid = false; /* reset starts a ramp */
    ESP_LOGI(TAG, "Slave %u reset", slave_idx);
    return ESP_OK;
}

esp_err_t dds_master_reset_all(dds_master_t *drv)
{
    ESP_RETURN_ON_FALSE(drv && drv->initialized, ESP_ERR_INVALID_STATE, TAG,
                        "Driver not initialised");

    esp_err_t first_err = ESP_OK;

    for (uint8_t i = 0; i < DDS_NUM_SLAVES; i++) {
        if (!dds_master_slave_present(drv, i)) continue;
        esp_err_t err = dds_master_reset_slave(drv, i);
        if (err != ESP_OK && first_err == ESP_OK) {
            /* Record the first failure but continue resetting remaining slaves. */
            first_err = err;
            ESP_LOGE(TAG, "reset_all: slave %u failed (%s)",
                     i, esp_err_to_name(err));
        }
    }
    return first_err;
}

/* =========================================================================
 * Slave presence query
 * ========================================================================= */

bool dds_master_slave_present(const dds_master_t *drv, uint8_t slave_idx)
{
    if (!drv || !drv->initialized || !slave_valid(slave_idx)) {
        return false;
    }
    return drv->dev_handles[slave_idx] != NULL;
}

/* =========================================================================
 * Cache accessor
 * ========================================================================= */

const dds_slave_state_t *dds_master_get_local_state(const dds_master_t *drv,
                                                    uint8_t             slave_idx)
{
    if (!drv || !drv->initialized || !slave_valid(slave_idx)) {
        return NULL;
    }
    return &drv->slave_state[slave_idx];
}

bool dds_status_is_off(const dds_slave_state_t *s) {
    return s && s->status_valid && !s->enabled && !s->current_amplitude_uA &&
           s->output_state == DDS_OUTPUT_OFF && !s->out_en && !s->dummy_en;
}
const char *dds_output_state_name(dds_output_state_t state) {
    static const char *names[] = {"OFF", "PRECHARGE", "CONNECT", "RAMP_UP", "ACTIVE", "RAMP_DOWN", "DISCONNECT"};
    return (unsigned)state <= DDS_OUTPUT_DISCONNECT ? names[state] : "UNKNOWN";
}
