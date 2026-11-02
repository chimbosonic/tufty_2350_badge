// Host-side preview of the Tufty UI: compiles the real tufty/ui.c + gfx.c
// against stubs and writes one BMP per screen. Run via tools/preview/run.sh.
#include <stdio.h>
#include <stdlib.h>

#include "../../tufty/ui.c"

// ---- stubs -----------------------------------------------------------------

struct apdu apdu;
volatile bool cancel_button;
static uint32_t fake_ms = 100000;
static bool fake_pending;
uint32_t board_millis(void) { return fake_ms; }
bool is_req_button_pending(void) { return fake_pending; }
uint32_t led_get_mode(void) { return MODE_MOUNTED; }
bool tud_mounted(void) { return false; }
bool tud_suspended(void) { return false; }
bool is_busy(void) { return false; }
int signal_add(signal_code_t c, signal_flag_t f, signal_handler_t h) { (void)c; (void)f; (void)h; return 0; }
void board_tufty_init(void) {}
void board_tufty_late_init(void) {}
uint32_t board_buttons(void) { return 0; }
void board_case_leds(bool on) { (void)on; }
void st7789_init(void) {}
void st7789_set_backlight(uint8_t b) { (void)b; }

static file_t files[8];
static int nfiles;
static void put(uint16_t fid, const void *data, uint32_t size) {
    files[nfiles++] = (file_t){ fid, data, size };
}
file_t *file_search_by_fid(uint16_t fid, const file_t *parent, uint8_t sp) {
    (void)parent; (void)sp;
    for (int i = 0; i < nfiles; i++) {
        if (files[i].fid == fid) {
            return &files[i];
        }
    }
    return NULL;
}

// ---- BMP output ------------------------------------------------------------

static const uint8_t rgb[16][3] = {
    [C_BLACK] = { 0, 0, 0 }, [C_WHITE] = { 255, 255, 255 }, [C_GREY] = { 170, 170, 170 },
    [C_DARKGREY] = { 60, 60, 60 }, [C_ACCENT] = { 0, 190, 170 }, [C_ACCENT_DIM] = { 0, 70, 64 },
    [C_YELLOW] = { 255, 200, 0 }, [C_GREEN] = { 40, 200, 60 }, [C_RED] = { 230, 40, 40 },
    [C_BLUE] = { 60, 110, 255 },
};

static const char *out_path = "/dev/null";   // ui_early_init() renders before main()
void st7789_update(void) {
    FILE *f = fopen(out_path, "wb");
    if (!f) {
        perror(out_path);
        exit(1);
    }
    uint32_t row = GFX_WIDTH * 3, size = 54 + row * GFX_HEIGHT;
    uint8_t h[54] = { 'B', 'M' };
    *(uint32_t *)(h + 2) = size;
    *(uint32_t *)(h + 10) = 54;
    *(uint32_t *)(h + 14) = 40;
    *(int32_t *)(h + 18) = GFX_WIDTH;
    *(int32_t *)(h + 22) = -GFX_HEIGHT;   // top-down
    *(uint16_t *)(h + 26) = 1;
    *(uint16_t *)(h + 28) = 24;
    fwrite(h, 1, 54, f);
    for (int y = 0; y < GFX_HEIGHT; y++) {
        for (int x = 0; x < GFX_WIDTH; x++) {
            const uint8_t *c = rgb[gfx_get(x, y)];
            uint8_t px[3] = { c[2], c[1], c[0] };
            fwrite(px, 1, 3, f);
        }
    }
    fclose(f);
}

static void shot(const char *dir, const char *name) {
    static char path[512];
    snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
    out_path = path;
    render(fake_ms);
    printf("%s\n", path);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";

    out_path = "/dev/null";
    snapshot_refresh();
    screen = SCREEN_BADGE;
    shot(dir, "badge_unconfigured");

    static const char name[] = "Lowe<<Alexis";
    static const char url[] = "https://keyoxide.org/0123456789ABCDEF0123456789ABCDEF01234567";
    static const uint8_t fp_sig[20] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x01, 0x23,
                                        0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67 };
    static const uint8_t fp_dec[20] = { 0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                        0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
    static const uint8_t pw[7] = { 1, 127, 127, 127, 3, 0, 3 };
    static const uint8_t cnt[3] = { 0, 0, 42 };
    put(EF_CH_NAME, name, sizeof(name) - 1);
    put(EF_URI_URL, url, sizeof(url) - 1);
    put(EF_FP_SIG, fp_sig, 20);
    put(EF_FP_DEC, fp_dec, 20);
    put(EF_PW_PRIV, pw, 7);
    put(EF_SIG_COUNT, cnt, 3);
    snapshot_refresh();

    badge_view = BADGE_NAME_QR;
    shot(dir, "badge_name_qr");
    badge_view = BADGE_QR;
    shot(dir, "badge_qr");
    badge_view = BADGE_NAME;
    shot(dir, "badge_name");
    badge_view = BADGE_NAME_QR;
    qr_use_fingerprint = true;
    shot(dir, "badge_name_qr_fpr");
    qr_use_fingerprint = false;

    screen = SCREEN_CARD;
    last_op = OP_SIGN;
    last_op_ms = fake_ms - 12000;
    shot(dir, "card");

    screen = SCREEN_CONFIRM;
    confirm_op = OP_SIGN;
    confirm_started_ms = fake_ms - 5000;
    confirm_timeout_ms = 15000;
    shot(dir, "confirm_sign");

    screen = SCREEN_CARD;
    card_busy = true;
    last_op = OP_KEYGEN;
    busy_since_ms = fake_ms - 83000;
    shot(dir, "card_keygen");
    card_busy = false;
    last_op = OP_SIGN;

    toast("Approved", C_GREEN);
    screen = SCREEN_CARD;
    shot(dir, "card_toast");
    info_until_ms = fake_ms + 1000;
    screen = SCREEN_BADGE;
    shot(dir, "info");
    return 0;
}
