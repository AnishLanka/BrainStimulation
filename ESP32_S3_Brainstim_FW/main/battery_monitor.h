#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stdbool.h>

/*Note: Code adapted from Neurotune*/

/**
 * @brief Initialize the battery monitor on GPIO34 (ADC1_CH6 / PIN_V_BAT_MEAS).
 *
 * @return true on success, false if ADC setup failed.
 */
bool battery_monitor_init(void);

/**
 * @brief Sample the battery voltage.
 *
 * @param voltage_v Optional pointer to receive the measured pack voltage in volts.
 * @return true when a valid reading was captured.
 */
bool battery_monitor_sample(float *voltage_v);

/**
 * @brief Estimate battery percentage based on voltage.
 *
 * @param voltage_v Battery voltage in volts.
 * @return Estimated percentage (0-100).
 */
int battery_monitor_estimate_percentage(float voltage_v);

/**
 * @brief Minimum healthy voltage threshold (in volts).
 */
float battery_monitor_warn_threshold(void);

/**
 * @brief Critical voltage threshold (in volts).
 */
float battery_monitor_crit_threshold(void);

/**
 * @brief Warn-clear hysteresis threshold (volts).
 */
float battery_monitor_warn_clear_threshold(void);

/**
 * @brief Critical-clear hysteresis threshold (volts).
 */
float battery_monitor_crit_clear_threshold(void);

/**
 * @brief Current scale factor applied to ADC voltage.
 */
float battery_monitor_scale_factor(void);

/**
 * @brief Current offset (volts) applied after scaling.
 */
float battery_monitor_offset(void);

#endif // BATTERY_MONITOR_H
