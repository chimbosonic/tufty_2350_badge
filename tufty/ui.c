// Tufty 2350 front-end for pico-openpgp: conference badge when unplugged,
// smartcard status + on-screen user-presence confirmation when plugged in.
//
// Runs on core0 via the pico-keys-sdk ENABLE_LVGL_UI hook (platform_ui_task()
// is called from execute_tasks() every main-loop iteration), so it must never
// block for long: a full redraw is ~10 ms and only happens on state changes.
//
// Copyright (c) 2026 Alexis Lowe. AGPL-3.0, as part of this pico-openpgp fork.
#include <stdio.h>
#include <string.h>

#include "apdu.h"
#include "board_tufty.h"
#include "button.h"
#include "files.h"
#include "fonts/font_12x24.h"
#include "fonts/font_16x32.h"
#include "fonts/font_8x16.h"
#include "gfx.h"
#include "led/led.h"
#include "picokeys.h"
#include "pico_time.h"
#include "signal.h"
#include "st7789.h"
#include "tusb.h"
#include "usb.h"
#include "version.h"

#ifndef TUFTY_BUILD_ID
#define TUFTY_BUILD_ID "dev"
#endif

// ---- tunables --------------------------------------------------------------

#define BACKLIGHT_ON        230
#define BACKLIGHT_DIM       60
#define BADGE_DIM_AFTER_MS  (10 * 60 * 1000)   // badge stays readable, just dimmer
#define CARD_DIM_AFTER_MS   (2 * 60 * 1000)
#define TOAST_MS            1500
#define SNAPSHOT_IDLE_MS    1500                // card quiet this long -> re-read DOs
#define MOUNT_SETTLE_MS     300                 // debounce mount/unmount flapping

// ---- card data snapshot ----------------------------------------------------

typedef struct {
    char name[40];          // display form ("First Last")
    char url[200];          // DO 5F50, may be empty
    uint8_t fp[3][20];      // SIG, DEC, AUT
    bool has_fp[3];
    uint8_t pw_retries[3];  // PW1, RC, PW3
    uint32_t sig_count;
    char id_fpr[41];        // identity (primary key) fingerprint shown on the badge, hex
    uint8_t id_src;         // ID_* below
} snapshot_t;

// Where the badge's identity fingerprint came from. The card only stores the
// fingerprints of the keys *on* it (usually subkeys), not the primary key's, so
// prefer an explicit one: private DO 0101 (gpg-card: privatedo 1), then a
// fingerprint inside the URL DO (https://keyoxide.org/<FPR>), and only then
// fall back to the signing key's own fingerprint.
enum { ID_NONE, ID_PRIVDO, ID_URL, ID_SIGKEY };

static snapshot_t snap;

static size_t read_do(uint16_t fid, uint8_t *out, size_t max) {
    file_t *ef = file_search_by_fid(fid, NULL, SPECIFY_EF);
    if (!ef || !file_has_data(ef)) {
        return 0;
    }
    size_t n = file_get_size(ef);
    if (n > max) {
        n = max;
    }
    memcpy(out, file_get_data(ef), n);
    return n;
}

// OpenPGP card names are "Surname<<Given<Names"; turn that into "Given Names Surname".
static void format_name(const char *raw, char *out, size_t max) {
    const char *sep = strstr(raw, "<<");
    char tmp[40];
    if (sep) {
        snprintf(tmp, sizeof(tmp), "%s %.*s", sep + 2, (int)(sep - raw), raw);
    }
    else {
        snprintf(tmp, sizeof(tmp), "%s", raw);
    }
    size_t j = 0;
    for (size_t i = 0; tmp[i] && j + 1 < max; i++) {
        char c = tmp[i] == '<' ? ' ' : tmp[i];
        if (c == ' ' && (j == 0 || out[j - 1] == ' ')) {
            continue;   // collapse/trim spaces
        }
        out[j++] = c;
    }
    while (j > 0 && out[j - 1] == ' ') {
        j--;
    }
    out[j] = 0;
}

static bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Finds a v4 fingerprint (a run of exactly 40 hex digits; spaces between groups
// allowed) in `s` and writes it upper-cased to `out`.
static bool find_fpr(const char *s, size_t len, char out[41]) {
    size_t n = 0;
    for (size_t i = 0; i <= len; i++) {
        char c = i < len ? s[i] : 0;
        if (c == ' ' && n > 0) {
            continue;
        }
        if (c && is_hex(c)) {
            if (n < 40) {
                out[n] = (c >= 'a' && c <= 'f') ? (char)(c - 32) : c;
            }
            n++;
            continue;
        }
        if (n == 40) {
            out[40] = 0;
            return true;
        }
        n = 0;
    }
    return false;
}

static void fp_hex(const uint8_t *fp, char *out /* >= 41 */);

// Only call while core1 is not processing an APDU (see platform_ui_task()).
static bool snapshot_refresh(void) {
    snapshot_t s;
    memset(&s, 0, sizeof(s));

    char raw[40] = { 0 };
    read_do(EF_CH_NAME, (uint8_t *)raw, sizeof(raw) - 1);
    format_name(raw, s.name, sizeof(s.name));

    read_do(EF_URI_URL, (uint8_t *)s.url, sizeof(s.url) - 1);

    static const uint16_t fp_fids[3] = { EF_FP_SIG, EF_FP_DEC, EF_FP_AUT };
    for (int i = 0; i < 3; i++) {
        if (read_do(fp_fids[i], s.fp[i], 20) == 20) {
            for (int k = 0; k < 20; k++) {
                if (s.fp[i][k]) {
                    s.has_fp[i] = true;
                    break;
                }
            }
        }
    }

    uint8_t pw[7] = { 0 };
    if (read_do(EF_PW_PRIV, pw, sizeof(pw)) == sizeof(pw)) {
        memcpy(s.pw_retries, pw + 4, 3);
    }

    uint8_t cnt[3] = { 0 };
    if (read_do(EF_SIG_COUNT, cnt, sizeof(cnt)) == sizeof(cnt)) {
        s.sig_count = ((uint32_t)cnt[0] << 16) | ((uint32_t)cnt[1] << 8) | cnt[2];
    }

    char priv[128];
    size_t priv_len = read_do(EF_PRIV_DO_1, (uint8_t *)priv, sizeof(priv));
    if (find_fpr(priv, priv_len, s.id_fpr)) {
        s.id_src = ID_PRIVDO;
    }
    else if (find_fpr(s.url, strlen(s.url), s.id_fpr)) {
        s.id_src = ID_URL;
    }
    else if (s.has_fp[0]) {
        fp_hex(s.fp[0], s.id_fpr);
        s.id_src = ID_SIGKEY;
    }

    bool changed = memcmp(&s, &snap, sizeof(s)) != 0;
    snap = s;
    return changed;
}

static void fp_hex(const uint8_t *fp, char *out /* >= 41 */) {
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 20; i++) {
        out[i * 2] = hex[fp[i] >> 4];
        out[i * 2 + 1] = hex[fp[i] & 15];
    }
    out[40] = 0;
}

// Copies `groups` 4-hex groups starting at group `first`, space separated.
static void fp_groups(const char *hex40, int first, int groups, char *out) {
    char *p = out;
    for (int g = first; g < first + groups && g < 10; g++) {
        if (p != out) {
            *p++ = ' ';
        }
        memcpy(p, hex40 + g * 4, 4);
        p += 4;
    }
    *p = 0;
}

// ---- state -----------------------------------------------------------------

typedef enum { SCREEN_BADGE, SCREEN_CARD, SCREEN_CONFIRM } screen_t;
typedef enum { BADGE_NAME_QR, BADGE_QR, BADGE_NAME, BADGE_VIEWS } badge_view_t;
typedef enum { OP_NONE, OP_SIGN, OP_DECRYPT, OP_AUTH, OP_KEYGEN, OP_VERIFY, OP_OTHER } op_t;

static screen_t screen = SCREEN_BADGE;
static badge_view_t badge_view = BADGE_NAME_QR;
static bool qr_use_fingerprint = false;
static bool dirty = true;

static op_t confirm_op = OP_NONE;
static uint32_t confirm_started_ms, confirm_timeout_ms;
static int confirm_bar_drawn = -1;

static uint32_t info_until_ms;   // firmware info overlay (button A outside confirm)
static const char *toast_text;
static uint8_t toast_color;
static uint32_t toast_until_ms;

static op_t last_op = OP_NONE;
static uint32_t last_op_ms;
static bool card_busy;
static uint32_t busy_since_ms;
static uint32_t card_idle_since_ms;
static bool snapshot_stale;

static uint32_t last_input_ms;
static bool dimmed;

static op_t current_op(void) {
    // core1 is parked inside the command while it waits for the button, and in
    // any case a torn read here only mislabels the screen.
    const uint8_t *h = apdu.header;
    if (!h) {
        return OP_NONE;
    }
    switch (h[1]) {
        case 0x2A:   // PSO
            if (h[2] == 0x9E && h[3] == 0x9A) {
                return OP_SIGN;
            }
            if (h[2] == 0x80 && h[3] == 0x86) {
                return OP_DECRYPT;
            }
            return OP_OTHER;
        case 0x88: return OP_AUTH;
        case 0x47: return OP_KEYGEN;
        case 0x20: return OP_VERIFY;
        default:   return OP_OTHER;
    }
}

static const char *op_verb(op_t op) {
    switch (op) {
        case OP_SIGN:    return "SIGN";
        case OP_DECRYPT: return "DECRYPT";
        case OP_AUTH:    return "AUTH";
        case OP_KEYGEN:  return "KEYGEN";
        default:         return "USE KEY";
    }
}

static const char *op_past(op_t op) {
    switch (op) {
        case OP_SIGN:    return "Signed";
        case OP_DECRYPT: return "Decrypted";
        case OP_AUTH:    return "Authenticated";
        case OP_KEYGEN:  return "Key generated";
        case OP_VERIFY:  return "PIN verified";
        default:         return "";
    }
}

static const char *op_progress(op_t op) {
    switch (op) {
        case OP_SIGN:    return "Signing";
        case OP_DECRYPT: return "Decrypting";
        case OP_AUTH:    return "Authenticating";
        case OP_KEYGEN:  return "Generating key";
        case OP_VERIFY:  return "Checking PIN";
        default:         return "";
    }
}

static void toast(const char *text, uint8_t color) {
    toast_text = text;
    toast_color = color;
    toast_until_ms = board_millis() + TOAST_MS;
    dirty = true;
}

static int ui_signal(signal_code_t code, void *data) {
    switch (code) {
        case SIGNAL_USER_PRESENCE_REQUEST: {
            const signal_user_presence_request_data_t *d = data;
            confirm_op = current_op();
            confirm_started_ms = board_millis();
            confirm_timeout_ms = (d && d->timeout ? d->timeout : 15) * 1000;
            confirm_bar_drawn = -1;
            break;
        }
        case SIGNAL_USER_PRESENCE_COMPLETED:
            if (confirm_op != OP_NONE) {
                toast("Approved", C_GREEN);
            }
            confirm_op = OP_NONE;
            break;
        case SIGNAL_USER_PRESENCE_CANCELLED:
            toast("Denied", C_RED);
            confirm_op = OP_NONE;
            break;
        case SIGNAL_USER_PRESENCE_TIMEOUT:
            toast("Timed out", C_RED);
            confirm_op = OP_NONE;
            break;
        default:
            break;
    }
    dirty = true;
    return 0;
}

// ---- drawing ---------------------------------------------------------------

static const gfx_font_t *fit_font(const char *s, int max_w) {
    if (gfx_text_width(&font_16x32, 1, s) <= max_w) {
        return &font_16x32;
    }
    if (gfx_text_width(&font_12x24, 1, s) <= max_w) {
        return &font_12x24;
    }
    return &font_8x16;
}

static const char *qr_payload(char *buf, size_t max) {
    if (snap.url[0] && !qr_use_fingerprint) {
        return snap.url;
    }
    if (snap.id_src != ID_NONE) {
        snprintf(buf, max, "openpgp4fpr:%s", snap.id_fpr);
        return buf;
    }
    return snap.url[0] ? snap.url : NULL;
}

static void draw_unconfigured(void) {
    gfx_text_centered(&font_16x32, 40, 1, C_ACCENT, "Tufty OpenPGP");
    gfx_text_centered(&font_8x16, 100, 1, C_WHITE, "Plug me in and run:");
    gfx_text_centered(&font_8x16, 124, 1, C_YELLOW, "gpg --edit-card");
    gfx_text_centered(&font_8x16, 148, 1, C_GREY, "admin > name / url / generate");
    gfx_text_centered(&font_8x16, 200, 1, C_DARKGREY, "pico-openpgp on Tufty 2350");
}

static void draw_badge(void) {
    char qrbuf[64];
    const char *payload = qr_payload(qrbuf, sizeof(qrbuf));
    const char *name = snap.name[0] ? snap.name : NULL;
    if (!payload && !name) {
        draw_unconfigured();
        return;
    }
    const char *hex = snap.id_fpr;

    badge_view_t view = badge_view;
    if (!payload && view != BADGE_NAME) {
        view = BADGE_NAME;
    }
    if (!name && view == BADGE_NAME) {
        view = BADGE_QR;
    }

    if (view == BADGE_QR) {
        gfx_qr(payload, GFX_WIDTH / 2, GFX_HEIGHT / 2, GFX_HEIGHT - 4);
        return;
    }

    if (view == BADGE_NAME) {
        // Big name, split on the last space onto two lines if it helps.
        const char *sp = strrchr(name, ' ');
        if (gfx_text_width(&font_16x32, 2, name) <= GFX_WIDTH - 8 || !sp) {
            const gfx_font_t *f = gfx_text_width(&font_16x32, 2, name) <= GFX_WIDTH - 8 ? &font_16x32 : fit_font(name, GFX_WIDTH - 8);
            int scale = f == &font_16x32 && gfx_text_width(f, 2, name) <= GFX_WIDTH - 8 ? 2 : 1;
            gfx_text_centered(f, 60, scale, C_WHITE, name);
        }
        else {
            char first[40];
            snprintf(first, sizeof(first), "%.*s", (int)(sp - name), name);
            const char *last = sp + 1;
            int s1 = gfx_text_width(&font_16x32, 2, first) <= GFX_WIDTH - 8 ? 2 : 1;
            int s2 = gfx_text_width(&font_16x32, 2, last) <= GFX_WIDTH - 8 ? 2 : 1;
            gfx_text_centered(fit_font(first, GFX_WIDTH - 8), 24, s1, C_WHITE, first);
            gfx_text_centered(fit_font(last, GFX_WIDTH - 8), 24 + 72, s2, C_WHITE, last);
        }
        gfx_rect(40, 172, GFX_WIDTH - 80, 2, C_ACCENT);
        if (hex[0]) {
            char line[32];
            fp_groups(hex, 0, 5, line);
            gfx_text_centered(&font_12x24, 182, 1, C_ACCENT, line);
            fp_groups(hex, 5, 5, line);
            gfx_text_centered(&font_12x24, 208, 1, C_ACCENT, line);
        }
        return;
    }

    // BADGE_NAME_QR: name across the top, QR left, fingerprint right.
    int qr_top = 4;
    if (name) {
        const gfx_font_t *f = fit_font(name, GFX_WIDTH - 8);
        gfx_text_centered(f, (40 - f->height) / 2 + 2, 1, C_WHITE, name);
        gfx_rect(0, 42, GFX_WIDTH, 2, C_ACCENT);
        qr_top = 48;
    }
    int qr_size = GFX_HEIGHT - qr_top - 4;
    gfx_qr(payload, 4 + qr_size / 2, qr_top + qr_size / 2, qr_size);

    int col_x = qr_size + 16;
    // Only claim "OpenPGP" identity when we know the primary fingerprint.
    gfx_text(&font_12x24, col_x, qr_top + 2, 1, C_ACCENT, snap.id_src == ID_SIGKEY ? "Sig. key" : "OpenPGP");
    if (hex[0]) {
        for (int g = 0; g < 5; g++) {
            char line[12];
            fp_groups(hex, g * 2, 2, line);
            gfx_text(&font_12x24, col_x, qr_top + 34 + g * 26, 1, C_WHITE, line);
        }
    }
    const char *hint = (snap.url[0] && !qr_use_fingerprint) ? "scan: keyoxide" : "scan: fingerprint";
    if (!strstr(snap.url, "keyoxide") && snap.url[0] && !qr_use_fingerprint) {
        hint = "scan: profile";
    }
    gfx_text(&font_8x16, col_x, GFX_HEIGHT - 20, 1, C_GREY, hint);
}

static void draw_card(uint32_t now) {
    gfx_rect(0, 0, GFX_WIDTH, 36, C_ACCENT_DIM);
    gfx_text(&font_16x32, 8, 2, 1, C_WHITE, "OpenPGP card");
    // Activity dot: yellow while an APDU is in flight.
    gfx_rect(GFX_WIDTH - 28, 10, 16, 16, card_busy ? C_YELLOW : C_GREEN);

    int y = 44;
    if (snap.name[0]) {
        gfx_text(&font_12x24, 8, y, 1, C_GREY, snap.name);
        y += 30;
    }

    static const char *labels[3] = { "SIG", "DEC", "AUT" };
    for (int i = 0; i < 3; i++) {
        gfx_text(&font_12x24, 8, y, 1, C_ACCENT, labels[i]);
        if (snap.has_fp[i]) {
            char hex[41], line[24];
            fp_hex(snap.fp[i], hex);
            fp_groups(hex, 6, 4, line);   // long key ID (last 16 hex)
            gfx_text(&font_12x24, 56, y, 1, C_WHITE, line);
        }
        else {
            gfx_text(&font_12x24, 56, y, 1, C_DARKGREY, "(no key)");
        }
        y += 28;
    }

    char line[48];
    snprintf(line, sizeof(line), "PIN %u  Reset %u  Admin %u", snap.pw_retries[0], snap.pw_retries[1], snap.pw_retries[2]);
    gfx_text(&font_8x16, 8, y + 4, 1, snap.pw_retries[0] == 0 || snap.pw_retries[2] == 0 ? C_RED : C_GREY, line);
    snprintf(line, sizeof(line), "Signatures: %lu", (unsigned long)snap.sig_count);
    gfx_text(&font_8x16, 8, y + 24, 1, C_GREY, line);

    if (card_busy && last_op != OP_NONE && op_progress(last_op)[0]) {
        // Long operations (RSA keygen can take minutes): show elapsed time so it
        // is obvious the card is alive.
        uint32_t s = (now - busy_since_ms) / 1000;
        snprintf(line, sizeof(line), "%s... %lu:%02lu", op_progress(last_op), (unsigned long)(s / 60), (unsigned long)(s % 60));
        gfx_text(&font_8x16, 8, GFX_HEIGHT - 20, 1, C_YELLOW, line);
    }
    else if (last_op != OP_NONE && op_past(last_op)[0]) {
        uint32_t ago = (now - last_op_ms) / 1000;
        if (ago < 60) {
            snprintf(line, sizeof(line), "%s %lus ago", op_past(last_op), (unsigned long)ago);
        }
        else {
            snprintf(line, sizeof(line), "%s %lum ago", op_past(last_op), (unsigned long)(ago / 60));
        }
        gfx_text(&font_8x16, 8, GFX_HEIGHT - 20, 1, C_WHITE, line);
    }
}

static int confirm_bar_width(uint32_t now) {
    uint32_t elapsed = now - confirm_started_ms;
    if (elapsed >= confirm_timeout_ms) {
        return 0;
    }
    return (int)((uint64_t)(GFX_WIDTH - 40) * (confirm_timeout_ms - elapsed) / confirm_timeout_ms);
}

static void draw_confirm(uint32_t now) {
    gfx_frame(4, 4, GFX_WIDTH - 8, GFX_HEIGHT - 8, 4, C_YELLOW);
    gfx_text_centered(&font_12x24, 20, 1, C_WHITE, "Confirm");
    char verb[16];
    snprintf(verb, sizeof(verb), "%s?", op_verb(confirm_op));
    gfx_text_centered(&font_16x32, 56, 2, C_YELLOW, verb);
    gfx_text(&font_12x24, 24, 140, 1, C_GREEN, "A");
    gfx_text(&font_8x16, 44, 144, 1, C_WHITE, "approve");
    gfx_text(&font_12x24, 180, 140, 1, C_RED, "B");
    gfx_text(&font_8x16, 200, 144, 1, C_WHITE, "deny");
    gfx_frame(18, 196, GFX_WIDTH - 36, 20, 2, C_GREY);
    int w = confirm_bar_width(now);
    gfx_rect(20, 198, w, 16, C_YELLOW);
    confirm_bar_drawn = w / 8;
}

static void draw_toast(void) {
    int w = gfx_text_width(&font_16x32, 1, toast_text) + 32;
    int x = (GFX_WIDTH - w) / 2, y = GFX_HEIGHT / 2 - 28;
    gfx_rect(x, y, w, 56, C_BLACK);
    gfx_frame(x, y, w, 56, 3, toast_color);
    gfx_text_centered(&font_16x32, y + 12, 1, toast_color, toast_text);
}

static void draw_info(void) {
    int x = 12, y = 50, w = GFX_WIDTH - 24, h = 130;
    char line[48];
    gfx_rect(x, y, w, h, C_BLACK);
    gfx_frame(x, y, w, h, 3, C_ACCENT);
    gfx_text_centered(&font_12x24, y + 10, 1, C_ACCENT, "Firmware");
    gfx_text_centered(&font_8x16, y + 46, 1, C_WHITE, TUFTY_BUILD_ID);
    snprintf(line, sizeof(line), "pico-openpgp %d.%d", PIPGP_VERSION_MAJOR, PIPGP_VERSION_MINOR);
    gfx_text_centered(&font_8x16, y + 76, 1, C_GREY, line);
    snprintf(line, sizeof(line), "OpenPGP card spec %d.%d", OPGP_VERSION_MAJOR, OPGP_VERSION_MINOR);
    gfx_text_centered(&font_8x16, y + 96, 1, C_GREY, line);
}

static void render(uint32_t now) {
    gfx_clear(C_BLACK);
    switch (screen) {
        case SCREEN_BADGE:   draw_badge(); break;
        case SCREEN_CARD:    draw_card(now); break;
        case SCREEN_CONFIRM: draw_confirm(now); break;
    }
    if (toast_text && now < toast_until_ms) {
        draw_toast();
    }
    if (screen != SCREEN_CONFIRM && now < info_until_ms) {
        draw_info();
    }
    st7789_update();
}

// ---- main task -------------------------------------------------------------

static void set_backlight_for_activity(uint32_t now) {
    uint32_t dim_after = screen == SCREEN_BADGE ? BADGE_DIM_AFTER_MS : CARD_DIM_AFTER_MS;
    bool want_dim = screen != SCREEN_CONFIRM && now - last_input_ms > dim_after;
    if (want_dim != dimmed) {
        dimmed = want_dim;
        st7789_set_backlight(dimmed ? BACKLIGHT_DIM : BACKLIGHT_ON);
    }
}

// Bring the panel up before main(): st7789_init() sleeps ~250 ms, which would
// otherwise stall USB enumeration on the first platform_ui_task() call.
static void __attribute__((constructor(110))) ui_early_init(void) {
    board_tufty_init();
    st7789_init();
    gfx_clear(C_BLACK);
    gfx_text_centered(&font_16x32, 90, 1, C_ACCENT, "Tufty OpenPGP");
    gfx_text_centered(&font_8x16, 130, 1, C_GREY, "starting...");
    gfx_text_centered(&font_8x16, GFX_HEIGHT - 20, 1, C_DARKGREY, TUFTY_BUILD_ID);
    st7789_update();
    st7789_set_backlight(BACKLIGHT_ON);
}

void platform_ui_task(void);
void platform_ui_task(void) {
    static bool initialised = false;
    static uint32_t prev_buttons;
    static bool mounted_raw, mounted;
    static uint32_t mounted_changed_ms;
    static uint32_t last_tick_ms;
    uint32_t now = board_millis();

    if (!initialised) {
        board_tufty_late_init();
        signal_add(SIGNAL_USER_PRESENCE_REQUEST, SIGNAL_FLAG_NONE, ui_signal);
        signal_add(SIGNAL_USER_PRESENCE_COMPLETED, SIGNAL_FLAG_NONE, ui_signal);
        signal_add(SIGNAL_USER_PRESENCE_CANCELLED, SIGNAL_FLAG_NONE, ui_signal);
        signal_add(SIGNAL_USER_PRESENCE_TIMEOUT, SIGNAL_FLAG_NONE, ui_signal);
        snapshot_refresh();   // nothing is talking to the card yet
        last_input_ms = now;
        initialised = true;
        render(now);
        dirty = false;
        return;
    }

    // Throttle to ~100 Hz; the rest of the loop is USB/flash work.
    if (now - last_tick_ms < 10) {
        return;
    }
    last_tick_ms = now;

    // Buttons (edge-triggered). A is also read by the SDK for user presence.
    uint32_t buttons = board_buttons();
    uint32_t pressed = buttons & ~prev_buttons;
    prev_buttons = buttons;
    if (pressed) {
        last_input_ms = now;
        if (dimmed) {
            set_backlight_for_activity(now);
            pressed = 0;   // first press only wakes the screen
        }
    }
    if (screen == SCREEN_CONFIRM) {
        if (pressed & BTN_B) {
            cancel_button = true;   // consumed by button_wait_poll() on core0
        }
    }
    else if (pressed & BTN_A) {
        // Outside a confirmation A is free: toggle the firmware info overlay.
        info_until_ms = now < info_until_ms ? now : now + 5000;
        dirty = true;
    }
    else if (screen == SCREEN_BADGE) {
        if (pressed & (BTN_B | BTN_DOWN)) {
            badge_view = (badge_view + 1) % BADGE_VIEWS;
            dirty = true;
        }
        if (pressed & BTN_UP) {
            badge_view = (badge_view + BADGE_VIEWS - 1) % BADGE_VIEWS;
            dirty = true;
        }
        if (pressed & BTN_C) {
            qr_use_fingerprint = !qr_use_fingerprint;
            dirty = true;
        }
    }

    // USB host presence, debounced. A power bank gives VBUS but never mounts,
    // so it correctly stays in badge mode.
    bool m = tud_mounted() && !tud_suspended();
    if (m != mounted_raw) {
        mounted_raw = m;
        mounted_changed_ms = now;
    }
    if (mounted != mounted_raw && now - mounted_changed_ms > MOUNT_SETTLE_MS) {
        mounted = mounted_raw;
        if (!mounted) {
            snapshot_stale = true;   // pick up anything changed while plugged in
        }
        last_input_ms = now;
        dirty = true;
    }

    // Track card activity from the SDK's LED mode and the in-flight APDU.
    bool busy = led_get_mode() == MODE_PROCESSING || is_busy();
    if (busy) {
        op_t op = current_op();
        if (op != OP_NONE && op != OP_OTHER) {
            if (op != last_op) {
                dirty = true;
            }
            last_op = op;
            last_op_ms = now;
        }
        snapshot_stale = true;
        card_idle_since_ms = now;
        last_input_ms = now;
    }
    if (busy != card_busy) {
        card_busy = busy;
        if (busy) {
            busy_since_ms = now;
        }
        if (screen == SCREEN_CARD) {
            dirty = true;
        }
    }
    // Re-read the DOs only once the card has been quiet for a while: core1 owns
    // the file system while it processes a command.
    if (snapshot_stale && !busy && !is_req_button_pending() && now - card_idle_since_ms > SNAPSHOT_IDLE_MS) {
        snapshot_stale = false;
        if (snapshot_refresh()) {
            dirty = true;
        }
    }

    screen_t want = is_req_button_pending() ? SCREEN_CONFIRM : mounted ? SCREEN_CARD : SCREEN_BADGE;
    if (want != screen) {
        screen = want;
        if (screen == SCREEN_CONFIRM) {
            last_input_ms = now;
        }
        dirty = true;
    }

    if (screen == SCREEN_CONFIRM) {
        board_case_leds(((now - confirm_started_ms) / 250) & 1);
        if (confirm_bar_width(now) / 8 != confirm_bar_drawn) {
            dirty = true;
        }
    }
    else {
        board_case_leds(card_busy);
    }

    // Toast expiry and the "Ns ago" clock.
    static uint32_t toast_drawn_until;
    if (toast_text && now >= toast_until_ms && toast_drawn_until != toast_until_ms) {
        toast_drawn_until = toast_until_ms;
        dirty = true;
    }
    static uint32_t info_drawn_until;
    if (info_until_ms && now >= info_until_ms && info_drawn_until != info_until_ms) {
        info_drawn_until = info_until_ms;
        dirty = true;
    }
    static uint32_t last_clock_redraw;
    if (screen == SCREEN_CARD && last_op != OP_NONE && now - last_clock_redraw > (card_busy ? 1000u : 5000u)) {
        last_clock_redraw = now;
        dirty = true;
    }

    set_backlight_for_activity(now);

    if (dirty) {
        dirty = false;
        render(now);
    }
}
