// Copyright (c) 2026 Alexis Lowe. AGPL-3.0, as part of this pico-openpgp fork.
#include "gfx.h"

#include <string.h>

#include "qrcodegen.h"

uint8_t gfx_fb[GFX_WIDTH * GFX_HEIGHT / 2];

void gfx_clear(uint8_t c) {
    memset(gfx_fb, (c & 0x0f) | (c << 4), sizeof(gfx_fb));
}

void gfx_pixel(int x, int y, uint8_t c) {
    if (x < 0 || y < 0 || x >= GFX_WIDTH || y >= GFX_HEIGHT) {
        return;
    }
    uint8_t *p = &gfx_fb[(y * GFX_WIDTH + x) >> 1];
    if (x & 1) {
        *p = (*p & 0x0f) | (uint8_t)(c << 4);
    }
    else {
        *p = (*p & 0xf0) | (c & 0x0f);
    }
}

void gfx_rect(int x, int y, int w, int h, uint8_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GFX_WIDTH) { w = GFX_WIDTH - x; }
    if (y + h > GFX_HEIGHT) { h = GFX_HEIGHT - y; }
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int yy = y; yy < y + h; yy++) {
        int xx = x, end = x + w;
        // Unaligned leading pixel, then whole bytes, then trailing pixel.
        if (xx & 1) {
            gfx_pixel(xx++, yy, c);
        }
        int bytes = (end - xx) >> 1;
        if (bytes > 0) {
            memset(&gfx_fb[(yy * GFX_WIDTH + xx) >> 1], (c & 0x0f) | (c << 4), (size_t)bytes);
            xx += bytes << 1;
        }
        if (xx < end) {
            gfx_pixel(xx, yy, c);
        }
    }
}

void gfx_frame(int x, int y, int w, int h, int t, uint8_t c) {
    gfx_rect(x, y, w, t, c);
    gfx_rect(x, y + h - t, w, t, c);
    gfx_rect(x, y, t, h, c);
    gfx_rect(x + w - t, y, t, h, c);
}

int gfx_text(const gfx_font_t *f, int x, int y, int scale, uint8_t c, const char *s) {
    for (; *s; s++) {
        uint8_t ch = (uint8_t)*s;
        if (ch < f->first || ch > f->last) {
            ch = '?';
        }
        const uint8_t *g = f->data + (size_t)(ch - f->first) * f->height * f->stride;
        for (int row = 0; row < f->height; row++) {
            for (int col = 0; col < f->width; col++) {
                if (g[row * f->stride + (col >> 3)] & (0x80 >> (col & 7))) {
                    if (scale == 1) {
                        gfx_pixel(x + col, y + row, c);
                    }
                    else {
                        gfx_rect(x + col * scale, y + row * scale, scale, scale, c);
                    }
                }
            }
        }
        x += f->width * scale;
    }
    return x;
}

int gfx_text_width(const gfx_font_t *f, int scale, const char *s) {
    return (int)strlen(s) * f->width * scale;
}

void gfx_text_centered(const gfx_font_t *f, int y, int scale, uint8_t c, const char *s) {
    gfx_text(f, (GFX_WIDTH - gfx_text_width(f, scale, s)) / 2, y, scale, c, s);
}

bool gfx_qr(const char *text, int cx, int cy, int max_px) {
    // Version 10 holds ~170 alphanumeric/byte chars at ECC M: plenty for a URL.
    static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    static uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    if (!qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true)) {
        return false;
    }
    int size = qrcodegen_getSize(qr);
    int total = size + 8;  // 4-module quiet zone each side
    int px = max_px / total;
    if (px < 1) {
        return false;
    }
    int side = total * px;
    int x0 = cx - side / 2, y0 = cy - side / 2;
    gfx_rect(x0, y0, side, side, C_WHITE);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (qrcodegen_getModule(qr, x, y)) {
                gfx_rect(x0 + (x + 4) * px, y0 + (y + 4) * px, px, px, C_BLACK);
            }
        }
    }
    return true;
}
