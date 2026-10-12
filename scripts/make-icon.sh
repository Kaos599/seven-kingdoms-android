#!/usr/bin/env bash
# Generate the launcher icon from the game's own art:
#   third_party/7kaa/data/IMAGE/7K_ICON.BMP (32x32, the "7K" mark from the original game)
# -> android/app/src/main/res/mipmap-*/ic_launcher.png at the standard launcher sizes.
# Run once; the PNGs are committed (they are small). Re-run only if the source art changes.
# Requires macOS sips.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/third_party/7kaa/data/IMAGE/7K_ICON.BMP"
RES="$ROOT/android/app/src/main/res"

[ -f "$SRC" ] || { echo "make-icon: $SRC missing: run scripts/fetch-deps.sh" >&2; exit 1; }
command -v sips >/dev/null || { echo "make-icon: sips not found (macOS only)" >&2; exit 1; }

# density bucket : px
for pair in mdpi:48 hdpi:72 xhdpi:96 xxhdpi:144 xxxhdpi:192; do
  bucket="${pair%%:*}"
  px="${pair##*:}"
  dir="$RES/mipmap-$bucket"
  mkdir -p "$dir"
  # sips reads the BMP directly and writes a PNG at the requested square size.
  sips -s format png -z "$px" "$px" "$SRC" --out "$dir/ic_launcher.png" >/dev/null
  echo "make-icon: mipmap-$bucket/ic_launcher.png ${px}x${px}"
done
