#ifndef OLED_DRIVER_H
#define OLED_DRIVER_H

/* ===========================================================================
 * SSD1306 0.96" OLED Display Driver
 *
 * Hardware: 128 x 64 pixel I2C OLED (SSD1306 controller)
 *           Yellow-blue dual-color, 3.3 V / 5 V compatible
 *
 * Uses a dedicated I2C_NUM_1 bus, fully independent of the DDS master bus
 * (I2C_NUM_0) used by the 8 RP2040 Zero slaves.
 *
 *   OLED SCL → GPIO 17  (PIN_OLED_SCL, defined in gpio_pins.h)
 *   OLED SDA → GPIO 16  (PIN_OLED_SDA, defined in gpio_pins.h)
 *   OLED I2C address: 0x3C  (SA0 = GND; change to 0x3D if SA0 = VCC)
 *
 * Display layout — 1 row per DDS slave (up to 8, driven by DDS_NUM_SLAVES):
 *   Row 0  →  Slave 0 (current test slave at 0x50)
 *   Row 1  →  Slave 1 … Row 7 → Slave 7  (populated as hardware is added)
 *
 * Each row format (18 chars, 5-pixel font + 1 px gap = 6 px/char):
 *   "S0 SIN   440Hz ON "
 *   "S7 SAW  1000Hz OFF"
 *
 * Each row: 21 characters wide (5-pixel font + 1-pixel inter-char gap = 6 px)
 * =========================================================================*/

#include <stdint.h>
#include <stdbool.h>
#include "gpio_pins.h"
#include "i2c_dds_master.h"

/* ---------------------------------------------------------------------------
 * Hardware constants
 * ------------------------------------------------------------------------- */
#define OLED_I2C_ADDR           0x3C    /* 7-bit address; change to 0x3D if SA0=VCC */
#define OLED_I2C_SCL_PIN        PIN_OLED_SCL    /* GPIO 17                       */
#define OLED_I2C_SDA_PIN        PIN_OLED_SDA    /* GPIO 16                       */
#define OLED_I2C_FREQ_HZ        400000  /* 400 kHz fast-mode                     */
#define OLED_I2C_TIMEOUT_MS     50

#define OLED_WIDTH              128     /* pixels horizontal                     */
#define OLED_HEIGHT             64      /* pixels vertical                       */
#define OLED_PAGES              8       /* OLED_HEIGHT / 8 — one page = 8 px    */

/* ---------------------------------------------------------------------------
 * Font geometry (5-pixel-wide glyphs + 1 px inter-character gap)
 * ------------------------------------------------------------------------- */
#define OLED_CHAR_W             6       /* total advance per character (px)      */
#define OLED_CHARS_PER_ROW      (OLED_WIDTH / OLED_CHAR_W)   /* 21 chars        */

/* ---------------------------------------------------------------------------
 * Display-task update interval
 * ------------------------------------------------------------------------- */
#define OLED_UPDATE_MS          1000    /* refresh the screen every 1 second     */

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * @brief  Initialise the SSD1306 display on its own dedicated I2C bus
 *         (I2C_NUM_1, GPIO 16/17).  Call once at startup.
 *
 * @return true on success, false on I2C error.
 */
bool oled_init(void);

/**
 * @brief  Clear the local framebuffer and push blank screen to display.
 */
void oled_clear(void);

/**
 * @brief  Write a NUL-terminated string into the framebuffer at (page, col).
 *
 * Characters that overflow the right edge are silently clipped.
 * Call oled_flush() afterwards to push changes to the display.
 *
 * @param  page  Page row 0-7 (each page is 8 pixels tall).
 * @param  col   Pixel column 0-127.
 * @param  str   String to render.
 */
void oled_write_string(uint8_t page, uint8_t col, const char *str);

/**
 * @brief  Flush the framebuffer to the physical display over I2C.
 *
 * Sends one page at a time (128 bytes each) and yields briefly between
 * pages so other tasks can run during the update.
 */
void oled_flush(void);

/**
 * @brief  Set the battery charge level shown in the top-right indicator.
 *
 * Stores the level; the indicator is redrawn automatically on the next call
 * to oled_update_slave_display().  Wire this to the ADC battery measurement
 * (PIN_V_BAT_MEAS, GPIO 1) when hardware is ready.
 *
 * The indicator occupies the rightmost 14 pixels of page-row 0 (shifted right
 * to make room for the 6-pixel Bluetooth icon at cols 108-113).
 *
 * @param  percent  0 = empty, 100 = full.  Values > 100 are clamped to 100.
 *                  Pass 255 to display an "unknown" pattern (diagonal fill).
 */
void oled_set_battery_level(uint8_t percent);

/**
 * @brief  Update the BLE connection state shown on the OLED.
 *
 * When connected, a 5×8 Bluetooth symbol is drawn at cols 108-112 of page 0
 * (the top-right status area, just left of the battery indicator).
 * When disconnected the symbol is cleared.
 *
 * Call this from the GAP BLE_GAP_EVENT_CONNECT / BLE_GAP_EVENT_DISCONNECT
 * handlers in main.c.  Thread-safe: only updates a static flag; the actual
 * pixels are written on the next oled_update_slave_display() call.
 *
 * @param  connected  true = BLE client connected, false = disconnected.
 */
void oled_set_ble_connected(bool connected);

/**
 * @brief  Compose the slave-status screen and flush it to the display.
 *
 * Reads locally-cached state from the DDS master driver and renders one
 * row per slave (waveform, frequency, enable state).  All 8 slaves fit
 * on the 128×64 screen simultaneously (1 page row each).
 *
 * @param  dds  Pointer to the initialised DDS master driver instance.
 */
void oled_update_slave_display(const dds_master_t *dds);

/**
 * @brief  Create and start the FreeRTOS display-refresh task.
 *
 * The task calls oled_update_slave_display() every OLED_UPDATE_MS.
 * Safe to call once after oled_init() succeeds.
 *
 * @param  dds  Pointer to the initialised DDS master driver instance.
 *              The pointer is stored internally; the pointee must remain
 *              valid for the lifetime of the task.
 */
void oled_start_display_task(const dds_master_t *dds);

#endif /* OLED_DRIVER_H */
