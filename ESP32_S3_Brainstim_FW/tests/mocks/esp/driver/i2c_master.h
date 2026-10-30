#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef int i2c_port_num_t;
#define I2C_ADDR_BIT_LEN_7 0
#define I2C_CLK_SRC_DEFAULT 0
typedef struct {int dev_addr_length;uint32_t scl_speed_hz;uint16_t device_address;} i2c_device_config_t;
typedef struct {int i2c_port,sda_io_num,scl_io_num,clk_source,glitch_ignore_cnt;struct {bool enable_internal_pullup;} flags;} i2c_master_bus_config_t;
esp_err_t i2c_master_bus_add_device(void*,const i2c_device_config_t*,void**);
esp_err_t i2c_master_probe(void*,uint16_t,int);
esp_err_t i2c_master_bus_rm_device(void*);
esp_err_t i2c_master_transmit(void*,const uint8_t*,size_t,int);
esp_err_t i2c_master_transmit_receive(void*,const uint8_t*,size_t,uint8_t*,size_t,int);
esp_err_t i2c_master_bus_reset(void*);
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t*,void**);
esp_err_t i2c_del_master_bus(void*);
