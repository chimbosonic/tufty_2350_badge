#!/usr/bin/env bash
# Render the Tufty UI screens on the host: tools/preview/run.sh [outdir]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/build/preview}"
mkdir -p "$OUT"
cc -std=c11 -Wall -Wextra -Wno-unused-parameter -O1 \
    -I"$ROOT/tools/preview/stubs" -I"$ROOT/tufty" -I"$ROOT/tufty/third_party/qrcodegen" \
    "$ROOT/tools/preview/preview.c" "$ROOT/tufty/gfx.c" "$ROOT/tufty/third_party/qrcodegen/qrcodegen.c" \
    -o "$OUT/preview"
"$OUT/preview" "$OUT" | while read -r bmp; do
    if command -v sips >/dev/null; then
        sips -s format png "$bmp" --out "${bmp%.bmp}.png" >/dev/null && rm "$bmp"
    fi
done
ls "$OUT"
