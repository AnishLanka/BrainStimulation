#ifndef ADC_VOLTAGE_H
#define ADC_VOLTAGE_H

#include <stdint.h>
#include <stdbool.h>

/*Note: Code adapted from Neurotune*/

/**
 * @brief Initialize ADC for voltage measurement
 * 
 * @param adc_channel ADC channel to use (ADC1_CHANNEL_0 to ADC1_CHANNEL_7)
 * @param gpio_num GPIO pin number corresponding to the ADC channel
 * @return true if initialization successful, false otherwise
 */
bool adc_voltage_init(int adc_channel, int gpio_num);

/**
 * @brief Read raw ADC value
 * 
 * @return Raw ADC reading (0-4095 for 12-bit resolution)
 */
int adc_voltage_read_raw(void);

/**
 * @brief Read voltage in millivolts
 * 
 * @return Voltage in millivolts (mV)
 */
int adc_voltage_read_mv(void);

/**
 * @brief Read voltage in volts
 * 
 * @return Voltage in volts (V)
 */
float adc_voltage_read_volts(void);

/**
 * @brief Read voltage with averaging for more stable readings
 * 
 * @param num_samples Number of samples to average (1-64 recommended)
 * @return Averaged voltage in millivolts (mV)
 */
int adc_voltage_read_averaged_mv(int num_samples);

/**
 * @brief Deinitialize ADC
 */
void adc_voltage_deinit(void);

#endif // ADC_VOLTAGE_H
