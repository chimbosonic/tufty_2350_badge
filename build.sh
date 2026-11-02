#!/usr/bin/env bash
# Build pico-openpgp with the Tufty 2350 badge UI.
#
#   PICO_SDK_PATH=~/pico-sdk ./build.sh          # -> build/pico_openpgp.uf2
#
# Applies patches/*.patch to the pinned submodules (idempotent), then configures
# and builds. Requires cmake, arm-none-eabi-gcc and pico-sdk >= 2.1.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
OPENPGP="$ROOT/third_party/pico-openpgp"
BUILD="${BUILD_DIR:-$ROOT/build}"

: "${PICO_SDK_PATH:?Set PICO_SDK_PATH to your pico-sdk checkout}"

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
apply_patch "$OPENPGP" "$ROOT/patches/pico-openpgp.patch"

cmake -S "$OPENPGP" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DPICO_SDK_PATH="$PICO_SDK_PATH" \
    -DPICO_PLATFORM=rp2350 \
    -DPICO_BOARD=pimoroni_tufty2350 \
    -DPICO_BOARD_HEADER_DIRS="$ROOT/board" \
    -DPICO_OPENPGP_EXTRA_CMAKE="$ROOT/tufty/tufty.cmake"
cmake --build "$BUILD"

echo
echo "Built: $BUILD/pico_openpgp.uf2"
echo "Build ID: $(cat "$BUILD/tufty_build_id.txt")   (shown on the badge boot screen / press A)"
echo "Flash: hold HOME (BOOT) while pressing RESET, then copy the .uf2 to the RP2350 drive."
