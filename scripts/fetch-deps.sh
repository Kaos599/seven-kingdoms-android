#!/usr/bin/env bash
# Download, verify and extract every component listed in third_party/VERSIONS.md.
# Idempotent: a valid archive is never re-downloaded, an extracted tree is never re-extracted.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TP="$ROOT/third_party"
DL="$TP/_downloads"
mkdir -p "$DL"

sha256_of() { shasum -a 256 "$1" | awk '{print $1}'; }

# name | archive file | url | sha256 | extracted dir | sentinel file proving a good extraction
COMPONENTS=(
"7kaa|7kaa-2.15.7.tar.gz|https://sourceforge.net/projects/skfans/files/7KAA%202.15.7/7kaa-2.15.7.tar.gz/download|2a9833ffe5fa7558857d4f8ba39cad1dccefadb01d298350ed8f954c75b6a6ae|7kaa|configure.ac"
"7kaa-music|7kaa-music-2.15.zip|https://www.7kfans.com/downloads/7kaa-music-2.15.zip|b6bdcd2ca4c7f78b45b831d70f30e3c472646e84dd8ca21e1f7128f80fcf0d32|7kaa-music|MUSIC/NORMAN.WAV"
"SDL2|SDL2-2.32.10.tar.gz|https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz|5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165|SDL2|CMakeLists.txt"
"enet|enet-1.3.18.tar.gz|https://github.com/lsalzman/enet/archive/refs/tags/v1.3.18.tar.gz|28603c895f9ed24a846478180ee72c7376b39b4bb1287b73877e5eae7d96b0dd|enet|CMakeLists.txt"
"openal-soft|openal-soft-1.25.2.tar.gz|https://github.com/kcat/openal-soft/archive/refs/tags/1.25.2.tar.gz|fb27e5839aa11f0e5b9d33756965291fad5d6909ab928ea1f796f4a1a6877894|openal-soft|CMakeLists.txt"
)

extract() {
  local archive="$1" dest="$2" name="$3" tmp
  tmp="$(mktemp -d "$DL/.extract-XXXXXX")"
  trap 'rm -rf "$tmp"' RETURN
  case "$archive" in
    *.tar.gz) tar -xzf "$archive" -C "$tmp" ;;
    *.zip)    unzip -q "$archive" -d "$tmp" ;;
    *) echo "unknown archive type: $archive" >&2; return 1 ;;
  esac
  # strip the single top-level folder (flatten)
  local top
  top="$(find "$tmp" -mindepth 1 -maxdepth 1)"
  if [ "$(echo "$top" | wc -l)" -ne 1 ] || [ ! -d "$top" ]; then
    echo "$name: expected exactly one top-level folder in archive" >&2; return 1
  fi
  rm -rf "$dest"
  mkdir -p "$(dirname "$dest")"
  mv "$top" "$dest"
}

for row in "${COMPONENTS[@]}"; do
  IFS='|' read -r name file url sha dir sentinel <<<"$row"
  archive="$DL/$file"
  dest="$TP/$dir"
  stamp="$DL/$file.extracted"

  # 1. archive present and valid?
  if [ -f "$archive" ] && [ "$(sha256_of "$archive")" = "$sha" ]; then
    echo "[ok]      $name: archive present, sha256 verified"
  else
    [ -f "$archive" ] && { echo "[warn]    $name: bad checksum, re-downloading"; rm -f "$archive"; }
    echo "[fetch]   $name: $url"
    curl -fL --retry 3 -o "$archive.part" "$url"
    got="$(sha256_of "$archive.part")"
    if [ "$got" != "$sha" ]; then
      rm -f "$archive.part"
      echo "$name: sha256 mismatch (expected $sha, got $got)" >&2
      exit 1
    fi
    mv "$archive.part" "$archive"
    rm -f "$stamp"
  fi

  # 2. extracted and matching this archive?
  if [ -e "$dest/$sentinel" ] && { [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$sha" ] || [ ! -f "$stamp" ]; }; then
    echo "$sha" > "$stamp"
    echo "[ok]      $name: already extracted"
  else
    echo "[extract] $name -> third_party/$dir"
    extract "$archive" "$dest" "$name"
    [ -e "$dest/$sentinel" ] || { echo "$name: $sentinel missing after extraction" >&2; exit 1; }
    echo "$sha" > "$stamp"
  fi
done
echo "fetch-deps: done"
