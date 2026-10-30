/**
 * @file  gpio_helpers.h
 * @brief Channel low-voltage rail GPIO control helpers.
 *
 * Each channel has an independent LV enable (PIN_CHx_LV_EN) configured as a
 * push-pull output.
 *
 * Safe default: all rails driven LOW (disabled).
 * Call channel_rails_init() early in app_main() so the rails are off before
 * any other hardware initialisation takes place.
 * Call channel_rails_disable_all() on any HW fault or emergency condition.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure CH1–CH4 LV pins as push-pull outputs and drive all LOW.
 *
 * Must be called once, as early as possible in app_main(), before any task
 * that might assert an enable line.
 *
 * @return ESP_OK on success, or an esp_err_t code from gpio_config().
 */
esp_err_t channel_rails_init(void);

/**
 * @brief Drive all four channel LV enable pins LOW (all rails off).
 *
 * Safe to call from any task context (FSM handler, fault handler, etc.).
 * Used at boot and whenever a HW fault or emergency is declared.
 */
void channel_rails_disable_all(void);

/**
 * @brief Enable or disable a single channel's LV rail.
 *
 * @param ch      Zero-based channel index (0 = CH1, 1 = CH2, …, 3 = CH4).
 * @param enable  true → drive HIGH (rail on), false → drive LOW (rail off).
 */
void channel_rails_set(uint8_t ch, bool enable);

#ifdef __cplusplus
}
#endif
