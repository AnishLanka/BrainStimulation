#ifndef SYSTEM_MODEL_H
#define SYSTEM_MODEL_H

#include <stdint.h>
#include <stdbool.h>

/* Number of output ports and tones per port — must match
 * g_port_tone_to_channel dimensions in dac_driver.c */
#define N_PORTS  4
#define N_TONES  2

typedef enum {
    FAULT_NONE = 0,
} FaultFlags_t;

typedef struct {
    float freq_hz;
    float setpoint_mA;
} ToneState_t;

typedef struct {
    bool         enabled;
    FaultFlags_t fault_flags;
    ToneState_t  tone[N_TONES];
} PortState_t;

typedef struct {
    PortState_t port[N_PORTS];
} SystemModel_t;

extern SystemModel_t g_system;

#endif /* SYSTEM_MODEL_H */
