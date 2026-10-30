/**
 * @file  i2c_dds_master.h
 * @brief I2C DDS Master Driver — ESP32-S3 → RP2040 Zero slave communication.
 *
 * Overview
 * --------
 * This driver allows the ESP32-S3 to act as an I2C master that controls up to
 * eight RP2040 Zero slave MCUs.  Each slave runs its own DDS (Direct Digital
 * Synthesis) engine and exposes a simple register-based I2C protocol so the
 * ESP32 can update waveform parameters and read back status at any time.
 *
 * Slave addressing
 * ----------------
 * Slaves use consecutive 7-bit addresses starting at DDS_SLAVE_BASE_ADDR:
 *   Slave 0 → 0x50,  Slave 1 → 0x51,  …,  Slave 7 → 0x57
 *
 * Current test configuration: 1 RP2040 Zero at address 0x50 (slave index 0).
 * Additional slaves can be added at 0x51–0x57 to reach the full DDS_NUM_SLAVES
 * complement without any firmware or driver changes.
 *
 * Bus assignment
 * --------------
 *   I2C_NUM_0  (GPIO 3 / 18)  — RP2040 Zero DDS slaves (0x50–0x57)
 *   I2C_NUM_1  (GPIO 16 / 17) — SSD1306 OLED (0x3C)
 *
 * ISO1641B isolator constraints
 * -----------------------------
 * An ISO1641B digital isolator is fitted between the ESP32-S3 and RP2040 Zero
 * I2C buses.  Three firmware rules follow from this hardware change:
 *
 *   1. Clock speed (DDS_I2C_SPEED_HZ = 100 000)
 *      The ISO1641B has a ~1 µs internal blanking time after one side drives a
 *      line low before enabling the other side's output driver.  At 400 kHz
 *      (2.5 µs per bit) this consumes ~40 % of every bit window, corrupting
 *      ACKs and read-phase bytes.  100 kHz (10 µs per bit) gives ample margin.
 *      The RP2040 slave must be configured to match: set I2C_BAUDRATE 100000
 *      in i2c_slave.c.
 *
 *   2. ESP32-S3 internal pull-ups (flags.enable_internal_pullup = false)
 *      External 2.2 kΩ resistors to 3.3 V are fitted on BOTH sides of the
 *      isolator.  The ESP32-S3's ~45 kΩ internal pull-ups must remain disabled.
 *      Enabling them lowers the effective pull-up impedance on Side 1 and
 *      prevents the isolator's open-drain output from pulling SCL/SDA cleanly
 *      to a valid logic low.
 *
 *   3. RP2040 internal pull-ups (gpio_pull_up → gpio_disable_pulls)
 *      The RP2040's ~50 kΩ internal pull-ups on I2C_SDA_PIN / I2C_SCL_PIN
 *      must likewise be disabled in i2c_slave.c for the same reason.
 *      Replace gpio_pull_up(I2C_SDA_PIN) / gpio_pull_up(I2C_SCL_PIN) with
 *      gpio_disable_pulls(I2C_SDA_PIN) / gpio_disable_pulls(I2C_SCL_PIN).
 *
 * RP2040 Zero I2C slave notes
 * ---------------------------
 * The RP2040 Zero implements I2C slave mode in firmware.  Each unit must be
 * flashed with DDS slave firmware that:
 *   - Listens on its assigned 7-bit address (0x50 + slave_index)
 *   - Handles clock-stretching during flash/DMA operations
 *   - Supports repeated-START for the DDS_CMD_GET_STATUS read-back
 *
 * Wire protocol
 * -------------
 * Every transaction begins with a one-byte command code (DDS_CMD_*).
 *
 * Write transactions (master → slave):
 *   [START] [SLAVE_ADDR | W] [CMD] [DATA...] [STOP]
 *
 * Read (status) transaction (master → slave → master):
 *   [START] [SLAVE_ADDR | W] [DDS_CMD_GET_STATUS]
 *   [REPEATED-START] [SLAVE_ADDR | R] [9 bytes: dds_status_pkt_t] [STOP]
 *
 * All multi-byte integer fields are transmitted little-endian.
 *
 * Integration example
 * -------------------
 * @code
 *   #include "i2c_dds_master.h"
 *
 *   static dds_master_t s_dds;
 *
 *   void app_main(void)
 *   {
 *       ESP_ERROR_CHECK(dds_master_init_new_bus(&s_dds,
 *                                               PIN_DDS_SDA, PIN_DDS_SCL,
 *                                               I2C_NUM_0, DDS_I2C_SPEED_HZ));
 *
 *       // Set slave 0 (0x50): 440 Hz sine wave, 1.5 mA, output on.
 *       dds_slave_state_t st = { .freq_hz = 440.0f, .amplitude_mA = 1.5f,
 *                                .waveform = DDS_WAVE_SINE, .enabled = true };
 *       ESP_ERROR_CHECK(dds_master_set_all(&s_dds, 0, &st));
 *   }
 * @endcode
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/* =========================================================================
 * Constants
 * ========================================================================= */

/** Number of RP2040 Zero slave devices managed by this driver. */
#define DDS_NUM_SLAVES            8

/** Command limits, not a guarantee of the attached analog output capability.
 * The RP2040 must enforce the same limits and support the requested sample rate.
 * See docs/dds_upgrade.md for the DMA and hardware requirements.
 */
#define DDS_MIN_FREQ_HZ           0.001f
#define DDS_MAX_FREQ_HZ           30000.0f
#define DDS_MAX_AMPLITUDE_MA      5.0f

/**
 * @brief 7-bit I2C base address for slave index 0.
 *
 * RP2040 Zero slaves occupy the address range [DDS_SLAVE_BASE_ADDR,
 * DDS_SLAVE_BASE_ADDR + DDS_NUM_SLAVES - 1] = 0x50 – 0x57.
 * This range does not conflict with the SSD1306 OLED (0x3C).
 */
#define DDS_SLAVE_BASE_ADDR       0x50

/**
 * @brief I2C clock speed — 100 kHz Standard Mode.
 *
 * FIX: Reduced from 400 kHz to 100 kHz to accommodate the ISO1641B isolator.
 * The isolator's ~1 µs blanking time is negligible at 100 kHz (10 µs bit
 * period) but consumed ~40 % of each bit window at 400 kHz, corrupting ACKs
 * and read-phase data.  The RP2040 slave baudrate (I2C_BAUDRATE in
 * i2c_slave.c) must be set to 100000 to match.
 */
#define DDS_I2C_SPEED_HZ          100000

/**
 * @brief Per-transaction I2C timeout in milliseconds.
 *
 * Each RP2040 Zero has up to DDS_I2C_TIMEOUT_MS to ACK a transaction before
 * the master considers it a failure.
 */
#define DDS_I2C_TIMEOUT_MS        50

/* =========================================================================
 * Command bytes (master → slave)
 * ========================================================================= */

/** Set waveform output frequency.  Payload: uint32_t freq_mHz (LE). */
#define DDS_CMD_SET_FREQ          0x01

/** Set waveform shape.  Payload: uint8_t waveform (dds_waveform_t). */
#define DDS_CMD_SET_WAVEFORM      0x02

/** Set peak output amplitude.  Payload: uint16_t amplitude_uA (LE). */
#define DDS_CMD_SET_AMPLITUDE     0x03

/** Enable or disable the waveform output.  Payload: uint8_t (0=off, 1=on). */
#define DDS_CMD_SET_ENABLE        0x04

/**
 * @brief Set all DDS parameters in a single 9-byte transaction.
 *
 * Payload layout: dds_set_all_pkt_t (excluding the cmd byte which is the
 * first byte of the write buffer, so the total packet is sizeof that struct).
 */
#define DDS_CMD_SET_ALL           0x05

/**
 * @brief Request a status readback.
 *
 * The master writes this single byte then immediately issues a repeated-START
 * read for sizeof(dds_status_pkt_t) bytes.
 */
#define DDS_CMD_GET_STATUS        0x06

/**
 * @brief Soft-reset the slave DDS engine.
 *
 * The slave resets its waveform output and clears any fault state.
 * No additional payload bytes.
 */
#define DDS_CMD_RESET             0xFF

/* =========================================================================
 * Types
 * ========================================================================= */

/**
 * @brief Waveform shape identifiers.
 *
 * @note Values must match the corresponding enum in the RP2040 Zero firmware.
 */
typedef enum {
    DDS_WAVE_SINE     = 0,  /**< Sinusoidal waveform  */
    DDS_WAVE_SQUARE   = 1,  /**< Square / digital waveform */
    DDS_WAVE_TRIANGLE = 2,  /**< Triangle waveform */
    DDS_WAVE_SAWTOOTH = 3,  /**< Rising sawtooth waveform */
} dds_waveform_t;

/**
 * @brief Live state of a single DDS slave channel.
 *
 * Used both as the "desired" state sent to a slave and as the decoded
 * representation of a status readback.
 */
typedef struct {
    float          freq_hz;       /**< Requested frequency in Hz (0.001 - 30000) */
    float          amplitude_mA;  /**< Requested peak current in mA (0 - 5) */
    dds_waveform_t waveform;      /**< Active waveform shape */
    bool           enabled;       /**< true when waveform output is active */
} dds_slave_state_t;

/**
 * @brief Wire-format packet sent for DDS_CMD_SET_ALL.
 *
 * The struct is written directly to the I2C bus (after ensuring it is packed).
 * All multi-byte fields are little-endian.
 *
 * Total size: 9 bytes.
 */
typedef struct __attribute__((packed)) {
    uint8_t  cmd;           /**< DDS_CMD_SET_ALL (0x05)              */
    uint32_t freq_mHz;      /**< Frequency × 1 000  (millihertz)     */
    uint16_t amplitude_uA;  /**< Amplitude × 1 000  (microamps)      */
    uint8_t  waveform;      /**< dds_waveform_t cast to uint8_t       */
    uint8_t  enabled;       /**< 0 = output off, 1 = output on        */
} dds_set_all_pkt_t;

/**
 * @brief Wire-format status response from DDS_CMD_GET_STATUS.
 *
 * Read back from the slave after a DDS_CMD_GET_STATUS write.
 * All multi-byte fields are little-endian.
 *
 * Total size: 9 bytes.
 */
typedef struct __attribute__((packed)) {
    uint32_t freq_mHz;      /**< Echo of active frequency (mHz)      */
    uint16_t amplitude_uA;  /**< Echo of active amplitude (µA)       */
    uint8_t  waveform;      /**< Echo of active waveform              */
    uint8_t  enabled;       /**< Echo of enable state                 */
    uint8_t  fault;         /**< 0 = no fault; non-zero = fault code  */
} dds_status_pkt_t;

/**
 * @brief DDS master driver instance.
 *
 * Allocate statically or on the stack and pass a pointer to the init
 * function.  Do not access members directly after initialisation.
 */
typedef struct {
    i2c_master_bus_handle_t bus_handle;                   /**< I2C bus (may be shared) */
    i2c_master_dev_handle_t dev_handles[DDS_NUM_SLAVES];  /**< One handle per slave    */
    dds_slave_state_t       slave_state[DDS_NUM_SLAVES];  /**< Locally-cached states   */
    bool                    owns_bus;                     /**< true if we created bus  */
    bool                    initialized;                  /**< Safety guard            */
} dds_master_t;

/* =========================================================================
 * Initialisation / Teardown
 * ========================================================================= */

/**
 * @brief Initialise the driver using an existing I2C bus handle.
 *
 * Call this when another peripheral already owns the bus.  The driver will
 * register all DDS_NUM_SLAVES RP2040 Zero slave devices but will NOT delete
 * the bus in dds_master_deinit().
 *
 * @param drv  Pointer to an uninitialised dds_master_t.
 * @param bus  Open bus handle.
 * @return     ESP_OK on success.
 *             ESP_ERR_INVALID_ARG if drv or bus is NULL.
 */
esp_err_t dds_master_init(dds_master_t *drv, i2c_master_bus_handle_t bus);

/**
 * @brief Initialise the driver, creating a new dedicated I2C bus.
 *
 * Use this when the RP2040 Zero slaves are wired to a free I2C controller.
 * The driver takes ownership of the bus and will destroy it in
 * dds_master_deinit().
 *
 * @param drv       Pointer to an uninitialised dds_master_t.
 * @param sda_gpio  GPIO number for the SDA signal.
 * @param scl_gpio  GPIO number for the SCL signal.
 * @param port_num  I2C controller index (I2C_NUM_0 or I2C_NUM_1).
 *                  Pass -1 to auto-select an available controller.
 * @param speed_hz  SCL clock frequency in Hz (typically DDS_I2C_SPEED_HZ).
 * @return          ESP_OK on success.
 */
esp_err_t dds_master_init_new_bus(dds_master_t  *drv,
                                  int            sda_gpio,
                                  int            scl_gpio,
                                  int            port_num,
                                  uint32_t       speed_hz);

/**
 * @brief Release all device handles and (if owned) the bus handle.
 *
 * Safe to call even if initialisation failed partway through.
 *
 * @param drv Driver instance previously passed to an init function.
 * @return    ESP_OK on success.
 */
esp_err_t dds_master_deinit(dds_master_t *drv);

/* =========================================================================
 * Per-slave write commands
 * ========================================================================= */

/**
 * @brief Set the waveform output frequency for one slave.
 *
 * The value is transmitted as a 32-bit millihertz integer, giving a
 * wire resolution of 0.001 Hz. Float input precision varies with magnitude.
 * Values must be finite and within DDS_MIN_FREQ_HZ through DDS_MAX_FREQ_HZ.
 *
 * @param drv       Driver instance.
 * @param slave_idx Slave index (0 – DDS_NUM_SLAVES-1).
 * @param freq_hz   Desired frequency in Hz.
 * @return          ESP_OK on success.
 *                  ESP_ERR_INVALID_ARG for an invalid index or frequency.
 *                  ESP_ERR_INVALID_STATE if the driver is not initialised.
 */
esp_err_t dds_master_set_freq(dds_master_t *drv,
                              uint8_t       slave_idx,
                              float         freq_hz);

/**
 * @brief Set the waveform shape for one slave.
 *
 * @param drv       Driver instance.
 * @param slave_idx Slave index (0 – DDS_NUM_SLAVES-1).
 * @param wave      Desired waveform (dds_waveform_t).
 * @return          ESP_OK on success.
 */
esp_err_t dds_master_set_waveform(dds_master_t  *drv,
                                  uint8_t        slave_idx,
                                  dds_waveform_t wave);

/**
 * @brief Set the peak output amplitude for one slave.
 *
 * The value is transmitted as a 16-bit microamp integer, giving a resolution
 * of 0.001 mA. The wire field can represent 65.535 mA, but this driver only
 * accepts finite values from 0 through DDS_MAX_AMPLITUDE_MA (5 mA peak).
 * Sending a current request does not measure or increase hardware capability.
 *
 * @param drv          Driver instance.
 * @param slave_idx    Slave index (0 – DDS_NUM_SLAVES-1).
 * @param amplitude_mA Peak amplitude in milliamps (0.0 - 5.0).
 * @return             ESP_OK on success.
 *                     ESP_ERR_INVALID_ARG for an invalid index or amplitude.
 */
esp_err_t dds_master_set_amplitude(dds_master_t *drv,
                                   uint8_t       slave_idx,
                                   float         amplitude_mA);

/**
 * @brief Enable or disable the waveform output for one slave.
 *
 * @param drv       Driver instance.
 * @param slave_idx Slave index (0 – DDS_NUM_SLAVES-1).
 * @param enable    true = start output, false = stop output.
 * @return          ESP_OK on success.
 */
esp_err_t dds_master_set_enable(dds_master_t *drv,
                                uint8_t       slave_idx,
                                bool          enable);

/**
 * @brief Send all DDS parameters to one slave in a single I2C transaction.
 *
 * More efficient than calling the individual setters when multiple parameters
 * change simultaneously.  On success the internal state cache is updated.
 *
 * @param drv       Driver instance.
 * @param slave_idx Slave index (0 – DDS_NUM_SLAVES-1).
 * @param state     Parameters to apply.  Must not be NULL.
 * @return          ESP_OK on success.
 *                  ESP_ERR_INVALID_ARG for invalid frequency/amplitude/waveform.
 */
esp_err_t dds_master_set_all(dds_master_t            *drv,
                             uint8_t                  slave_idx,
                             const dds_slave_state_t *state);

/**
 * @brief Broadcast the same DDS parameters to all eight slaves.
 *
 * Each slave is addressed individually (I2C has no native broadcast for
 * acknowledged writes).  Returns the first error encountered; all slaves
 * up to the failing one will have been updated.
 *
 * @param drv   Driver instance.
 * @param state Parameters to apply to every slave.
 * @return      ESP_OK only if all eight transactions succeeded.
 */
esp_err_t dds_master_broadcast_all(dds_master_t            *drv,
                                   const dds_slave_state_t *state);

/* =========================================================================
 * Status / control
 * ========================================================================= */

/**
 * @brief Read the current DDS status from one slave.
 *
 * Issues a DDS_CMD_GET_STATUS write followed by a repeated-START read of
 * sizeof(dds_status_pkt_t) bytes.
 *
 * @param drv        Driver instance.
 * @param slave_idx  Slave index (0 – DDS_NUM_SLAVES-1).
 * @param out_state  Filled with slave-reported parameters.  Must not be NULL.
 * @param out_fault  Set to true if the slave reports a non-zero fault code.
 * @return           ESP_OK on success.
 */
esp_err_t dds_master_get_status(dds_master_t      *drv,
                                uint8_t            slave_idx,
                                dds_slave_state_t *out_state,
                                bool              *out_fault);

/**
 * @brief Send a soft-reset command to one slave.
 *
 * The slave is expected to stop its waveform output, clear any fault state,
 * and return to a known idle configuration.  The local state cache for that
 * slave is zeroed.
 *
 * @param drv       Driver instance.
 * @param slave_idx Slave index (0 – DDS_NUM_SLAVES-1).
 * @return          ESP_OK on success.
 */
esp_err_t dds_master_reset_slave(dds_master_t *drv, uint8_t slave_idx);

/**
 * @brief Send a soft-reset command to all slaves.
 *
 * Continues through all slaves regardless of individual errors; returns the
 * first error encountered (or ESP_OK if all succeeded).
 *
 * @param drv Driver instance.
 * @return    ESP_OK if every slave was successfully reset.
 */
esp_err_t dds_master_reset_all(dds_master_t *drv);

/**
 * @brief Return true if the slave at slave_idx was found on the bus during init.
 *
 * Slaves that did not ACK their probe transaction have a NULL handle and will
 * return ESP_ERR_NOT_FOUND from any write/read call.  Use this to guard BLE
 * commands before dispatching to avoid triggering that error path.
 *
 * @param drv       Driver instance (const).
 * @param slave_idx Slave index (0 – DDS_NUM_SLAVES-1).
 * @return          true if the slave is present and usable.
 */
bool dds_master_slave_present(const dds_master_t *drv, uint8_t slave_idx);

/**
 * @brief Return a read-only pointer to the locally-cached state for one slave.
 *
 * The cache reflects the last successfully written values.  It does NOT
 * automatically reflect slave-side changes; call dds_master_get_status() to
 * synchronise.
 *
 * @param drv       Driver instance (const).
 * @param slave_idx Slave index (0 – DDS_NUM_SLAVES-1).
 * @return          Pointer to internal dds_slave_state_t, or NULL if the
 *                  driver is uninitialised or the index is out of range.
 */
const dds_slave_state_t *dds_master_get_local_state(const dds_master_t *drv,
                                                    uint8_t             slave_idx);

#ifdef __cplusplus
}
#endif
