// Copyright (c) 2026 Alexis Lowe. AGPL-3.0, as part of this pico-openpgp fork.
#include "board_tufty.h"

#include "button.h"
#include "hardware/gpio.h"
#include "picokeys.h"

static const uint user_buttons[] = { TUFTY_SW_A, TUFTY_SW_B, TUFTY_SW_C, TUFTY_SW_UP, TUFTY_SW_DOWN };
static const uint case_leds[] = { TUFTY_LED_0, TUFTY_LED_1, TUFTY_LED_2, TUFTY_LED_3 };

// The Tufty's soft power switch drops the rail on battery unless POWER_EN is
// held high, so latch it before anything else runs (pico-sdk runs constructors
// after runtime init, before main()).
static void __attribute__((constructor(101))) board_tufty_power_latch(void) {
    gpio_init(TUFTY_POWER_EN);
    gpio_set_dir(TUFTY_POWER_EN, GPIO_OUT);
    gpio_put(TUFTY_POWER_EN, 1);
}

void board_tufty_init(void) {
    for (unsigned i = 0; i < count_of(user_buttons); i++) {
        gpio_init(user_buttons[i]);
        gpio_set_dir(user_buttons[i], GPIO_IN);
        gpio_pull_up(user_buttons[i]);
    }
    gpio_init(TUFTY_VBUS_DETECT);
    gpio_set_dir(TUFTY_VBUS_DETECT, GPIO_IN);
    gpio_disable_pulls(TUFTY_VBUS_DETECT);
    for (unsigned i = 0; i < count_of(case_leds); i++) {
        gpio_init(case_leds[i]);
        gpio_set_dir(case_leds[i], GPIO_OUT);
        gpio_put(case_leds[i], 0);
    }
}

void board_tufty_late_init(void) {
    // pico-keys-sdk treats a zero user-presence timeout as "no button fitted"
    // and auto-approves UIF requests. This board has one, so default to 15 s
    // unless the user has commissioned a different value (RAM only, not saved).
    if (!phy_data.up_btn_present) {
        phy_data.up_btn = 15;
        phy_data.up_btn_present = true;
    }
}

uint32_t board_buttons(void) {
    uint32_t mask = 0;
    for (unsigned i = 0; i < count_of(user_buttons); i++) {
        if (!gpio_get(user_buttons[i])) {   // active low
            mask |= 1u << i;
        }
    }
    return mask;
}

bool board_vbus_present(void) {
    return gpio_get(TUFTY_VBUS_DETECT);
}

void board_case_leds(bool on) {
    for (unsigned i = 0; i < count_of(case_leds); i++) {
        gpio_put(case_leds[i], on);
    }
}

// Button A is the user-presence ("touch") button for UIF confirmations.
// Overrides the weak BOOTSEL default in pico-keys-sdk/src/button.c.
bool picok_board_button_read_hook(void) {
    return !gpio_get(TUFTY_SW_A);
}
