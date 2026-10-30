#include "freertos/task.h"
#include "i2c_dds_master.h"
#include "fsm.h"
#include "gpio_helpers.h"
#include <string.h>
static dds_master_t driver;
static dds_status_pkt_t response;
static unsigned writes, off_requests, poweroffs, tick, present_mask, read_error;
static uint8_t last_write[16];
static unsigned last_length;
void *memset(void *p,int c,size_t n){unsigned char *d=p;while(n--)*d++=c;return p;}
void *memcpy(void *p,const void *s,size_t n){unsigned char *d=p;const unsigned char *a=s;while(n--)*d++=*a++;return p;}
const char *esp_err_to_name(esp_err_t e){return "mock";}
esp_err_t i2c_master_bus_add_device(void*b,const i2c_device_config_t*c,void**d){*d=(void*)(uintptr_t)(c->device_address-DDS_SLAVE_BASE_ADDR+1);return 0;}
esp_err_t i2c_master_probe(void*b,uint16_t a,int t){return present_mask & (1u<<(a-DDS_SLAVE_BASE_ADDR)) ? 0:3;}
esp_err_t i2c_master_bus_rm_device(void*d){return 0;}
esp_err_t i2c_master_transmit(void*d,const uint8_t*b,size_t n,int t){
 ++writes;memcpy(last_write,b,n);last_length=n;
 if(b[0]==DDS_CMD_SET_ENABLE && !b[1])++off_requests;
 return 0;
}
esp_err_t i2c_master_transmit_receive(void*d,const uint8_t*b,size_t n,uint8_t*r,size_t count,int t){
 if(read_error)return ESP_ERR_TIMEOUT;memcpy(r,&response,count);return 0;
}
esp_err_t i2c_master_bus_reset(void*b){return 0;}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t*c,void**b){*b=(void*)1;return 0;}
esp_err_t i2c_del_master_bus(void*b){return 0;}
TickType_t xTaskGetTickCount(void){return tick;}
void vTaskDelay(TickType_t t){tick+=t;}
esp_err_t channel_hv_set(uint8_t c,bool e){return 0;}
bool channel_hv_available(uint8_t c){return true;}
esp_err_t channel_rails_set(uint8_t c,bool e){if(!e)++poweroffs;return 0;}
void channel_rails_disable_all(void){poweroffs+=4;}
void init(unsigned mask){
 memset(&response,0,sizeof(response));response.freq_mHz=1000000;response.version=2;response.magic=0xA5;
 present_mask=mask;writes=off_requests=poweroffs=read_error=tick=0;
 dds_master_init(&driver,(void*)1);fsm_init(&driver);
}
int freq(float f){return dds_master_set_freq(&driver,0,f);}
int amp(float a){return dds_master_set_amplitude(&driver,0,a);}
int wave(unsigned w){return dds_master_set_waveform(&driver,0,w);}
unsigned write_count(void){return writes;}
unsigned wire(unsigned i){return i<last_length?last_write[i]:0;}
int get_status(void){dds_slave_state_t s;bool f;return dds_master_get_status(&driver,0,&s,&f);}
int is_off(void){return dds_status_is_off(&driver.slave_state[0]);}
void set_status(unsigned version,unsigned magic,unsigned state,unsigned current,unsigned switches,unsigned enabled){
 response.version=version;response.magic=magic;response.output_state=state;
 response.current_amplitude_uA=current;response.switches=switches;response.enabled=enabled;
}
void set_read_error(unsigned v){read_error=v;}
void power_up(void){fsm_power_up();poweroffs=0;}
unsigned poweroff_count(void){return poweroffs;}
unsigned off_count(void){return off_requests;}
void advance(unsigned ms){tick+=ms;fsm_service();}
void stop(void){fsm_handle_event(EV_BLE_DISCONNECT);}
int allowed(void){return fsm_commands_allowed();}
int state(void){return fsm_get_state();}
int present(unsigned i){return dds_master_slave_present(&driver,i);}
int broadcast(void){dds_slave_state_t s={.freq_hz=1000,.amplitude_mA=1};return dds_master_broadcast_all(&driver,&s);}
