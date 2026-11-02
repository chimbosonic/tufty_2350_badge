// Copyright (c) 2026 Alexis Lowe. AGPL-3.0, as part of this pico-openpgp fork.
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    BTN_A    = 1 << 0,
    BTN_B    = 1 << 1,
    BTN_C    = 1 << 2,
    BTN_UP   = 1 << 3,
    BTN_DOWN = 1 << 4,
};

void board_tufty_init(void);        // GPIO setup, safe before main()
void board_tufty_late_init(void);   // after pico-keys-sdk phy_init()
uint32_t board_buttons(void);   // bitmask of BTN_* currently held
bool board_vbus_present(void);
void board_case_leds(bool on);
