// Minimal 4-bit-per-pixel framebuffer graphics for the Tufty 2350 (320x240).
// Copyright (c) 2026 Alexis Lowe. AGPL-3.0, as part of this pico-openpgp fork.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GFX_WIDTH  320
#define GFX_HEIGHT 240

// Palette indices. The RGB565 values live in st7789.c.
enum {
    C_BLACK = 0,
    C_WHITE,
    C_GREY,
    C_DARKGREY,
    C_ACCENT,     // OpenPGP-ish teal
    C_ACCENT_DIM,
    C_YELLOW,
    C_GREEN,
    C_RED,
    C_BLUE,
};

typedef struct {
    uint8_t width;
    uint8_t height;
    uint8_t stride;   // bytes per glyph row
    uint8_t first;
    uint8_t last;
    const uint8_t *data;
} gfx_font_t;

// Two pixels per byte; low nibble = even x.
extern uint8_t gfx_fb[GFX_WIDTH * GFX_HEIGHT / 2];

void gfx_clear(uint8_t c);
void gfx_pixel(int x, int y, uint8_t c);
void gfx_rect(int x, int y, int w, int h, uint8_t c);
void gfx_frame(int x, int y, int w, int h, int t, uint8_t c);

// Draws text at integer scale; returns the x after the last glyph.
int gfx_text(const gfx_font_t *f, int x, int y, int scale, uint8_t c, const char *s);
int gfx_text_width(const gfx_font_t *f, int scale, const char *s);
void gfx_text_centered(const gfx_font_t *f, int y, int scale, uint8_t c, const char *s);

// Renders a QR code for `text` centred at (cx, cy), at most `max_px` square,
// with a 4-module white quiet zone. Returns false if it does not fit or encode.
bool gfx_qr(const char *text, int cx, int cy, int max_px);

static inline uint8_t gfx_get(int x, int y) {
    uint8_t b = gfx_fb[(y * GFX_WIDTH + x) >> 1];
    return (x & 1) ? (b >> 4) : (b & 0x0f);
}
