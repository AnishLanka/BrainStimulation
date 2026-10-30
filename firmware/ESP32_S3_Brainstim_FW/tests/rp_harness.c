#include "dds_state.h"
#include "spi_dac.h"
#include "pico/stdlib.h"
static dds_request_t req;
static dds_status_t stat;
static uint32_t applied, applied_inc;
extern void test_request(void);
void command(int enable,int amplitude,int heartbeat,int reset) {
    if (!enable && req.enabled) ++req.stop_generation;
    if (reset && !req.reset_generation) {++req.reset_generation;++req.stop_generation;}
    req.enabled=enable;req.amplitude_uA=amplitude;
    if(heartbeat) req.last_command_us=time_us_32();
}
void dds_shared_init(void) {req.freq_mHz=DEFAULT_FREQ_MHZ;}
void dds_request_read(dds_request_t *out){test_request();*out=req;}
void dds_request_write(const dds_request_t *in){req=*in;}
void dds_status_read(dds_status_t *out){*out=stat;}
void dds_status_write(const dds_status_t *in){stat=*in;}
void spi_dac_init(void){}
bool spi_dac_iso_fault(void){return false;}
void spi_dac_stream_start(uint32_t i,const uint16_t*l){}
bool spi_dac_stream_service(uint32_t i,const uint16_t*l,uint32_t s){applied=s;applied_inc=i;return true;}
void set_freq(uint32_t mHz){req.freq_mHz=mHz;}
uint32_t applied_phase(void){return applied_inc;}
bool spi_dac_stream_zero(void){return applied==0;}
uint32_t spi_dac_stream_max_scale(void){return applied;}
void app_i2c_slave_init(void){}
void i2c_slave_task(void){}
uint32_t applied_scale(void){return applied;}
unsigned output_state(void){return stat.state;}
unsigned fault_code(void){return stat.fault;}
uint32_t amp(uint16_t v){return calc_amp_scale(v);}
uint32_t phase(uint32_t v){return calc_phase_inc(v);}
int check_tables(void){
 for(unsigned w=0;w<WAVE_TYPE_COUNT;++w) {
  uint64_t sum=0;
  for(unsigned i=0;i<LUT_SIZE;++i) {
   uint16_t v=lut_table[w][i];sum+=v;
   if(!v || scale_sample(v,0)!=DAC_MIDPOINT || scale_sample(v,AMP_Q16_ONE)!=v)return 1;
  }
  if(sum!=(uint64_t)DAC_MIDPOINT*LUT_SIZE)return 2;
 }
 return 0;
}
