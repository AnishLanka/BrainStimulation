#include "gpio_helpers.h"
#include "gpio_pins.h"
#include "driver/gpio.h"
static const int lv_pins[DDS_NUM_SLAVES] = {PIN_CH1_LV_EN, PIN_CH2_LV_EN, PIN_CH3_LV_EN, PIN_CH4_LV_EN};
static const int hv_pins[DDS_NUM_SLAVES] = {PIN_CH1_HV_DISABLE, PIN_CH2_HV_DISABLE, PIN_CH3_HV_DISABLE, PIN_CH4_HV_DISABLE};
static bool hv_configured(uint8_t ch) { return ch < DDS_NUM_SLAVES && hv_pins[ch] >= 0; }
bool channel_hv_available(uint8_t ch) { return CH_HV_CONTROL_CONFIRMED && hv_configured(ch); }
static esp_err_t init_output(int pin, int safe_level) {
    esp_err_t err = gpio_set_level(pin, safe_level); /* preload BEFORE output direction */
    if (err != ESP_OK) return err;
    gpio_config_t io = {.pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE};
    err = gpio_config(&io);
    if (err == ESP_OK) err = gpio_sleep_sel_dis(pin);
    return err;
}
esp_err_t channel_rails_init(void) {
    for (uint8_t ch = 0; ch < DDS_NUM_SLAVES; ++ch) {
        int pin = hv_pins[ch];
        if (pin < 0) continue;
        if (!GPIO_IS_VALID_OUTPUT_GPIO(pin) || IS_FLASH_PSRAM_PIN(pin) ||
            (pin >= 35 && pin <= 37) || IS_USB_PIN(pin) || IS_UART_PIN(pin) ||
            IS_STRAPPING_PIN(pin) || pin == PIN_DDS_SCL || pin == PIN_DDS_SDA ||
            pin == PIN_OLED_SCL || pin == PIN_OLED_SDA ||
            pin == PIN_SOFT_STOP_PB || pin == PIN_V_BAT_MEAS) return ESP_ERR_INVALID_ARG;
        for (uint8_t i = 0; i < DDS_NUM_SLAVES; ++i)
            if (pin == lv_pins[i] || (i != ch && pin == hv_pins[i])) return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t ch = 0; ch < DDS_NUM_SLAVES; ++ch) {
        if (hv_configured(ch)) {
            esp_err_t err = init_output(hv_pins[ch], CH_HV_OFF_LEVEL);
            if (err != ESP_OK) return err;
        }
    }
    for (uint8_t ch = 0; ch < DDS_NUM_SLAVES; ++ch) {
        esp_err_t err = init_output(lv_pins[ch], 0);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
esp_err_t channel_hv_set(uint8_t ch, bool enable) {
    if (ch >= DDS_NUM_SLAVES) return ESP_ERR_INVALID_ARG;
    if (!hv_configured(ch)) return enable ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (enable && !channel_hv_available(ch)) return ESP_ERR_INVALID_STATE;
    return gpio_set_level(hv_pins[ch], enable ? !CH_HV_OFF_LEVEL : CH_HV_OFF_LEVEL);
}
esp_err_t channel_rails_set(uint8_t ch, bool enable) {
    if (ch >= DDS_NUM_SLAVES) return ESP_ERR_INVALID_ARG;
    if (!enable) { esp_err_t err = channel_hv_set(ch, false); if (err != ESP_OK) return err; }
    return gpio_set_level(lv_pins[ch], enable ? 1 : 0);
}
void channel_rails_disable_all(void) {
    for (uint8_t ch = 0; ch < DDS_NUM_SLAVES; ++ch) (void)channel_hv_set(ch, false);
    for (uint8_t ch = 0; ch < DDS_NUM_SLAVES; ++ch) (void)channel_rails_set(ch, false);
}
