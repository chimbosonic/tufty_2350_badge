// ST7789 8-bit parallel display driver for the Tufty 2350.
// Ported to C from pimoroni/tufty2350 modules/c/st7789 (MIT, Copyright (c) Pimoroni Ltd).
// Changes: C instead of C++, 4bpp palettised source framebuffer, no MicroPython hooks.
#include "st7789.h"

#include <math.h>

#include "gfx.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
#include "st7789_parallel.pio.h"

#define PIN_CS    27
#define PIN_DC    28
#define PIN_WR    30
#define PIN_RD    31
#define PIN_D0    32
#define PIN_BL    26

#define BACKLIGHT_MIN 8192
#define MAX_PIO_CLK   (44 * MHZ)

enum {
    SWRESET = 0x01, SLPOUT = 0x11, INVON = 0x21, DISPON = 0x29, CASET = 0x2A, RASET = 0x2B,
    RAMWR = 0x2C, TEON = 0x35, MADCTL = 0x36, COLMOD = 0x3A, STE = 0x44, RAMCTRL = 0xB0,
    PORCTRL = 0xB2, GCTRL = 0xB7, VCOMS = 0xBB, LCMCTRL = 0xC0, VDVVRHEN = 0xC2, VRHS = 0xC3,
    VDVS = 0xC4, FRCTRL2 = 0xC6, PWCTRL1 = 0xD0, GMCTRP1 = 0xE0, GMCTRN1 = 0xE1,
};
#define MADCTL_ROW_ORDER  0x80
#define MADCTL_SCAN_ORDER 0x10

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | ((b) >> 3)))
#define BE(v) ((uint16_t)__builtin_bswap16(v))

// Indexed by the C_* palette constants in gfx.h; stored big-endian for the 8-bit bus.
static const uint16_t palette[16] = {
    [C_BLACK]      = BE(RGB565(0, 0, 0)),
    [C_WHITE]      = BE(RGB565(255, 255, 255)),
    [C_GREY]       = BE(RGB565(170, 170, 170)),
    [C_DARKGREY]   = BE(RGB565(60, 60, 60)),
    [C_ACCENT]     = BE(RGB565(0, 190, 170)),
    [C_ACCENT_DIM] = BE(RGB565(0, 70, 64)),
    [C_YELLOW]     = BE(RGB565(255, 200, 0)),
    [C_GREEN]      = BE(RGB565(40, 200, 60)),
    [C_RED]        = BE(RGB565(230, 40, 40)),
    [C_BLUE]       = BE(RGB565(60, 110, 255)),
};

static PIO pio = pio1;
static uint sm;
static uint dma;
static uint16_t linebuffer[GFX_HEIGHT * 2];

static void configure_dma(bool read_increment) {
    dma_channel_config cfg = dma_channel_get_default_config(dma);
    channel_config_set_read_increment(&cfg, read_increment);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_dreq(&cfg, pio_get_dreq(pio, sm, true));
    dma_channel_configure(dma, &cfg, &pio->txf[sm], NULL, 0, false);
}

static void wait_for_dma(void) {
    dma_channel_wait_for_finish_blocking(dma);
    // Wait for the PIO to drain too, so CS/DC never change mid-byte.
    uint32_t stall = 1u << (sm + PIO_FDEBUG_TXSTALL_LSB);
    pio->fdebug = stall;
    while (!(pio->fdebug & stall)) {
    }
}

static void start_dma(const void *src, size_t len) {
    dma_channel_set_trans_count(dma, len, false);
    dma_channel_set_read_addr(dma, src, true);
}

static void write_blocking(const void *src, size_t len) {
    start_dma(src, len);
    wait_for_dma();
}

static void command(uint8_t cmd, size_t len, const char *data) {
    wait_for_dma();
    gpio_put(PIN_DC, 0);
    gpio_put(PIN_CS, 0);
    write_blocking(&cmd, 1);
    if (data) {
        gpio_put(PIN_DC, 1);
        write_blocking(data, len);
    }
    gpio_put(PIN_CS, 1);
}

void st7789_set_backlight(uint8_t brightness) {
    if (brightness == 0) {
        pwm_set_gpio_level(PIN_BL, 0);
        return;
    }
    float level = powf(brightness / 255.0f, 2.8f);
    pwm_set_gpio_level(PIN_BL, BACKLIGHT_MIN + (uint16_t)(level * (65535.0f - BACKLIGHT_MIN) + 0.5f));
}

void st7789_init(void) {
    // Data pins are GPIO32..39, so this PIO block must address the upper bank.
    pio_set_gpio_base(pio, 16);
    sm = (uint)pio_claim_unused_sm(pio, true);
    int offset = pio_add_program(pio, &st7789_parallel_program);
    if (offset < 0) {
        panic("st7789: no PIO space");
    }
    pio_gpio_init(pio, PIN_WR);
    for (uint i = 0; i < 8; i++) {
        pio_gpio_init(pio, PIN_D0 + i);
    }
    pio_sm_set_consecutive_pindirs(pio, sm, PIN_D0, 8, true);
    pio_sm_set_consecutive_pindirs(pio, sm, PIN_WR, 1, true);

    pio_sm_config c = st7789_parallel_program_get_default_config((uint)offset);
    sm_config_set_out_pins(&c, PIN_D0, 8);
    sm_config_set_sideset_pins(&c, PIN_WR);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_out_shift(&c, false, true, 8);
    float hz = (float)clock_get_hz(clk_sys);
    sm_config_set_clkdiv(&c, ceilf(2.f * fmaxf(1.0f, hz / MAX_PIO_CLK)) * 0.5f);
    pio_sm_init(pio, sm, (uint)offset, &c);
    pio_sm_set_enabled(pio, sm, true);

    dma = (uint)dma_claim_unused_channel(true);
    configure_dma(true);

    gpio_init(PIN_RD);
    gpio_set_dir(PIN_RD, GPIO_OUT);
    gpio_put(PIN_RD, 1);
    gpio_init(PIN_DC);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, 65535);
    pwm_init(pwm_gpio_to_slice_num(PIN_BL), &cfg, true);
    gpio_set_function(PIN_BL, GPIO_FUNC_PWM);
    st7789_set_backlight(0);

    command(SWRESET, 0, NULL);
    sleep_ms(150);
    command(COLMOD, 1, "\x05");
    command(PORCTRL, 5, "\x0c\x0c\x00\x33\x33");
    command(LCMCTRL, 1, "\x2c");
    command(VDVVRHEN, 1, "\x01");
    command(VRHS, 1, "\x0f");
    command(VDVS, 1, "\x20");
    command(PWCTRL1, 2, "\xa4\xa1");
    command(FRCTRL2, 1, "\x0f");
    command(RAMCTRL, 2, "\x00\xc0");
    command(GCTRL, 1, "\x35");
    command(VCOMS, 1, "\x1b");
    command(GMCTRP1, 14, "\xF0\x00\x06\x04\x05\x05\x31\x44\x48\x36\x12\x12\x2B\x34");
    command(GMCTRN1, 14, "\xF0\x0B\x0F\x0F\x0D\x26\x31\x43\x47\x38\x14\x14\x2C\x32");
    command(INVON, 0, NULL);
    command(SLPOUT, 0, NULL);
    sleep_ms(100);

    command(CASET, 4, "\x00\x00\x00\xef");  // 0..239
    command(RASET, 4, "\x00\x00\x01\x3f");  // 0..319
    uint8_t madctl = MADCTL_ROW_ORDER | MADCTL_SCAN_ORDER;
    command(MADCTL, 1, (const char *)&madctl);
    command(TEON, 1, "\x00");
    command(STE, 2, "\x00\x00");

    gfx_clear(C_BLACK);
    st7789_update();
    command(DISPON, 0, NULL);
}

void st7789_update(void) {
    wait_for_dma();
    uint8_t cmd = RAMWR;
    gpio_put(PIN_DC, 0);
    gpio_put(PIN_CS, 0);
    write_blocking(&cmd, 1);
    gpio_put(PIN_DC, 1);

    // The panel scans in columns: emit each x as a 240-pixel run, converting one
    // half of the linebuffer while DMA streams the other (also rotates 90 degrees).
    uint16_t *a = linebuffer, *b = linebuffer + GFX_HEIGHT;
    for (int x = 0; x < GFX_WIDTH; x++) {
        for (int y = 0; y < GFX_HEIGHT; y++) {
            a[y] = palette[gfx_get(x, y)];
        }
        wait_for_dma();
        start_dma(a, GFX_HEIGHT * sizeof(uint16_t));
        uint16_t *t = a; a = b; b = t;
    }
    wait_for_dma();
    gpio_put(PIN_CS, 1);
}
