#ifndef DDS_TABLES_H
#define DDS_TABLES_H

#include <stdint.h>

#define LUT_SIZE     4096
#define LUT_SHIFT    20       /* phase_acc >> (32 - log2(4096)) = 32 - 12 = 20 */

typedef enum {
    WAVE_SINE     = 0,
    WAVE_SQUARE   = 1,
    WAVE_TRIANGLE = 2,
    WAVE_SAWTOOTH = 3,
    WAVE_TYPE_COUNT
} wave_type_t;

extern uint16_t lut_sine[LUT_SIZE];
extern uint16_t lut_square[LUT_SIZE];
extern uint16_t lut_triangle[LUT_SIZE];
extern uint16_t lut_sawtooth[LUT_SIZE];

extern const uint16_t * const lut_table[WAVE_TYPE_COUNT];

void dds_tables_init(void);
#endif /* DDS_TABLES_H */