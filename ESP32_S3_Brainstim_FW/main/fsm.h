#pragma once
#include "i2c_dds_master.h"
typedef enum { STATE_BOOT, STATE_IDLE, STATE_STOPPING, STATE_STOPPED, STATE_EMERGENCY, STATE_FAULT } FSMState_t;
typedef enum { EV_HW_OK, EV_HW_FAIL, EV_EMERGENCY, EV_BATT_CRIT, EV_BLE_DISCONNECT, EV_CHANNEL_FAULT } FSMEvent_t;
void fsm_init(dds_master_t *dds);
FSMState_t fsm_get_state(void);
const char *fsm_state_name(FSMState_t state);
/* Thread-safe event posting; never blocks the BLE/OLED/soft-stop task. */
void fsm_handle_event(FSMEvent_t event);
/* Call once after LV boot and I2C discovery, before starting tasks. */
void fsm_power_up(void);
/* Sole DDS command task owns all subsequent I2C/FSM service. */
void fsm_service(void);
bool fsm_commands_allowed(void);
bool fsm_channel_ready(uint8_t ch);
