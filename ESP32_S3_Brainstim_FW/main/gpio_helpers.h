#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
esp_err_t channel_rails_init(void);
bool channel_hv_available(uint8_t ch);
esp_err_t channel_hv_set(uint8_t ch, bool enable);
/* Power-off requires a fresh status confirming zero amplitude and OFF. */
esp_err_t channel_rails_set(uint8_t ch, bool enable);
/* Boot cleanup only, when no output has been enabled. HV off precedes LV. */
void channel_rails_disable_all(void);
