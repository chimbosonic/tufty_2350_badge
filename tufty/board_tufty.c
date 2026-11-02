// Copyright (c) 2026 Alexis Lowe. AGPL-3.0, as part of this pico-openpgp fork.
#include "board_tufty.h"

#include "button.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/powman.h"
#include "hardware/resets.h"
#include "hardware/structs/usb.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "picokeys.h"
#include "pico/stdlib.h"

static const uint user_buttons[] = { TUFTY_SW_A, TUFTY_SW_B, TUFTY_SW_C, TUFTY_SW_UP, TUFTY_SW_DOWN };
static const uint case_leds[] = { TUFTY_LED_0, TUFTY_LED_1, TUFTY_LED_2, TUFTY_LED_3 };

#define TUFTY_RESET_SW      14   // RESET also reads as a GPIO (low while held)
#define TUFTY_PSRAM_CS      8
#define LONG_PRESS_STEP_MS  300  // one case LED lights per step; all four = power off

// POWER_EN switches the peripheral rail; the stock firmware drives it high at
// boot and so do we, before anything else runs (pico-sdk runs constructors
// after runtime init, before main()).
static void __attribute__((constructor(101))) board_tufty_power_latch(void) {
    gpio_init(TUFTY_POWER_EN);
    gpio_set_dir(TUFTY_POWER_EN, GPIO_OUT);
    gpio_put(TUFTY_POWER_EN, 1);
}

// ---- power off -------------------------------------------------------------
// Ported from pimoroni/tufty2350 modules/c/powman (MIT): park every GPIO in its
// lowest-power state, power down the USB PHY, and put the RP2350 into its
// deepest power state (only the always-on domain running). With
// wake_on_buttons, pressing any front button powers it back up (fresh boot);
// otherwise only RESET or plugging into USB and pressing RESET does.

static void __no_inline_not_in_flash_func(powman_enter_off)(powman_power_state off, powman_power_state on) {
    if (!powman_configure_wakeup_state(off, on)) {
        return;
    }
    // Boot normally on wake rather than resuming anywhere.
    powman_hw->boot[0] = 0;
    powman_hw->boot[1] = 0;
    powman_hw->boot[2] = 0;
    powman_hw->boot[3] = 0;
    if (powman_set_power_state(off) != PICO_OK) {
        return;
    }
    while (true) {
        __wfi();
    }
}

static void board_tufty_power_off(bool wake_on_buttons) {
    save_and_disable_interrupts();
    set_sys_clock_48mhz();

    for (uint i = 0; i < NUM_BANK0_GPIOS; i++) {
        gpio_set_function(i, GPIO_FUNC_SIO);
        gpio_set_dir(i, GPIO_IN);
        gpio_set_input_enabled(i, false);
        switch (i) {
            case TUFTY_PSRAM_CS:
                gpio_set_pulls(i, true, false);    // keep the PSRAM deselected
                break;
            case TUFTY_RESET_SW:
            case TUFTY_SW_HOME:
            case TUFTY_POWER_EN:
            case 40:                               // VBAT_SENSE
            case 42:                               // SENSE_1V1
                gpio_disable_pulls(i);
                break;
            case TUFTY_SW_A:
            case TUFTY_SW_B:
            case TUFTY_SW_C:
            case TUFTY_SW_UP:
            case TUFTY_SW_DOWN:
                gpio_set_pulls(i, true, false);    // buttons must stay pulled up
                break;
            default:
                gpio_set_pulls(i, false, true);    // incl. backlight -> off
        }
    }

    // Power down the USB PHY with pull-downs on D+/D-.
    reset_block_mask(RESETS_RESET_USBCTRL_BITS);
    unreset_block_mask_wait_blocking(RESETS_RESET_USBCTRL_BITS);
    usb_hw->muxing = USB_USB_MUXING_TO_PHY_BITS | USB_USB_MUXING_SOFTCON_BITS;
    usb_hw->main_ctrl = USB_MAIN_CTRL_CONTROLLER_EN_BITS;
    usb_hw->phy_direct = USB_USBPHY_DIRECT_TX_PD_BITS | USB_USBPHY_DIRECT_RX_PD_BITS |
                         USB_USBPHY_DIRECT_DM_PULLDN_EN_BITS | USB_USBPHY_DIRECT_DP_PULLDN_EN_BITS;
    usb_hw->phy_direct_override =
        USB_USBPHY_DIRECT_RX_DM_BITS | USB_USBPHY_DIRECT_RX_DP_BITS | USB_USBPHY_DIRECT_RX_DD_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_TX_DIFFMODE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DM_PULLUP_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_TX_FSSLEW_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_PD_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_RX_PD_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OE_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DM_PULLDN_EN_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_DP_PULLDN_EN_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DP_PULLUP_EN_OVERRIDE_EN_BITS |
        USB_USBPHY_DIRECT_OVERRIDE_DM_PULLUP_HISEL_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_DP_PULLUP_HISEL_OVERRIDE_EN_BITS;

    powman_timer_start();
    powman_set_debug_power_request_ignored(true);
    powman_disable_all_wakeups();

    if (wake_on_buttons) {
        // SW_INT goes low when any front button is pressed. Wait for the
        // buttons to be released first, or we would wake straight back up.
        gpio_set_input_enabled(TUFTY_SW_INT, true);
        gpio_set_pulls(TUFTY_SW_INT, true, false);
        for (int i = 0; i < 100 && !gpio_get(TUFTY_SW_INT); i++) {
            busy_wait_ms(10);
        }
        powman_enable_gpio_wakeup(0, TUFTY_SW_INT, true, false);
    }

    powman_power_state off = POWMAN_POWER_STATE_NONE;
    powman_power_state on = powman_power_state_with_domain_on(
        powman_power_state_with_domain_on(POWMAN_POWER_STATE_NONE, POWMAN_POWER_DOMAIN_SWITCHED_CORE),
        POWMAN_POWER_DOMAIN_XIP_CACHE);
    powman_enter_off(off, on);
    // Only reached if the power state change was refused.
    watchdog_reboot(0, 0, 0);
}

static void case_leds_set(unsigned lit) {
    // Same sweep order as the stock firmware: 1, 2, 3, 0.
    static const uint order[4] = { TUFTY_LED_1, TUFTY_LED_2, TUFTY_LED_3, TUFTY_LED_0 };
    for (unsigned i = 0; i < 4; i++) {
        gpio_put(order[i], i < lit);
    }
}

// Long-press RESET to power off, like the stock Tufty firmware. RESET restarts
// the chip; if it is still held when we boot, light one case LED per step and
// power off once all four are lit. Releasing early just boots normally.
// Holding UP+DOWN as well enters "shipping mode": buttons do not wake it.
static void __attribute__((constructor(102))) board_tufty_long_press_check(void) {
    if (!(powman_hw->chip_reset & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS) || watchdog_caused_reboot()) {
        return;   // not a RESET press (power-on, button wake, or software reboot)
    }
    gpio_init(TUFTY_RESET_SW);
    gpio_set_dir(TUFTY_RESET_SW, GPIO_IN);
    gpio_pull_up(TUFTY_RESET_SW);
    busy_wait_ms(1);
    if (gpio_get(TUFTY_RESET_SW)) {
        return;
    }
    for (unsigned i = 0; i < count_of(case_leds); i++) {
        gpio_init(case_leds[i]);
        gpio_set_dir(case_leds[i], GPIO_OUT);
    }
    for (unsigned step = 1; step <= 5; step++) {
        sleep_ms(LONG_PRESS_STEP_MS);
        if (gpio_get(TUFTY_RESET_SW)) {
            case_leds_set(0);   // released early: carry on booting
            return;
        }
        case_leds_set(step);
    }
    case_leds_set(0);
    gpio_init(TUFTY_SW_UP);
    gpio_init(TUFTY_SW_DOWN);
    gpio_pull_up(TUFTY_SW_UP);
    gpio_pull_up(TUFTY_SW_DOWN);
    busy_wait_ms(1);
    bool shipping = !gpio_get(TUFTY_SW_UP) && !gpio_get(TUFTY_SW_DOWN);
    board_tufty_power_off(!shipping);
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
