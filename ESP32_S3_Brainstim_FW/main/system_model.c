#include "system_model.h"

/* Global system state — zero-initialised at startup.
 * dac_update_all_channels() reads port enable/fault/tone fields from here.
 * For direct BLE command control the DAC API is called directly from main.c
 * without going through this model. */
SystemModel_t g_system = {0};
