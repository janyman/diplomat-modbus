#pragma once

#include <inttypes.h>

typedef uint32_t sys_time_t;

void sys_timer_init(void);

sys_time_t sys_timer_now(void);