#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ProjectHax LLC
#
# Render the app icon (resources/appicon/nutty.svg) into the committed PNG
# sizes and the multi-resolution Windows .ico. The macOS .icns is built from
# these PNGs by `iconutil` in the release workflow.
#
#   scripts/gen-icons.sh
#
# Needs rsvg-convert (librsvg), ImageMagick 7 (`magick`) and python3.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dir="$root/resources/appicon"
svg="$dir/nutty.svg"

command -v rsvg-convert >/dev/null 2>&1 || { echo "error: rsvg-convert not found" >&2; exit 1; }
command -v magick >/dev/null 2>&1 || { echo "error: magick (ImageMagick 7) not found" >&2; exit 1; }

mkdir -p "$dir/png"
# hicolor sizes (16-512) plus 1024 for the macOS iconset's 512@2x.
for size in 16 22 24 32 48 64 128 256 512 1024; do
    rsvg-convert -w "$size" -h "$size" "$svg" -o "$dir/png/nutty-$size.png"
done

# Windows picks the closest size per DPI; 20/40 cover 125 % small icons.
ico_parts=()
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
for size in 16 20 24 32 40 48 64 256; do
    rsvg-convert -w "$size" -h "$size" "$svg" -o "$tmp/$size.png"
    ico_parts+=("$tmp/$size.png")
done
magick "${ico_parts[@]:0:${#ico_parts[@]}-1}" "$tmp/small.ico"
# ImageMagick stores every entry as an uncompressed bitmap; swap in the PNG
# itself for 256 px (as Windows' own icons do), which keeps the .ico small.
python3 - "$tmp/small.ico" "$tmp/256.png" "$dir/nutty.ico" <<'PY'
import struct, sys
small, png, out = sys.argv[1:]
data = open(small, "rb").read()
count = struct.unpack_from("<HHH", data)[2]
images = []
for i in range(count):
    w, h, colors, _, planes, bpp, size, offset = struct.unpack_from("<BBBBHHII", data, 6 + 16 * i)
    images.append((w, h, colors, planes, bpp, data[offset:offset + size]))
images.append((0, 0, 0, 1, 32, open(png, "rb").read()))  # 0 means 256
offset = 6 + 16 * len(images)
blob = struct.pack("<HHH", 0, 1, len(images))
for w, h, colors, planes, bpp, img in images:
    blob += struct.pack("<BBBBHHII", w, h, colors, 0, planes, bpp, len(img), offset)
    offset += len(img)
blob += b"".join(img for *_, img in images)
open(out, "wb").write(blob)
PY


echo "wrote $dir/png/nutty-{16..1024}.png and $dir/nutty.ico"
