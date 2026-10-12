#!/usr/bin/env bash
# Create build/7kaa-src = pristine third_party/7kaa (src + include only) + patches/7kaa/*.patch (CONTRACT 3.1).
# third_party/ is never modified. Re-runnable: unchanged files keep their mtime so incremental builds stay fast.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UP="$ROOT/third_party/7kaa"
DST="$ROOT/build/7kaa-src"

[ -f "$UP/src/AM.cpp" ] || { echo "third_party/7kaa missing: run scripts/fetch-deps.sh" >&2; exit 1; }
mkdir -p "$DST"

# Start from pristine every time (checksums, so mtimes of untouched files are preserved).
rsync -rlc --delete "$UP/src/" "$DST/src/"
rsync -rlc --delete "$UP/include/" "$DST/include/"

shopt -s nullglob
patches=("$ROOT"/patches/7kaa/*.patch)
for p in ${patches[@]+"${patches[@]}"}; do
  echo "[patch] $(basename "$p")"
  patch -p1 --no-backup-if-mismatch -s -d "$DST" < "$p"
done
echo "apply-patches: ${#patches[@]} patch(es) applied to build/7kaa-src"
