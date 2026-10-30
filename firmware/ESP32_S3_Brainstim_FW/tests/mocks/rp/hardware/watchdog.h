#pragma once
#include <stdbool.h>
static inline bool watchdog_caused_reboot(void){return false;}
static inline void watchdog_enable(unsigned t,bool p){}
static inline void watchdog_update(void){}
