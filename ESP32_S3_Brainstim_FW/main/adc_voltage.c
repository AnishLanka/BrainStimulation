#include "adc_voltage.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

/*Note: Code adapted from Neurotune*/

static const char *TAG = "ADC_VOLTAGE";

// ADC handles
static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static adc_channel_t current_channel = ADC_CHANNEL_0;
static bool adc_initialized = false;
static bool calibration_enabled = false;

bool adc_voltage_init(int adc_channel, int gpio_num) {
    if (adc_initialized) {
        ESP_LOGW(TAG, "ADC already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing ADC on GPIO %d (ADC channel %d)", gpio_num, adc_channel);
    
    // Configure ADC unit
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,  // Using ADC1 (ADC2 is used by WiFi)
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC unit: %s", esp_err_to_name(ret));
        return false;
    }

    // Configure ADC channel
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,      // 12-bit resolution (0-4095)
        .atten = ADC_ATTEN_DB_12,         // 12dB attenuation (0-3.1V range)
    };
    
    current_channel = (adc_channel_t)adc_channel;
    ret = adc_oneshot_config_channel(adc_handle, current_channel, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
        return false;
    }

    // Initialize calibration (ESP32-S3 uses curve fitting instead of line fitting)
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
    if (ret == ESP_OK) {
        calibration_enabled = true;
        ESP_LOGI(TAG, "ADC calibration enabled (curve fitting)");
    } else {
        ESP_LOGW(TAG, "ADC calibration not available: %s", esp_err_to_name(ret));
        calibration_enabled = false;
    }

    adc_initialized = true;
    ESP_LOGI(TAG, "ADC initialized successfully");
    ESP_LOGI(TAG, "  Resolution: 12-bit (0-4095)");
    ESP_LOGI(TAG, "  Voltage range: 0-3.1V");
    ESP_LOGI(TAG, "  Attenuation: 12dB");
    
    return true;
}

int adc_voltage_read_raw(void) {
    if (!adc_initialized) {
        ESP_LOGE(TAG, "ADC not initialized");
        return -1;
    }

    int raw_value = 0;
    esp_err_t ret = adc_oneshot_read(adc_handle, current_channel, &raw_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ADC: %s", esp_err_to_name(ret));
        return -1;
    }

    return raw_value;
}

int adc_voltage_read_mv(void) {
    if (!adc_initialized) {
        ESP_LOGE(TAG, "ADC not initialized");
        return -1;
    }

    int raw_value = adc_voltage_read_raw();
    if (raw_value < 0) {
        return -1;
    }

    int voltage_mv = 0;
    
    if (calibration_enabled) {
        // Use calibration for accurate voltage reading
        esp_err_t ret = adc_cali_raw_to_voltage(adc_cali_handle, raw_value, &voltage_mv);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to convert raw to voltage: %s", esp_err_to_name(ret));
            return -1;
        }
    } else {
        // Fallback to linear conversion (less accurate)
        voltage_mv = (raw_value * 3100) / 4095; // Scale to 0-3.1V range
    }
    
    return voltage_mv;
}

float adc_voltage_read_volts(void) {
    int voltage_mv = adc_voltage_read_mv();
    if (voltage_mv < 0) {
        return -1.0f;
    }
    return voltage_mv / 1000.0f;
}

int adc_voltage_read_averaged_mv(int num_samples) {
    if (!adc_initialized) {
        ESP_LOGE(TAG, "ADC not initialized");
        return -1;
    }

    if (num_samples < 1) {
        num_samples = 1;
    }
    if (num_samples > 64) {
        num_samples = 64;  // Limit to prevent excessive delays
    }

    int64_t sum = 0;
    int valid_samples = 0;

    for (int i = 0; i < num_samples; i++) {
        int voltage_mv = adc_voltage_read_mv();
        if (voltage_mv >= 0) {
            sum += voltage_mv;
            valid_samples++;
        }
    }

    if (valid_samples == 0) {
        ESP_LOGE(TAG, "No valid samples obtained");
        return -1;
    }

    return (int)(sum / valid_samples);
}

void adc_voltage_deinit(void) {
    if (!adc_initialized) {
        return;
    }

    if (calibration_enabled && adc_cali_handle != NULL) {
        adc_cali_delete_scheme_curve_fitting(adc_cali_handle);
        adc_cali_handle = NULL;
        calibration_enabled = false;
    }

    if (adc_handle != NULL) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
    }

    adc_initialized = false;
    ESP_LOGI(TAG, "ADC deinitialized");
}