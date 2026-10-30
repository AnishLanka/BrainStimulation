/**
 * @file  fsm.c
 * @brief Hardware guardian FSM — ESP32-S3 DDS controller.
 */

#include "fsm.h"
#include "gpio_helpers.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "FSM";
static FSMState_t  g_state = STATE_BOOT;
static dds_master_t *g_dds = NULL;

static void disable_all_outputs(void)
{
    /* 1. Stop all DDS slave outputs over I2C first. */
    if (g_dds && g_dds->initialized) {
        for (uint8_t i = 0; i < DDS_NUM_SLAVES; i++) {
            dds_master_set_enable(g_dds, i, false);
        }
        ESP_LOGW(TAG, "All DDS slave outputs disabled");
    }
    /*Wait 30s for all the otuputs to ramp down to zero before cutting the rails.*/
    //This is a safety measure to ensure stimulation is fully stopped before cutting power, to avoid any risk of hardware damage or patient discomfort.
    vTaskDelay(30000 / portTICK_PERIOD_MS);
    /* 2. Then cut the high-voltage rails. */
    channel_rails_disable_all();
}

void fsm_init(dds_master_t *dds)
{
    g_dds   = dds;
    g_state = STATE_BOOT;
    ESP_LOGI(TAG, "BOOT");
}

FSMState_t fsm_get_state(void) { return g_state; }

const char *fsm_state_name(FSMState_t s)
{
    switch (s) {
        case STATE_BOOT:      return "BOOT";
        case STATE_IDLE:      return "IDLE";
        case STATE_EMERGENCY: return "EMERGENCY";
        case STATE_FAULT:     return "FAULT";
        default:              return "UNKNOWN";
    }
}

void fsm_handle_event(FSMEvent_t ev)
{
    FSMState_t old = g_state;

    switch (ev) {
        case EV_HW_OK:
            if (g_state == STATE_BOOT) {
                g_state = STATE_IDLE;
            }
            break;

        case EV_HW_FAIL:
            if (g_state == STATE_BOOT) {
                g_state = STATE_FAULT;
            }
            break;

        case EV_EMERGENCY:
            if (g_state == STATE_IDLE) {
                disable_all_outputs();
                g_state = STATE_EMERGENCY;
            }
            break;

        case EV_BATT_CRIT:
            if (g_state != STATE_FAULT) {
                disable_all_outputs();
                g_state = STATE_FAULT;
            }
            break;
    }

    if (g_state != old) {
        ESP_LOGI(TAG, "%s → %s", fsm_state_name(old), fsm_state_name(g_state));
    }
}
