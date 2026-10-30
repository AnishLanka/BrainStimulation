#pragma once
#include <stdint.h>
typedef int i2c_inst_t;
#define i2c1 ((i2c_inst_t*)1)
uint8_t i2c_read_byte_raw(i2c_inst_t*);
void i2c_write_byte_raw(i2c_inst_t*,uint8_t);
static inline void i2c_init(i2c_inst_t*p,unsigned b){}
