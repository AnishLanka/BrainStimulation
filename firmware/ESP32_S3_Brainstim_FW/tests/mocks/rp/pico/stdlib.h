#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef unsigned int uint;
#define GPIO_OUT 1
extern uint32_t test_now(void);
extern void test_wait(void);
extern void test_gpio(unsigned,unsigned);
static bool pins[32];
static inline uint32_t time_us_32(void){return test_now();}
static inline void gpio_init(unsigned p){}
static inline void gpio_put(unsigned p,bool v){pins[p]=v;test_gpio(p,v);}
static inline void gpio_set_dir(unsigned p,bool d){}
static inline bool gpio_get(unsigned p){return pins[p];}
static inline void __wfi(void){test_wait();}
static inline void __wfe(void){test_wait();}
