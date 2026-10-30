#pragma once
#include <stdint.h>
/* Both calls run on Core 1, keeping its I2C IRQ away from Core 0 DMA work. */
void app_i2c_slave_init(void);
void i2c_slave_task(void);
uint8_t i2c_slave_get_address(void);
