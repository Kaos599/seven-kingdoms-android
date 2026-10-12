#!/usr/bin/env bash
# Create build/7kaa-src = pristine third_party/7kaa (src + include only), then apply, in order:
#   1. patches/hd/*.patch   (7kaa_HD cf1cedb, widescreen base; sorted)
#   2. patches/7kaa/*.patch (our Android patches; sorted)
# Every patch is git-style with -p1 (a/src/..., a/include/...) relative to build/7kaa-src.
# Any reject aborts the run; rejects are kept outside the tree and no .rej/.orig is left behind (CONTRACT 3.1).
# third_party/ is never modified. Re-runnable: unchanged files keep their mtime so incremental builds stay fast.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UP="$ROOT/third_party/7kaa"
DST="$ROOT/build/7kaa-src"

[ -f "$UP/src/AM.cpp" ] || { echo "third_party/7kaa missing: run scripts/fetch-deps.sh" >&2; exit 1; }
mkdir -p "$DST"

# Start from pristine every time (checksums, so mtimes of untouched files are preserved).
# --delete also removes any stale .rej/.orig left in the tree by an earlier run.
rsync -rlc --delete "$UP/src/" "$DST/src/"
rsync -rlc --delete "$UP/include/" "$DST/include/"

REJ_DIR="$(mktemp -d "${TMPDIR:-/tmp}/apply-patches-rej.XXXXXX")"
trap 'rm -rf "$REJ_DIR"' EXIT

shopt -s nullglob
total=0
for dir in hd 7kaa; do
  patches=("$ROOT"/patches/$dir/*.patch)
  for p in ${patches[@]+"${patches[@]}"}; do
    echo "[patch] $dir/$(basename "$p")"
    if ! patch -p1 --no-backup-if-mismatch -s -d "$DST" --reject-file="$REJ_DIR/$dir-$(basename "$p").rej" < "$p"; then
      echo "apply-patches: FAILED applying $dir/$(basename "$p") to build/7kaa-src" >&2
      find "$REJ_DIR" -name '*.rej' -exec sh -c 'echo "--- reject: $1" >&2; cat "$1" >&2' _ {} \;
      exit 1
    fi
    total=$((total + 1))
  done
done

leftovers="$(find "$DST" \( -name '*.rej' -o -name '*.orig' \) -print)"
if [ -n "$leftovers" ]; then
  echo "apply-patches: stray files in build/7kaa-src:" >&2; echo "$leftovers" >&2; exit 1
fi
echo "apply-patches: $total patch(es) applied to build/7kaa-src (patches/hd first, then patches/7kaa)"
