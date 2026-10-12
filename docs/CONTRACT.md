# 7kaa-android — Build Contract

Native Android port of **Seven Kingdoms: Ancient Adversaries 2.15.7** (GPL v2) for personal play on an
arm64 Android phone. Built from the official GPL source, not by reverse engineering.

Roles: the orchestrator writes this contract and verifies each phase. Implementation agents write the code.
Every agent reads this file first and obeys it. If a clause blocks you, stop and report; do not work around it.

---

## 1. Scope

**In scope**
- arm64-v8a APK that boots, plays single-player games, saves/loads, and plays sound + music.
- Touch controls good enough to play a full game without a mouse or keyboard.
- App lifecycle behaviour expected of a normal Android game (section 5, phase 3).

**Out of scope (do not build)**
- Online/LAN multiplayer (upstream supports x86 only; ARM would desync). Compile enet in if the game needs it to link, but hide or leave untouched the multiplayer menu; do not try to fix it.
- libcurl / 7kfans web integration, gettext / translations (English only).
- Play Store publishing, ads, analytics, in-app anything.
- Other ABIs (armeabi-v7a, x86, x86_64). arm64-v8a only.
- Automated test suites. **No unit tests.** Verification is the manual checklist in section 6.

---

## 2. Fixed technical decisions (do not change without orchestrator approval)

| Item | Decision |
|---|---|
| Game source | `third_party/7kaa` = official 2.15.7 tarball, MD5 `435a063a8d84905b757be942d93232a5` |
| SDL | Latest SDL **2.x** (not SDL3), from `third_party/SDL2`, built from source as a shared lib via its own CMake |
| Audio | openal-soft from `third_party/openal-soft`, built from source (Android uses its OpenSL/AAudio backend) |
| Net | enet from `third_party/enet`, built static |
| Android project | Start from SDL2's `android-project/` template, copied to `android/`. Gradle + `externalNativeBuild` **CMake** (not ndk-build) |
| Toolchain | Existing SDK at `~/Library/Android/sdk`: NDK `27.1.12297006`, CMake `3.22.1`, platform `android-36`, build-tools `36.0.0`, JDK 21 at `/usr/bin/java`. Do not install other NDK/SDK versions unless the build provably needs one; say so if it does |
| API levels | `minSdk 24`, `targetSdk 36`, `compileSdk 36` |
| Package id | `com.sevenkfans.sevenkingdoms` (personal build; app label "Seven Kingdoms") |
| Orientation | Landscape only (`sensorLandscape`), fullscreen immersive |
| Char signedness | Compile game code with `-fsigned-char` (ARM `char` is unsigned by default; x86 build assumes signed; upstream `configure.ac` enforces it too) |
| Preprocessor defines | Replicate the Linux build's defines from `docs/AUDIT.md` section I in CMake **explicitly** (do not run `configure`; its `-mfpmath=387` probe fails on arm64). Never define `USE_ASM` (x86 asm). Leave `HAVE_KNOWN_BUILD` undefined and multiplayer off (out of scope); the "?" in the version string is accepted. No libcurl, no NLS |
| Video | Game renders 800x600 8-bit paletted into a streaming texture (`OVGA.cpp`). On Android force fullscreen + `vga_keep_aspect_ratio` on, so SDL's `SDL_RenderSetLogicalSize(800,600)` letterboxes. Handle `SDL_RENDER_DEVICE_RESET` (GLES context loss) by recreating renderer resources |
| Data / saves | Game `chdir`s into `SKDATA` and `fopen`s relative uppercase paths (`OSYS.cpp:2823-2919`). Extract assets to internal storage and set `SKDATA` before game init. Saves/config already use `SDL_GetPrefPath` (`OSYS.cpp:531`), which is internal storage on Android: keep it |
| Sources in git | `third_party/*` is NOT committed (≈300 MB). `scripts/fetch-deps.sh` downloads every component listed in `third_party/VERSIONS.md` and verifies its sha256; a clean clone = `fetch-deps.sh` then `build.sh` |

---

## 3. Code rules

1. **Upstream stays pristine.** Never edit files under `third_party/` in place. All game-source changes live as
   patch files in `patches/7kaa/NNNN-short-name.patch` (git-format, applied by a script `scripts/apply-patches.sh`
   onto a working copy at `build/7kaa-src/`), OR as new files under `port/` that the CMake build adds.
   Prefer `port/` + small `#ifdef __ANDROID__` hooks over large patches.
2. **Android-specific code** goes under `port/android/` (C++) and `android/app/src/main/java/...` (Java).
   Guard any change to shared game code with `#ifdef __ANDROID__` so the desktop build behaviour is unchanged.
3. **Minimal Java.** Subclass `SDLActivity`; only add Java for things C++ cannot do (asset extraction may be in
   either; prefer C++ with `SDL_RWops`/`AAssetManager` if simpler).
4. **Match upstream style** in any game-source patch (indentation, naming, comment density).
5. **No silent fallbacks.** If data extraction or init fails, show an SDL message box with the reason and exit;
   never boot into a half-working state.
6. **Builds must be reproducible from a clean clone**: `scripts/fetch-deps.sh` (download + sha256 check, idempotent),
   then `scripts/build.sh` = apply patches, `./gradlew assembleDebug` (and `assembleRelease` once signing exists).
   No manual steps beyond those two commands.
7. **No secrets in git.** The release keystore and its passwords live outside the repo
   (`~/.android/7kaa-release.jks`, passwords in `~/.android/7kaa-release.properties`), referenced by path.
8. **No git** (owner's instruction, 2026-10-12). Do not run git commands (init, commit, branch, worktree). Edit files
   in place in `/Users/hd/Work/7kaa-android` and report changed files by path.
9. **No tests.** Do not write unit/instrumentation tests. Do run the build and report its result honestly.

---

## 4. Licence obligations (GPL v2)

- Ship `COPYING` (GPL v2) and the music licence `COPYING-Music.txt` inside the APK assets and show them from a
  "Licences" entry (a long-press menu or a button on the first-run screen is fine; keep it simple).
- The repo itself (this port + patches) is the corresponding source; keep `third_party/VERSIONS.md` accurate.
- Music is not GPL. `COPYING-Music.txt` allows redistribution only complete, unmodified, with authors credited
  and the notice kept: ship all 10 WAVs unmodified + the notice. Personal build; note this in `README.md`.
- Unaligned / `#pragma pack(1)` access (AUDIT risk 5): arm64 user space tolerates ordinary unaligned loads, so
  do NOT pre-emptively rewrite these. Only fix a site if a SIGBUS shows up in logcat.

---

## 5. Phases and acceptance criteria

Each phase ends with the orchestrator's verification pass (section 6). The next phase does not start until
the previous one is signed off.

### Phase 0 — Audit (done by audit agent)
- `third_party/` populated, `third_party/VERSIONS.md`, `docs/AUDIT.md` with portability risks.

### Phase 1 — It runs
- `scripts/build.sh` produces `app-debug.apk` for arm64-v8a with zero manual steps.
- Game data + music are packaged as APK assets and copied on first launch to app-internal storage
  (`SDL_AndroidGetInternalStoragePath()`), with a version stamp so they are re-copied only after an update.
  `SKDATA` (or whatever the audit found) points there.
- Save games and config go to app-internal storage, not the data directory or external storage.
- Game renders at its native resolution, scaled to fill the screen with aspect ratio kept (letterbox), using
  `SDL_RenderSetLogicalSize` or the game's own scaling path.
- Single tap = left click (SDL default touch→mouse is acceptable in this phase).
- Sound effects and music play.

### Phase 2 — It's playable (touch controls)
Implemented in one input-translation layer under `port/android/touch*.{h,cpp}` that turns `SDL_FINGER*` events
into the mouse/keyboard events the game already understands. Game logic is not modified for input.

| Gesture | Action |
|---|---|
| Tap | Left click at finger |
| Long-press (≥ 400 ms, < 12 px movement) | Right click at finger position, with a visible ring/feedback |
| Two-finger tap | Right click at the midpoint |
| One-finger drag on the map with a unit/building selected | Left-button drag (box select) |
| Two-finger drag | Scroll the map (translate to the game's scroll, not edge-scrolling hacks if avoidable) |
| Back button | Opens the game's in-game menu (Esc) |

- Disable SDL's own touch→mouse synthesis (`SDL_HINT_TOUCH_MOUSE_EVENTS=0`) once this layer exists, so clicks
  are not doubled.
- A small, semi-transparent, collapsible on-screen toolbar for the hotkeys that have no mouse equivalent
  (the audit lists them; pick the ≤ 6 most important, e.g. game speed, pause, F-key reports). It must not cover
  the game's own sidebar buttons.
- Coordinates must be correct after letterbox scaling (map touch → logical game coordinates).

### Phase 3 — It behaves like an app
- Pause on background (`SDL_APP_WILLENTERBACKGROUND`/`DIDENTERBACKGROUND`): pause game, pause audio. Resume cleanly.
- Survive a screen-off/on and a switch to another app for 5+ minutes without crashing or losing the game.
- Autosave on background if the game supports it without engine changes; otherwise rely on the user's saves and
  say so in the report.
- Immersive fullscreen (no status/nav bars), screen stays on while playing (`FLAG_KEEP_SCREEN_ON`).
- Proper launcher icon (derived from the game's own art in the data files), app label "Seven Kingdoms".
- Signed release APK via `scripts/build.sh release` using the external keystore (section 3.7). Create the
  keystore with `keytool` if missing.
- `README.md`: what this is, how to build, how to install (`adb install`), controls table, licence notes.

---

## 6. Verification checklist (orchestrator runs this per phase)

Checks are done by reading the diff, running the build, and, when a phone is connected, driving it with
`adb` (install, `adb shell input tap/swipe`, `adb exec-out screencap -p`, `adb logcat -s SDL SDL/APP 7kaa`).
"Device" items stay **UNVERIFIED** until a phone is connected; they are never assumed to pass.

### Every phase
- [ ] Contract rules 3.1–3.9 hold: no edits under `third_party/`; patches apply cleanly to a
      fresh copy; Android code is `#ifdef __ANDROID__`-guarded.
- [ ] `scripts/build.sh` from a clean state (`rm -rf build android/app/build android/app/.cxx`) succeeds; build
      log has no new warnings in `port/` code.
- [ ] `unzip -l` of the APK shows `lib/arm64-v8a/` with `libmain.so`, `libSDL2.so`, `libopenal.so` and no other ABIs.
- [ ] No keystore or password file inside the project folder.
- [ ] Agent's report matches the diff (no claimed work that is not in the code).

### Phase 1
- [ ] APK assets contain the full game data folder and music (count files vs `third_party/7kaa/data`).
- [ ] Device: cold install + first launch reaches the main menu; logcat shows data copy then game init, no crash.
- [ ] Device: second launch skips the copy (version stamp works).
- [ ] Device: screenshot shows the game letterboxed with correct aspect ratio.
- [ ] Device: start a new single-player game; taps select a unit/building; sound effects and music audible.
- [ ] Device: save a game, force-stop (`adb shell am force-stop`), relaunch, load it.

### Phase 2
- [ ] Code review: one input layer, no game logic touched; SDL touch→mouse synthesis disabled.
- [ ] Code review: letterbox coordinate mapping applied to every gesture.
- [ ] Device (injected): tap selects; long-press (`input swipe x y x y 600`) issues a move/right-click;
      two-finger scroll moves the map (needs a real finger test, mark UNVERIFIED via adb if it cannot be injected).
- [ ] Device (owner playtest): play 15+ minutes of a new game with touch only; owner notes friction points.
- [ ] Toolbar does not cover the sidebar in a screenshot.

### Phase 3
- [ ] Device: home → wait 5 min → return: game resumes, audio resumes, no crash in logcat.
- [ ] Device: screen off/on mid-game: same.
- [ ] Device: no status/nav bar visible; screen does not dim during play.
- [ ] Release APK is signed (`apksigner verify --print-certs`), installs over itself on update without data loss.
- [ ] Licences screen shows GPL + music licence.
- [ ] README build steps reproduce the APK on a clean clone.

---

## 7. Reporting format (every implementation agent)

End with:
1. What was built (files, one line each).
2. Build result: exact command + pass/fail + last 20 lines of output on failure.
3. Deviations from this contract and why (empty if none).
4. Known issues / what remains UNVERIFIED (device items always listed here if no phone was attached).
5. Commit hashes.
