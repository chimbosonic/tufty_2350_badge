/*
 * pico-sdk board header for the Pimoroni Tufty 2350 (RP2350B, 16MB flash, 8MB PSRAM).
 * Pin assignments from pimoroni/tufty2350 board/pins.csv (MIT).
 *
 * No PICO_DEFAULT_LED_PIN on purpose: pico-keys-sdk then uses its dummy LED
 * driver, and the status is shown on the display instead (tufty/ui.c).
 */
#ifndef _BOARDS_PIMORONI_TUFTY2350_H
#define _BOARDS_PIMORONI_TUFTY2350_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

#define PIMORONI_TUFTY2350

// RP2350B: 48 GPIOs (the LCD bus is on GPIO32..39)
#define PICO_RP2350A 0

#define TUFTY_LED_0         0
#define TUFTY_LED_1         1
#define TUFTY_LED_2         2
#define TUFTY_LED_3         3
#define TUFTY_SW_DOWN       6
#define TUFTY_SW_A          7
#define TUFTY_SW_B          9
#define TUFTY_SW_C          10
#define TUFTY_SW_UP         11
#define TUFTY_VBUS_DETECT   12
#define TUFTY_SW_INT        15
#define TUFTY_SW_HOME       22
#define TUFTY_POWER_EN      41

// No PICO_DEFAULT_UART: GPIO0/1 drive the case LEDs, not a debug UART.

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
