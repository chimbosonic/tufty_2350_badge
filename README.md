# Tufty 2350 OpenPGP badge

[pico-openpgp](https://github.com/polhenarejos/pico-openpgp) on a Pimoroni Tufty 2350, with a display UI:

- **Unplugged (battery):** a conference badge showing your name and a QR code for your
  Keyoxide profile (or `openpgp4fpr:` fingerprint) plus the fingerprint.
- **Plugged into a computer:** an OpenPGP smartcard (CCID) for gpg/ssh. The screen shows key IDs,
  PIN retry counters and recent activity. When a key has touch policy (UIF) enabled, the screen asks
  you to approve: **A** approves, **B** denies.

The badge reads its identity from the card's own data objects (cardholder name `5B`, URL `5F50`,
signing key fingerprint), so you configure it with standard `gpg --edit-card` commands.

> This **replaces** the stock Pimoroni MicroPython firmware. To go back, flash
> `tufty-vX.Y.Z-micropython-with-filesystem.uf2` from
> [pimoroni/tufty2350 releases](https://github.com/pimoroni/tufty2350/releases). Going back also
> erases the card's keys, so keep your primary key backed up offline and only put subkeys on the badge.

## Build

You need `cmake`, `ninja`, `arm-none-eabi-gcc` and a [pico-sdk](https://github.com/raspberrypi/pico-sdk)
checkout (2.1 or newer).

```sh
git clone --recursive <this repo> && cd tufty_2350_badge
PICO_SDK_PATH=~/pico-sdk ./build.sh
```

pico-openpgp comes from the [`tufty` branch of chimbosonic/pico-openpgp](https://github.com/chimbosonic/pico-openpgp/tree/tufty),
which adds a CMake hook and a fix for PINs staying blocked after `factory-reset`. `build.sh` applies
`patches/pico-keys-sdk.patch` to the SDK submodule and builds `build/pico_openpgp.uf2`.

## Flash

Hold **HOME** (the BOOT button) and press **RESET**. An `RP2350` drive appears; copy
`build/pico_openpgp.uf2` onto it.

## Set up

```sh
gpg --card-status                      # should show an OpenPGP card
gpg --edit-card
  admin
  name                                 # shown on the badge
  url                                  # e.g. https://keyoxide.org/<FINGERPRINT> -> QR code
  key-attr                             # pick ECC / Curve 25519 for snappy ops
  generate                             # or: keytocard from an existing key
  uif 1 on                             # require on-screen approval for signing (2 = decrypt, 3 = auth)
```

The default PINs are user `123456` and admin `12345678`. Change them with `passwd` in `gpg --edit-card`.

If macOS doesn't see the card, add `disable-ccid` (to use the PC/SC framework) or `pcsc-shared` to
`~/.gnupg/scdaemon.conf` and run `gpgconf --kill scdaemon`.

## Buttons

| Screen | A | B | C | UP / DOWN |
|---|---|---|---|---|
| Badge | n/a | next view | QR: URL ↔ fingerprint | previous / next view |
| Card | n/a | n/a | n/a | n/a |
| Confirm | approve | deny | n/a | n/a |

The first press after the screen dims only wakes it. Badge views: name + QR, QR only, big name + fingerprint.

## Layout

```
board/pimoroni_tufty2350.h    pico-sdk board header (RP2350B, pins)
tufty/                        display UI, compiled into pico_openpgp via tufty/tufty.cmake
  ui.c                        state machine and screens (core0, pico-keys-sdk platform_ui_task hook)
  st7789.c, gfx.c             parallel LCD driver (ported from pimoroni/tufty2350) and 4bpp drawing
  board_tufty.c               power latch, buttons, case LEDs, user-presence button = A
patches/                      minimal changes to pico-keys-sdk (pico-openpgp changes live in the fork)
tools/preview/run.sh          renders every screen to PNG on your computer (no hardware needed)
tools/bdf2c.py                regenerates tufty/fonts/*.h from the Spleen BDF fonts
```

## Licences

This firmware is a derivative of pico-openpgp and pico-keys-sdk and is distributed under **AGPL-3.0**.
Bundled components: the ST7789 driver is derived from pimoroni/tufty2350 (MIT), the QR encoder is
Nayuki QR-Code-generator (MIT), and the fonts are Spleen (BSD-2-Clause).
