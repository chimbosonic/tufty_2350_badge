# Tufty 2350 OpenPGP badge

[pico-openpgp](https://github.com/polhenarejos/pico-openpgp) on a Pimoroni Tufty 2350, with a display UI:

- **Unplugged (battery):** a conference badge showing your name and a QR code for your
  Keyoxide profile (or `openpgp4fpr:` fingerprint) plus the fingerprint.
- **Plugged into a computer:** an OpenPGP smartcard (CCID) for gpg/ssh. The screen shows key IDs,
  PIN retry counters and recent activity. When a key has touch policy (UIF) enabled, the screen asks
  you to approve: **A** approves, **B** denies.

The badge reads its identity from the card's own data objects (cardholder name `5B`, URL `5F50`),
so you configure it with standard `gpg --edit-card` commands.

The card only stores fingerprints for the keys *on* it, usually subkeys, so the badge works out
your **primary (identity) key fingerprint** in this order:

1. Private data object 1, if it contains a fingerprint: `gpg-card` → `verify` → `privatedo 1 <FINGERPRINT>`
   (needs the user PIN).
2. A fingerprint inside the URL, e.g. `https://keyoxide.org/<FINGERPRINT>`.
3. Otherwise, the signing key's own fingerprint. The badge then labels it "Sig. key" rather than "OpenPGP".

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
which adds a CMake hook and a fix for PINs staying blocked after `factory-reset`. The build enables
EdDSA (Ed25519/Ed448), which upstream leaves off by default, so gpg's usual ed25519/cv25519 keys can be
generated on the card or moved to it. `build.sh` applies
`patches/pico-keys-sdk.patch` to the SDK submodule and builds `build/pico_openpgp.uf2`.

### Linux / Tails: Gnuk USB ID

By default the badge uses pico-openpgp's USB ID `2E8A:10FF`. Most Linux `pcscd` installs don't
recognise it, so there gpg needs a udev rule to reach the card. On Tails that rule is lost at every
reboot. Build the Gnuk variant instead:

```sh
USB_ID=gnuk PICO_SDK_PATH=~/pico-sdk ./build.sh   # -> build-gnuk/pico_openpgp.uf2
```

This variant enumerates as a Gnuk token (`234B:0000`), which stock libccid supports, so it works
without any setup. It still works on macOS. Its build ID ends in `-gnuk`, so pressing **A** on the
badge shows which variant it's running. Reflashing keeps the card's keys.

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

## Power

- **Power off:** hold **RESET**. The four case LEDs light one by one; keep holding until all four
  are lit (about 1.5 s), then let go. Releasing earlier just restarts the badge.
- **Power on:** press any front button (A, B, C, UP, DOWN), or RESET.
- **Shipping mode** (for storage/transport): hold **UP + DOWN** as well while long-pressing RESET.
  Front buttons then won't wake it; only RESET does.

This matches the stock Tufty firmware. The badge doesn't turn itself off: on battery it stays on
(the screen dims after 10 minutes) until you power it off.

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
