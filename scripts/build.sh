#!/usr/bin/env bash
# Build the APK:  scripts/build.sh [debug|release] [--side-by-side]
#   --side-by-side  second app: application ID com.sevenkfans.sevenkingdoms.hd, label "Seven Kingdoms HD"
#                   (Gradle -PsideBySide). Default: com.sevenkfans.sevenkingdoms, label "Seven Kingdoms".
# Prerequisite (once): scripts/fetch-deps.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MODE="debug"
GRADLE_PROPS=()
for arg in "$@"; do
  case "$arg" in
    debug|release) MODE="$arg" ;;
    --side-by-side) GRADLE_PROPS+=(-PsideBySide) ;;
    *) echo "usage: $0 [debug|release] [--side-by-side]" >&2; exit 2 ;;
  esac
done

case "$MODE" in
  debug) TASK=assembleDebug; APK="$ROOT/android/app/build/outputs/apk/debug/app-debug.apk" ;;
  release) TASK=assembleRelease; APK="$ROOT/android/app/build/outputs/apk/release/app-release.apk" ;;
  *) echo "usage: $0 [debug|release] [--side-by-side]" >&2; exit 2 ;;
esac

# Release keystore (CONTRACT 3.7): lives outside the repo, referenced by absolute path.
#   ~/.android/7kaa-release.jks            PKCS12, RSA 4096, alias sevenkingdoms, 10000 days
#   ~/.android/7kaa-release.properties     storePassword / keyPassword / keyAlias (mode 600)
ensure_release_keystore() {
  local dir="$HOME/.android"
  local jks="$dir/7kaa-release.jks"
  local props="$dir/7kaa-release.properties"

  if [ -f "$jks" ] && [ -f "$props" ]; then
    for k in storePassword keyPassword keyAlias; do
      grep -q "^$k=." "$props" || { echo "build.sh: $k missing from $props" >&2; exit 1; }
    done
    return 0
  fi
  # Refuse to half-create: a new keystore next to an existing one (or a props file without its
  # keystore) would either overwrite the signing key or leave the APK unrecoverable.
  if [ -f "$jks" ] || [ -f "$props" ]; then
    echo "build.sh: only one of $jks / $props exists; fix or remove it by hand, not auto-created." >&2
    exit 1
  fi

  echo "build.sh: creating release keystore in $dir (back it up; losing it means no updates)"
  mkdir -p "$dir"
  chmod 700 "$dir"
  local pw alias="sevenkingdoms"
  pw="$(openssl rand -base64 24)"
  # -storepass:env keeps the password off the process argument list.
  KS_PW="$pw" keytool -genkeypair -noprompt \
    -keystore "$jks" -storetype PKCS12 -storepass:env KS_PW \
    -alias "$alias" -keyalg RSA -keysize 4096 -validity 10000 \
    -keypass:env KS_PW -dname "CN=Seven Kingdoms personal build, O=personal, C=IE"
  umask 077
  printf 'storePassword=%s\nkeyPassword=%s\nkeyAlias=%s\n' "$pw" "$pw" "$alias" > "$props"
  chmod 600 "$props" "$jks"
}

if [ "$MODE" = release ]; then
  ensure_release_keystore
fi

# Android SDK location (local.properties is git-ignored).
if [ ! -f "$ROOT/android/local.properties" ]; then
  SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Library/Android/sdk}}"
  [ -d "$SDK" ] || { echo "Android SDK not found; set ANDROID_HOME" >&2; exit 1; }
  echo "sdk.dir=$SDK" > "$ROOT/android/local.properties"
fi

"$ROOT/scripts/apply-patches.sh"
"$ROOT/scripts/stage-assets.sh"

cd "$ROOT/android"
./gradlew "$TASK" ${GRADLE_PROPS[@]+"${GRADLE_PROPS[@]}"}

echo
echo "APK: $APK"
