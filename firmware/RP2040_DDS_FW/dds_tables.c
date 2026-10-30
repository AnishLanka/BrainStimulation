#include "dds_tables.h"
#include "dds_state.h"
#include <math.h>
uint16_t lut_sine[LUT_SIZE], lut_square[LUT_SIZE], lut_triangle[LUT_SIZE], lut_sawtooth[LUT_SIZE];
const uint16_t * const lut_table[WAVE_TYPE_COUNT] = {lut_sine, lut_square, lut_triangle, lut_sawtooth};
void dds_tables_init(void) {
    /* Construct opposing pairs exactly, so every table has average 32768.
     * Every code lies in [1,65535], avoiding asymmetric 0/65535 endpoints. */
    for (unsigned i = 0; i < LUT_SIZE/2; ++i) {
        int32_t sine = (int32_t)lround(32767.0 * sin(6.283185307179586 * i / LUT_SIZE));
        int32_t triangle = i <= LUT_SIZE/4 ?
            (int32_t)(32767UL * i / (LUT_SIZE/4)) :
            (int32_t)(32767UL * (LUT_SIZE/2 - i) / (LUT_SIZE/4));
        lut_sine[i] = DAC_MIDPOINT + sine; lut_sine[i+LUT_SIZE/2] = DAC_MIDPOINT - sine;
        lut_square[i] = DAC_MIDPOINT + 32767; lut_square[i+LUT_SIZE/2] = DAC_MIDPOINT - 32767;
        lut_triangle[i] = DAC_MIDPOINT + triangle; lut_triangle[i+LUT_SIZE/2] = DAC_MIDPOINT - triangle;
        int32_t saw = -32767 + (int32_t)(65534UL * i / (LUT_SIZE-1));
        lut_sawtooth[i] = DAC_MIDPOINT + saw; lut_sawtooth[LUT_SIZE-1-i] = DAC_MIDPOINT - saw;
    }
}
