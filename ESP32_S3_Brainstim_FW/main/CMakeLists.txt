idf_component_register(SRCS "oled_driver.c" "system_model.c"
                            "i2c_dds_master.c" "fsm.c" "gpio_helpers.c" "adc_voltage.c" "battery_monitor.c" "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES bt nvs_flash log esp_common freertos
                              esp_driver_i2c esp_driver_gpio esp_adc esp_pm)