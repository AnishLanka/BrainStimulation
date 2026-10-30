#pragma once
#include "hardware/i2c.h"
typedef enum {I2C_SLAVE_RECEIVE,I2C_SLAVE_REQUEST,I2C_SLAVE_FINISH} i2c_slave_event_t;
typedef void (*i2c_slave_handler_t)(i2c_inst_t*,i2c_slave_event_t);
void i2c_slave_init(i2c_inst_t*,uint8_t,i2c_slave_handler_t);
