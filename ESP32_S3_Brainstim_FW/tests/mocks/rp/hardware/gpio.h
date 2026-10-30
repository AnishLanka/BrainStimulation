#include "pico/stdlib.h"
#define GPIO_IN 0
#define GPIO_FUNC_I2C 3
static inline void gpio_pull_down(unsigned p){}
static inline void gpio_set_function(unsigned p,unsigned f){}
static inline void gpio_disable_pulls(unsigned p){}
static inline void sleep_us(unsigned u){}
