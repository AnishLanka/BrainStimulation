#include "fsm.h"
#include "gpio_helpers.h"
#include "gpio_pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <string.h>

static const char *TAG = "FSM";
static atomic_int state;
static atomic_uint events;
static dds_master_t *dds;
static bool ready[DDS_NUM_SLAVES], powered[DDS_NUM_SLAVES], stopped[DDS_NUM_SLAVES];
static bool shutting_down, timed_out;
static TickType_t stop_start, last_poll;
static FSMState_t stop_result;
#define EVENT(ev) (1U << (ev))
#define STOP_EVENTS (EVENT(EV_EMERGENCY) | EVENT(EV_BATT_CRIT) | EVENT(EV_BLE_DISCONNECT) | EVENT(EV_CHANNEL_FAULT) | EVENT(EV_HW_FAIL))

void fsm_init(dds_master_t *driver) {
    dds = driver; atomic_store(&state, STATE_BOOT); atomic_store(&events, 0);
    memset(ready, 0, sizeof(ready)); memset(powered, 0, sizeof(powered));
    memset(stopped, 0, sizeof(stopped)); shutting_down = timed_out = false; last_poll = 0;
}
FSMState_t fsm_get_state(void) { return (FSMState_t)atomic_load(&state); }
const char *fsm_state_name(FSMState_t s) {
    switch(s) {
    case STATE_BOOT: return "BOOT"; case STATE_IDLE: return "IDLE";
    case STATE_STOPPING: return "STOPPING"; case STATE_STOPPED: return "STOPPED";
    case STATE_EMERGENCY: return "EMERGENCY"; case STATE_FAULT: return "FAULT";
    default: return "UNKNOWN";
    }
}
void fsm_handle_event(FSMEvent_t ev) { atomic_fetch_or(&events, EVENT(ev)); }
bool fsm_commands_allowed(void) {
    return fsm_get_state() == STATE_IDLE && !(atomic_load(&events) & STOP_EVENTS);
}
bool fsm_channel_ready(uint8_t ch) {
    return ch < DDS_NUM_SLAVES && ready[ch] && fsm_commands_allowed();
}
void fsm_power_up(void) {
    /* LV rails were started before bus discovery. No HV until a v2 OFF status. */
    for (uint8_t ch = 0; ch < DDS_NUM_SLAVES; ++ch) {
        if (!dds_master_slave_present(dds, ch)) { (void)channel_rails_set(ch, false); continue; }
        powered[ch] = true;
        (void)dds_master_set_enable(dds, ch, false);
        TickType_t start = xTaskGetTickCount();
        do {
            dds_slave_state_t st; bool fault;
            if (dds_master_get_status(dds, ch, &st, &fault) == ESP_OK &&
                !fault && dds_status_is_off(&st)) {
                if (channel_hv_set(ch, true) == ESP_OK) {
                    vTaskDelay(pdMS_TO_TICKS(DDS_HV_SETTLE_MS));
                    ready[ch] = true;
                } else ESP_LOGW(TAG, "CH%u unavailable: confirm HV pin/polarity", ch);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(DDS_SERVICE_MS));
        } while ((TickType_t)(xTaskGetTickCount() - start) < pdMS_TO_TICKS(DDS_CARD_READY_TIMEOUT_MS));
        if (!ready[ch]) ESP_LOGW(TAG, "CH%u is not ready for stimulation", ch);
    }
    atomic_store(&state, STATE_IDLE);
}
void fsm_service(void) {
    unsigned pending = atomic_exchange(&events, 0);
    if (pending & STOP_EVENTS) {
        FSMState_t result = (pending & (EVENT(EV_BATT_CRIT) | EVENT(EV_CHANNEL_FAULT) | EVENT(EV_HW_FAIL))) ?
            STATE_FAULT : (pending & EVENT(EV_EMERGENCY)) ? STATE_EMERGENCY : STATE_STOPPED;
        if (!shutting_down && fsm_get_state() != STATE_STOPPED &&
            fsm_get_state() != STATE_EMERGENCY && fsm_get_state() != STATE_FAULT) {
            shutting_down = true; timed_out = false; stop_start = xTaskGetTickCount();
            stop_result = result; memset(ready, 0, sizeof(ready));
            atomic_store(&state, STATE_STOPPING);
            for (uint8_t ch = 0; ch < DDS_NUM_SLAVES; ++ch)
                if (powered[ch]) (void)dds_master_set_enable(dds, ch, false);
            last_poll = xTaskGetTickCount() - pdMS_TO_TICKS(DDS_SERVICE_MS);
        } else if (shutting_down && result > stop_result) stop_result = result;
        else if (result == STATE_FAULT) atomic_store(&state, STATE_FAULT);
    }
    TickType_t now = xTaskGetTickCount();
    if ((TickType_t)(now - last_poll) < pdMS_TO_TICKS(DDS_SERVICE_MS)) return;
    last_poll = now;
    bool all_off = true;
    for (uint8_t ch = 0; ch < DDS_NUM_SLAVES; ++ch) {
        if (!powered[ch] || stopped[ch]) continue;
        dds_slave_state_t st; bool fault;
        /* GET_STATUS is also the 250 ms command heartbeat. */
        esp_err_t err = dds_master_get_status(dds, ch, &st, &fault);
        if (shutting_down) {
            if (err == ESP_OK && dds_status_is_off(&st)) {
                /* Fault flags do not invalidate positive OFF/zero evidence. */
                if (channel_rails_set(ch, false) == ESP_OK) {
                    stopped[ch] = true; powered[ch] = false; continue;
                }
            }
            all_off = false;
            /* Retry lost disable writes; never infer zero from a failed read. */
            (void)dds_master_set_enable(dds, ch, false);
        } else if (ready[ch] && (err != ESP_OK || fault)) {
            fsm_handle_event(EV_CHANNEL_FAULT);
        }
    }
    if (!shutting_down) return;
    if (all_off) {
        shutting_down = false;
        atomic_store(&state, timed_out ? STATE_FAULT : stop_result);
        ESP_LOGW(TAG, "All populated outputs confirmed off; HV then LV disabled");
    } else if (!timed_out &&
               (TickType_t)(now - stop_start) >= pdMS_TO_TICKS(DDS_SHUTDOWN_TIMEOUT_MS)) {
        timed_out = true; atomic_store(&state, STATE_FAULT);
        ESP_LOGE(TAG, "Shutdown unconfirmed after 65 s; retain unverified rails, retry OFF/status");
        /* TODO: confirm system-level policy for permanent communications failure.
         * Do not silently turn a failed status read into a forced current cutoff. */
    }
}
