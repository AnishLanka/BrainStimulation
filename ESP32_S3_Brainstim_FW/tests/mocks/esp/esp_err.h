#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_FOUND 3
#define ESP_ERR_INVALID_RESPONSE 4
#define ESP_ERR_TIMEOUT 5
const char *esp_err_to_name(esp_err_t);
