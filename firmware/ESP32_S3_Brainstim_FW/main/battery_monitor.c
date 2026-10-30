#include "battery_monitor.h"
#include "adc_voltage.h"
#include "gpio_pins.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

/*Note: Code adapted from Neurotune*/

#define BATTERY_ADC_CHANNEL    ADC_CHANNEL_0

#define BATTERY_SAMPLE_COUNT   32

// R1 = 33kohm, R2 = 10kohm -> ratio = (R1+R2)/R2 = 4.3
#define BATTERY_R1_OHM         33000.0f
#define BATTERY_R2_OHM         10000.0f
#define BATTERY_DIVIDER_RATIO  ((BATTERY_R1_OHM + BATTERY_R2_OHM) / BATTERY_R2_OHM)


#define BATTERY_OFFSET_V       0.0f // If you find a consistent offset (e.g. due to ADC nonlinearity), you can set it here.

// Voltage thresholds for 3S Li-ion (11.1V nominal, 12.6V full, 9.9V safe empty)
#define BATTERY_MIN_V          9.0f    // Informational lower bound only
#define BATTERY_MAX_V          12.6f   // Full charge
#define BATTERY_WARN_V         10.65f  // Warn below this
#define BATTERY_WARN_CLEAR_V   10.80f  // Clear warn above this
#define BATTERY_CRIT_V         9.90f   // Critical / safe empty
#define BATTERY_CRIT_CLEAR_V   10.20f  // Clear crit above this

static const char *TAG = "BAT_MON";
static bool s_initialized = false;

static float convert_adc_mv_to_battery_v(int adc_mv) {
    if (adc_mv < 0) {
        return -1.0f;
    }
    float sensed_v = adc_mv / 1000.0f;
    return (sensed_v * BATTERY_DIVIDER_RATIO) + BATTERY_OFFSET_V;
}

bool battery_monitor_init(void) {
    if (s_initialized) {
        return true;
    }

    if (!adc_voltage_init(BATTERY_ADC_CHANNEL, PIN_V_BAT_MEAS)) {
        ESP_LOGE(TAG, "ADC init failed on GPIO%d", PIN_V_BAT_MEAS);
        return false;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Battery monitor initialized (GPIO%d, R1=%.0f, R2=%.0f, scale=%.4fx)",
             PIN_V_BAT_MEAS, BATTERY_R1_OHM, BATTERY_R2_OHM, BATTERY_DIVIDER_RATIO);
    return true;
}

bool battery_monitor_sample(float *voltage_v) {
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    int adc_mv = adc_voltage_read_averaged_mv(BATTERY_SAMPLE_COUNT);
    float battery_v = convert_adc_mv_to_battery_v(adc_mv);
    if (battery_v < 0.0f) {
        ESP_LOGW(TAG, "Invalid ADC reading (mv=%d)", adc_mv);
        return false;
    }

    if (voltage_v) {
        *voltage_v = battery_v;
    }
    return true;
}

int battery_monitor_estimate_percentage(float voltage_v) {
    if (voltage_v >= BATTERY_MAX_V) return 100;
    if (voltage_v <= BATTERY_CRIT_V) return 0;

    float normalized = (voltage_v - BATTERY_CRIT_V) / (BATTERY_MAX_V - BATTERY_CRIT_V);
    int percent = (int)(normalized * 100.0f + 0.5f);
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    return percent;
}

float battery_monitor_warn_threshold(void)       { return BATTERY_WARN_V; }
float battery_monitor_crit_threshold(void)       { return BATTERY_CRIT_V; }
float battery_monitor_warn_clear_threshold(void) { return BATTERY_WARN_CLEAR_V; }
float battery_monitor_crit_clear_threshold(void) { return BATTERY_CRIT_CLEAR_V; }
float battery_monitor_scale_factor(void)         { return BATTERY_DIVIDER_RATIO; }
float battery_monitor_offset(void)               { return BATTERY_OFFSET_V; }