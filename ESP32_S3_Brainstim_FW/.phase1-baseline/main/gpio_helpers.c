/**
 * @file  gpio_helpers.c
 * @brief Channel low-voltage rail GPIO control helpers.
 */

#include "gpio_helpers.h"
#include "gpio_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "GPIO";

/* Ordered CH1→CH4 so index 0 = CH1, 1 = CH2, … */
static const gpio_num_t k_ch_lv_pins[4] = {
    PIN_CH1_LV_EN,
    PIN_CH2_LV_EN,
    PIN_CH3_LV_EN,
    PIN_CH4_LV_EN,
};

esp_err_t channel_rails_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_CH1_LV_EN) |
                        (1ULL << PIN_CH2_LV_EN) |
                        (1ULL << PIN_CH3_LV_EN) |
                        (1ULL << PIN_CH4_LV_EN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    /* Drive all lines LOW before configuring the pin direction so the rails
     * never glitch high during the gpio_config() call. */
    gpio_set_level(PIN_CH1_LV_EN, 0);
    gpio_set_level(PIN_CH2_LV_EN, 0);
    gpio_set_level(PIN_CH3_LV_EN, 0);
    gpio_set_level(PIN_CH4_LV_EN, 0);

    esp_err_t ret = gpio_config(&io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "channel_rails_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Channel LV rails initialised — all disabled");
    return ESP_OK;
}

void channel_rails_disable_all(void)
{
    for (int i = 0; i < 4; i++) {
        gpio_set_level(k_ch_lv_pins[i], 0);
    }
    ESP_LOGW(TAG, "All channel LV rails disabled");
}

void channel_rails_set(uint8_t ch, bool enable)
{
    if (ch >= 4) {
        ESP_LOGE(TAG, "channel_rails_set: invalid channel %u", ch);
        return;
    }
    if (enable) {
        gpio_set_level(k_ch_lv_pins[ch], 1);
        vTaskDelay(pdMS_TO_TICKS(200)); //Increase or decrease as needed based on hardware characteristics
        ESP_LOGI(TAG, "CH%u LV rail enabled", ch + 1);
    } else {
        gpio_set_level(k_ch_lv_pins[ch], 0);
        ESP_LOGI(TAG, "CH%u LV rail disabled", ch + 1);
    }
}
