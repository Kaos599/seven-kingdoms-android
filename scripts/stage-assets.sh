#!/usr/bin/env bash
# Fill build/assets (the Gradle assets srcDir) with the game data, music, licences, manifest.txt and
# data_version.txt. Nothing is copied into the git tree: build/ is ignored.
#
# Layout (read by port/android/sk_android_data.cpp):
#   build/assets/manifest.txt         relative path of every file below gamedata/
#   build/assets/data_version.txt     stamp from port/data_version.txt
#   build/assets/gamedata/<path>      third_party/7kaa/data/** + MUSIC/*.WAV + licences
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TP="$ROOT/third_party"
OUT="$ROOT/build/assets"
DATA="$OUT/gamedata"

[ -f "$TP/7kaa/data/IMAGE/HALLFAME.ICN" ] || { echo "third_party/7kaa/data missing: run scripts/fetch-deps.sh" >&2; exit 1; }
[ -f "$TP/7kaa-music/MUSIC/NORMAN.WAV" ]   || { echo "third_party/7kaa-music missing: run scripts/fetch-deps.sh" >&2; exit 1; }
[ -f "$TP/7kaa-music/COPYING-Music.txt" ]  || { echo "COPYING-Music.txt missing from third_party/7kaa-music" >&2; exit 1; }

mkdir -p "$DATA"
rsync -rlt --delete --exclude MUSIC "$TP/7kaa/data/" "$DATA/"
# The game looks for music at <data>/MUSIC/NORMAN.WAV etc. (src/OSYS.cpp set_one_dir, OMUSIC.cpp).
mkdir -p "$DATA/MUSIC"
rsync -rlt --delete "$TP/7kaa-music/MUSIC/" "$DATA/MUSIC/"
# Licences (CONTRACT 4): GPL v2 for the game, and the music notice + readme, shipped unmodified.
cp "$TP/7kaa/COPYING" "$DATA/COPYING"
cp "$TP/7kaa-music/COPYING-Music.txt" "$DATA/COPYING-Music.txt"
cp "$TP/7kaa-music/README-Music.txt" "$DATA/README-Music.txt"

( cd "$DATA" && find . -type f | sed 's|^\./||' | LC_ALL=C sort ) > "$OUT/manifest.txt"
cp "$ROOT/port/data_version.txt" "$OUT/data_version.txt"

echo "stage-assets: $(wc -l < "$OUT/manifest.txt" | tr -d ' ') files, version $(cat "$OUT/data_version.txt")"
