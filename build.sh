#!/usr/bin/env bash
# Build pico-openpgp with the Tufty 2350 badge UI.
#
#   PICO_SDK_PATH=~/pico-sdk ./build.sh                # -> build/pico_openpgp.uf2
#   USB_ID=gnuk PICO_SDK_PATH=~/pico-sdk ./build.sh    # -> build-gnuk/pico_openpgp.uf2
#
# USB_ID=gnuk makes the badge enumerate as a Gnuk token (234B:0000), which stock
# pcscd/libccid already supports, so it works on e.g. Tails without udev rules
# or scdaemon.conf changes. The default is pico-openpgp's own ID (2E8A:10FF).
#
# EdDSA (Ed25519/Ed448) is always enabled: upstream leaves it off by default, but
# without it gpg's usual ed25519 sign/auth keys can't be generated or moved to the
# card (keytocard fails with "Invalid value"). It pulls pico-keys' mbedtls fork.
#
# Applies patches/*.patch to the pinned submodules (idempotent), then configures
# and builds. Requires cmake, arm-none-eabi-gcc and pico-sdk >= 2.1.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
OPENPGP="$ROOT/third_party/pico-openpgp"
USB_ID="${USB_ID:-default}"
case "$USB_ID" in
    default) VIDPID=None; DEFAULT_BUILD="$ROOT/build" ;;
    gnuk)    VIDPID=Gnuk; DEFAULT_BUILD="$ROOT/build-gnuk" ;;
    *) echo "error: USB_ID must be 'default' or 'gnuk' (got '$USB_ID')" >&2; exit 1 ;;
esac
BUILD="${BUILD_DIR:-$DEFAULT_BUILD}"

: "${PICO_SDK_PATH:?Set PICO_SDK_PATH to your pico-sdk checkout}"

git -C "$ROOT" submodule sync --recursive >/dev/null
git -C "$ROOT" submodule update --init --recursive

apply_patch() {
    local repo="$1" patch="$2"
    if git -C "$repo" apply --check "$patch" 2>/dev/null; then
        git -C "$repo" apply "$patch"
        echo "applied $(basename "$patch")"
    elif git -C "$repo" apply --reverse --check "$patch" 2>/dev/null; then
        echo "already applied $(basename "$patch")"
    else
        echo "error: $(basename "$patch") does not apply to $repo" >&2
        exit 1
    fi
}
apply_patch "$OPENPGP/pico-keys-sdk" "$ROOT/patches/pico-keys-sdk.patch"

cmake -S "$OPENPGP" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DPICO_SDK_PATH="$PICO_SDK_PATH" \
    -DPICO_PLATFORM=rp2350 \
    -DPICO_BOARD=pimoroni_tufty2350 \
    -DPICO_BOARD_HEADER_DIRS="$ROOT/board" \
    -DPICO_OPENPGP_EXTRA_CMAKE="$ROOT/tufty/tufty.cmake" \
    -DVIDPID="$VIDPID" \
    -DENABLE_EDDSA=ON
cmake --build "$BUILD"

echo
echo "Built: $BUILD/pico_openpgp.uf2"
echo "Build ID: $(cat "$BUILD/tufty_build_id.txt")   (shown on the badge boot screen / press A)"
echo "USB ID:   $(cat "$BUILD/tufty_usb_id.txt")"
echo "Flash: hold HOME (BOOT) while pressing RESET, then copy the .uf2 to the RP2350 drive."
