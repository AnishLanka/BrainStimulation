#pragma once
#include <stdint.h>
#include <stdbool.h>
void spi_dac_init(void);
void spi_dac_write(uint16_t value); /* Boot only, before streaming. */
bool spi_dac_iso_fault(void);       /* ISOW7741 EN/FLT of the streamed channel is low. */
void spi_dac_stream_start(uint32_t phase_inc, const uint16_t *lut);
bool spi_dac_stream_service(uint32_t phase_inc, const uint16_t *lut, uint32_t scale);
bool spi_dac_stream_zero(void);
uint32_t spi_dac_stream_max_scale(void);
