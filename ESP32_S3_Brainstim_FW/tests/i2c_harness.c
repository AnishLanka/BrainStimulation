#include "dds_state.h"
#include "i2c_slave.h"
#include "pico/i2c_slave.h"
static dds_request_t r;
static dds_status_t s;
static i2c_slave_handler_t handler;
static uint8_t incoming,outgoing;
uint32_t test_now(void){return 12345;}
void test_wait(void){}
void test_gpio(unsigned p,unsigned v){}
void dds_request_read(dds_request_t *out){*out=r;}
void dds_request_write(const dds_request_t *in){r=*in;}
void dds_status_read(dds_status_t *out){*out=s;}
uint8_t i2c_read_byte_raw(i2c_inst_t*p){return incoming;}
void i2c_write_byte_raw(i2c_inst_t*p,uint8_t v){outgoing=v;}
void i2c_slave_init(i2c_inst_t*p,uint8_t a,i2c_slave_handler_t h){handler=h;}
void init(void){r.freq_mHz=DEFAULT_FREQ_MHZ;s.freq_mHz=30000000;s.amplitude_uA=5000;s.current_uA=1234;s.state=OUTPUT_RAMP_DOWN;s.switches=SWITCH_OUT;app_i2c_slave_init();}
void receive(unsigned v){incoming=v;handler(i2c1,I2C_SLAVE_RECEIVE);}
void finish(void){handler(i2c1,I2C_SLAVE_FINISH);}
unsigned request(void){handler(i2c1,I2C_SLAVE_REQUEST);return outgoing;}
unsigned freq(void){return r.freq_mHz;}
unsigned amp(void){return r.amplitude_uA;}
unsigned wave(void){return r.waveform;}
unsigned enabled(void){return r.enabled;}
unsigned timestamp(void){return r.last_command_us;}
