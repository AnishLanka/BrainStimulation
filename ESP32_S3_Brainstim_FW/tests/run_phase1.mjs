import fs from 'node:fs';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import assert from 'node:assert/strict';
const root=path.resolve(import.meta.dirname,'..');
const rp=path.resolve(root,'../RP2040_DDS_FW');
const build=path.join(import.meta.dirname,'build');
fs.mkdirSync(build,{recursive:true});
const mocks=path.join(import.meta.dirname,'mocks');
const clang=process.env.CLANG || 'C:/Program Files/LLVM/bin/clang.exe';
function compile(name, sources, include, extra=[]) {
  const dest=path.join(build,name+'.wasm');
  execFileSync(clang,['--target=wasm32','-std=c11','-O2','-nostdlib','-fno-builtin',
    '-Wl,--no-entry','-Wl,--export-all','-Wl,--allow-undefined',
    ...include.flatMap(i=>['-I',i]),...extra,...sources,'-o',dest],{stdio:'inherit'});
  return fs.readFileSync(dest);
}
const rpBinary=compile('rp',[path.join(rp,'main.c'),path.join(rp,'dds_tables.c'),
  path.join(import.meta.dirname,'rp_harness.c')],[path.join(mocks,'rp'),rp],['-Dmain=rp_main']);
for(const scenario of ['normal','timeout','early_stop','reset','wrap','freq_slew']) {
 let instance, ticks=0, now=scenario==='wrap'?0xffff0000:0;
 let out=false,dummy=false,maxScale=0,lastSwitchTime=0,connectedAt=0,finished=false;
 let lastScale=0,lastTime=0,firstRamp=-1,lastRamp=-1,maxRate=0;
 let lastInc=0,lastIncTime=0,maxOctPerSec=0,idleJumpOk=false,reachedAt=-1;
 const stopAt=scenario==='early_stop'?500000:60000000;
 const env={
   sin:Math.sin,lround:Math.round,
   test_now:()=>now>>>0,
   test_request:()=>{
     const e=instance.exports, elapsed=Math.floor(ticks*819.2);
     let enabled=elapsed<stopAt, heartbeat=1;
     if(scenario==='timeout' && elapsed>=60000000) { enabled=true;heartbeat=0; }
     e.command(enabled?1:0,5000,heartbeat,scenario==='reset'&&elapsed>=stopAt?1:0);
   },
   test_gpio:(pin,value)=>{
     const elapsed=Math.floor(ticks*819.2);
     if(pin===0 && !!value!==out) {
       assert.equal(instance.exports.applied_scale(),0,'OUT switched with nonzero scale');
       if(value) {assert(dummy);assert(elapsed-lastSwitchTime>=1000000); connectedAt=elapsed;}
       out=!!value;
     }
     if(pin===1 && !!value!==dummy) {
       if(value && !out) lastSwitchTime=elapsed;
       if(!value && out) assert(elapsed-connectedAt>=10000);
       dummy=!!value;
     }
   },
   test_wait:()=>{
     const e=instance.exports, elapsed=Math.floor(ticks*819.2), scale=e.applied_scale();
     maxScale=Math.max(maxScale,scale);
     if(scale && firstRamp<0)firstRamp=elapsed;
     if(scale===65536)lastRamp=elapsed;
     if(scenario==='freq_slew') {
       const inc=e.applied_phase()>>>0;
       // With zero current the new frequency may apply at once.
       if(ticks===100)e.set_freq(2000000);
       if(ticks===102){assert.equal(scale,0);assert.equal(inc,e.phase(2000000)>>>0,'idle frequency applies immediately');idleJumpOk=true;}
       // At full current a 2 kHz -> 30 kHz request must slew, never step.
       if(ticks===Math.ceil(55000000/819.2)){assert.equal(scale,65536);e.set_freq(30000000);lastInc=inc;lastIncTime=elapsed;}
       if(lastIncTime && ticks%100===0 && elapsed>lastIncTime) {
         if(inc!==lastInc)maxOctPerSec=Math.max(maxOctPerSec,Math.abs(Math.log2(inc/lastInc))*1e6/(elapsed-lastIncTime));
         lastInc=inc;lastIncTime=elapsed;
       }
       if(reachedAt<0 && inc===e.phase(30000000)>>>0)reachedAt=elapsed;
     }
     if(ticks%1000===0) {
       if(lastTime && elapsed-lastTime)maxRate=Math.max(maxRate,Math.abs(scale-lastScale)*5000/65536*1e6/(elapsed-lastTime));
       lastScale=scale;lastTime=elapsed;
     }
     if(elapsed>stopAt+1000000 && e.output_state()===0) {
       assert.equal(scale,0); assert(!out&&!dummy);
       if(scenario!=='early_stop') {assert.equal(maxScale,65536);assert(maxRate<100.2);}
       if(scenario==='timeout')assert(e.fault_code()&1);
       if(scenario==='freq_slew') {
         assert(idleJumpOk);
         assert(maxOctPerSec>0.5 && maxOctPerSec<1.6,'frequency slew rate '+maxOctPerSec);
         assert(reachedAt>55000000+2500000 && reachedAt<stopAt,'30 kHz reached by slewing, at '+reachedAt);
       }
       finished=true;throw new Error('DONE');
     }
     if(elapsed>125000000)throw new Error('Simulation did not stop: '+scenario);
     ++ticks;now=(now+819+(ticks%5===0?1:0));
   }
 };
 instance=(await WebAssembly.instantiate(rpBinary,{env})).instance;
 const e=instance.exports;
 assert.equal(e.amp(0),0);assert.equal(e.amp(5000),65536);assert.equal(e.amp(6000),65536);
 assert.equal(e.phase(30000000)>>>0,Math.floor(30000000*4294967296/312500000));
 assert.notEqual(e.phase(500001),e.phase(500000),'mHz precision retained');
 try{e.rp_main();}catch(error){if(error.message!=='DONE')throw error;}
 assert(finished);
 assert.equal(e.check_tables(),0,'LUT midpoint/range/scaling checks');
 console.log('PASS RP2040 '+scenario+'; peak ramp window '+maxRate.toFixed(3)+' uA/s'+
   (scenario==='freq_slew'?'; peak frequency slew '+maxOctPerSec.toFixed(2)+' oct/s':''));
}

const espBinary=compile('esp',[path.join(root,'main/i2c_dds_master.c'),
 path.join(root,'main/fsm.c'),path.join(import.meta.dirname,'esp_harness.c')],
 [path.join(mocks,'esp'),path.join(root,'main')]);
const ee=(await WebAssembly.instantiate(espBinary,{env:{}})).instance.exports;
ee.init(1);
for(const value of [500,30000])assert.equal(ee.freq(value),0);
for(const value of [-1,0,499.9,30001,NaN,Infinity]) {
 const count=ee.write_count();assert.notEqual(ee.freq(value),0);assert.equal(ee.write_count(),count);
}
for(const value of [0,5])assert.equal(ee.amp(value),0);
for(const value of [-1,5.01,NaN,Infinity])assert.notEqual(ee.amp(value),0);
assert.notEqual(ee.wave(255),0);
ee.freq(30000);assert.deepEqual([0,1,2,3,4].map(i=>ee.wire(i)),[1,128,195,201,1]);
ee.amp(5);assert.deepEqual([0,1,2].map(i=>ee.wire(i)),[3,136,19]);
assert.equal(ee.get_status(),0);assert(ee.is_off());
for(const packet of [[0,0,0,0,0,0],[2,0,0,0,0,0],[2,165,9,0,0,0],
 [2,165,0,1,0,0],[2,165,0,0,1,0],[2,165,0,0,4,0]]) {
 ee.set_status(...packet);assert.notEqual(ee.get_status(),0);assert(!ee.is_off());
}
ee.set_status(2,165,5,5000,1,0);assert.equal(ee.get_status(),0);assert(!ee.is_off());
ee.init(5);assert(ee.present(0)&&ee.present(2)&&!ee.present(1)&&!ee.present(3));
assert.equal(ee.broadcast(),0);assert.equal(ee.write_count(),2);
ee.init(0);assert(!ee.present(0));
ee.init(1);ee.power_up();assert(ee.allowed());
ee.set_status(2,165,4,5000,1,1);ee.stop();assert(!ee.allowed(),'stop gates commands immediately');
ee.advance(250);assert.equal(ee.state(),2);assert.equal(ee.poweroff_count(),0);
ee.set_status(2,165,5,100,1,0);ee.advance(53000);assert.equal(ee.poweroff_count(),0);
ee.set_read_error(1);ee.advance(13000);assert.equal(ee.state(),5);assert.equal(ee.poweroff_count(),0);
ee.set_read_error(0);ee.set_status(2,165,0,0,0,0);ee.advance(250);
assert.equal(ee.poweroff_count(),1);assert.equal(ee.state(),5);assert(!ee.allowed());
assert(ee.off_count()>=2);
ee.init(1);ee.power_up();ee.stop();ee.advance(250);
assert.equal(ee.poweroff_count(),1);assert.equal(ee.state(),3);
console.log('PASS ESP32 range/wire validation, old/corrupt status rejection, sparse cards, stop gating and 65 s timeout');


const i2cBinary=compile('i2c',[path.join(rp,'i2c_slave.c'),path.join(import.meta.dirname,'i2c_harness.c')],[path.join(mocks,'rp'),rp]);
const ie=(await WebAssembly.instantiate(i2cBinary,{env:{}})).instance.exports;
ie.init();
const send=(bytes)=>{bytes.forEach(v=>ie.receive(v));ie.finish();};
send([1,129,195,201,1]);assert.equal(ie.freq(),30000000);
send([1,0,0,0,0]);assert.equal(ie.freq(),500000);
send([1,33,161,7,0]);assert.equal(ie.freq(),500001);
send([3,255,255]);assert.equal(ie.amp(),5000);
send([2,255]);assert.equal(ie.wave(),0);
send([4,1,...Array(300).fill(0)]);assert.equal(ie.enabled(),0);
send([4,1,0]);assert.equal(ie.enabled(),0);
send([4,1]);assert.equal(ie.enabled(),1);
ie.receive(6);ie.finish(); // repeated START invokes FINISH before REQUEST
const packet=Array.from({length:15},()=>ie.request());ie.finish();
assert.deepEqual(packet,[128,195,201,1,136,19,0,0,0,2,5,210,4,1,165]);
assert.equal(ie.timestamp(),12345);
ie.receive(6);const direct=Array.from({length:15},()=>ie.request());ie.finish();
assert.deepEqual(direct,packet);
console.log('PASS RP2040 I2C clamps, mHz precision, malformed writes, repeated START and 15-byte status');


const mainSource=fs.readFileSync(path.join(root,'main/main.c'),'utf8');
const commandSource=mainSource.slice(mainSource.indexOf('static void report_slave_fault('),
 mainSource.indexOf('static int nus_rx_write_cb('));
const commandPrefix=`#include "i2c_dds_master.h"
#include "fsm.h"
#include "freertos/task.h"
#include <stddef.h>
extern int errno;
#define ERANGE 34
#define pdTRUE 1
#define DDS_CMD_MAX_LEN 252
typedef struct {char sub[253];} dds_cmd_msg_t;
static void *s_dds_cmd_queue;
static dds_master_t s_dds;
float strtof(const char*,char**);
int isspace(int);
#define isfinite(v) __builtin_isfinite(v)
int snprintf(char*,size_t,const char*,...);
int strcmp(const char*,const char*);
int strncmp(const char*,const char*,size_t);
char *strcpy(char*,const char*);
char *strncpy(char*,const char*,size_t);
int xQueueReceive(void*,void*,unsigned);
int xQueueSend(void*,const void*,unsigned);
void ble_notify_str(const char*);
`;
const commandFile=path.join(build,'ble_commands.c');
fs.writeFileSync(commandFile,commandPrefix+commandSource);
execFileSync(clang,['--target=wasm32','-std=c11','-fsyntax-only','-Werror=implicit-function-declaration',
 '-I',path.join(mocks,'esp'),'-I',path.join(root,'main'),commandFile],{stdio:'inherit'});
console.log('PASS edited BLE command/worker C syntax with SDK mocks');

const html=fs.readFileSync(path.join(root,'web/ble_test.html'),'utf8');
const {parseStatusLine,stateRank,channelLabel}=await import(new URL('../web/dds_status.ts',import.meta.url));
const ramp=parseStatusLine('DDS S0 FREQ=30000.000 WAVE=SINE AMP=5.000 STATE=RAMP_DOWN CURRENT=2.123 OUT=1 DUMMY=0');
assert.equal(ramp.ch,'0');assert.equal(ramp.current,2.123);assert.equal(ramp.freq,30000);
assert.equal(stateRank(ramp.state),2);assert.equal(channelLabel(ramp),'RAMP');
const faulted=parseStatusLine('DDS S1 FREQ=1000.000 WAVE=SINE AMP=1.000 STATE=RAMP_DOWN CURRENT=0.500 OUT=1 DUMMY=0 FAULT');
assert(faulted.fault);assert.equal(channelLabel(faulted),'FAULT');
const absent=parseStatusLine('DDS S3 NOT_PRESENT');
assert.equal(absent.state,'N/A');assert.equal(channelLabel(absent),'N/A');assert.equal(stateRank(absent.state),0);
const failed=parseStatusLine('DDS S2 ERR ESP_ERR_TIMEOUT');
assert.equal(stateRank(failed.state),0,'an I2C error must never read as ramping or on');assert(failed.fault);
assert.equal(parseStatusLine('DDS S4 FREQ=1 WAVE=SINE AMP=0 STATE=OFF CURRENT=0 OUT=0 DUMMY=0'),null);
assert(!html.includes('Channel 7'));assert(html.includes('max="30000"'));assert(html.includes('max="5"'));
for (const id of ['freq-out','amp-out','wave-canvas','channel-status-list','btn-dac-on','btn-dac-off'])
 assert(html.includes(`id="${id}"`),'missing #'+id);
console.log('PASS web status parsing, four channels, faults and live ramp status');
