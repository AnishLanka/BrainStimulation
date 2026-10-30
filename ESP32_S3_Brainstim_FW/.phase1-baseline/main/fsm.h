/**
 * @file  fsm.h
 * @brief Hardware guardian FSM for the ESP32-S3 DDS controller.
 *
 *   BOOT       Power-on; DDS I2C bus not yet initialised.
 *   IDLE       Hardware ready; channels may be commanded freely via BLE.
 *   EMERGENCY  Soft-stop button pressed; all DDS slave outputs disabled.
 *   FAULT      DDS I2C bus init failed.
 *
 *   BOOT  → EV_HW_OK        → IDLE
 *   BOOT  → EV_HW_FAIL      → FAULT
 *   IDLE  → EV_EMERGENCY    → EMERGENCY  (all slaves disabled)
 *   EMERGENCY  — latched; requires power-cycle to recover
 *   ANY   → EV_BATT_CRIT    → FAULT      (battery critical — all slaves disabled)
 */

#pragma once

#include "i2c_dds_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { STATE_BOOT, STATE_IDLE, STATE_EMERGENCY, STATE_FAULT } FSMState_t;
typedef enum { EV_HW_OK, EV_HW_FAIL, EV_EMERGENCY, EV_BATT_CRIT }    FSMEvent_t;

/**
 * @brief Initialise the FSM.  Must be called after dds_master_init_new_bus().
 * @param dds  Pointer to the initialised DDS master (used to disable slaves on
 *             EV_EMERGENCY).
 */
void        fsm_init(dds_master_t *dds);

FSMState_t  fsm_get_state(void);
const char *fsm_state_name(FSMState_t s);
void        fsm_handle_event(FSMEvent_t ev);

#ifdef __cplusplus
}
#endif
