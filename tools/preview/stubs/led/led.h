#pragma once
#include <stdint.h>
enum { MODE_MOUNTED = 1, MODE_PROCESSING = 2 };
uint32_t led_get_mode(void);
