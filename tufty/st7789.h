// ST7789 8-bit parallel display driver for the Tufty 2350.
// Ported to C from pimoroni/tufty2350 modules/c/st7789 (MIT, Copyright (c) Pimoroni Ltd).
#pragma once

#include <stdint.h>

void st7789_init(void);
// Pushes gfx_fb to the panel through the palette. Blocks for roughly 10 ms.
void st7789_update(void);
// 0 = off, 1..255 gamma-corrected brightness.
void st7789_set_backlight(uint8_t brightness);
