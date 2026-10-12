# LAN multiplayer: contract

Branch `feature/lan-multiplayer` (from `feature/phone-ux`). Goal: two or more phones running the **same APK** play
a multiplayer game over the same Wi-Fi network or one phone's hotspot, using the game's own lobby. Phone vs PC is
out of scope; the game already refuses mismatched builds (`OGAMEMP.cpp:3143-3150`, `FLAG_UNKNOWN_BUILD`).

Evidence: the read-only study of 2026-10-12 (summarised in the owner conversation). Key facts, paths in the
patched tree `build/7kaa-src`:
- `DISABLE_MULTI_PLAYER` is defined at `port/CMakeLists.txt:103`; it hides the main-menu item
  (`OGAMMAIN.cpp:94-97, 322, 690`) and the lobby case (`AM.cpp:384`). `OGAMEMP.cpp`, `OREMOTE*.cpp` and
  `enet/multiplayer.cpp` already compile; without `USE_ENET` the latter is stubs. enet is built but not linked
  (`port/CMakeLists.txt:62-65, 126`).
- LAN discovery: the host broadcasts its session every 5 s to UDP 19383 (`multiplayer.cpp:836-843`); game
  traffic is UDP 19255. "Enter Address" joins by IP or host name (`OGAMEMP.cpp:665-675`, default text "localhost").
- libcurl is only needed for the 7kfans online service (service mode 3) and the leaderboard (mode 4).
- Lockstep: a peer missing for `CONNECTION_LOST_TIME_OUT` (20 s, `OSYS.cpp:~1275`) is turned into an AI nation.
  Desync detection exists (`remote.sync_test_level`, random-seed and object-CRC compare) but is silent in release.
- The app has no network permission (only `VIBRATE`). Background pauses the game and blocks the main thread
  (`port/android/sk_lifecycle.cpp:86, 93-104`, `SDL_HINT_ANDROID_BLOCK_ON_PAUSE`).

`docs/CONTRACT.md` (upstream pristine, `#ifdef __ANDROID__`, no secrets, no tests, report format) and the
earlier contracts stay binding except where this file overrides them. Single-player behaviour must not change.

---

## 1. Fixed decisions

| Item | Decision |
|---|---|
| Build | Remove `DISABLE_MULTI_PLAYER`, define `USE_ENET`, link the enet static library and its include dir. No libcurl. `HAVE_KNOWN_BUILD` stays undefined |
| Permissions | `INTERNET`, `ACCESS_NETWORK_STATE`, `ACCESS_WIFI_STATE`, `CHANGE_WIFI_MULTICAST_STATE` (all install-time, no runtime prompt). If the target SDK requires a runtime permission for local-network access, report it with the source; do not add one speculatively |
| Broadcast reception | Hold a `WifiManager.MulticastLock` only while the game's multiplayer object is initialised (acquire on init, release on deinit), via a small JNI call from our port code into the activity. Single player never holds it |
| Online service | Hide or disable the 7kfans service button and the leaderboard path (they need libcurl). LAN and "Enter Address" remain |
| Own address | On the multiplayer screens, show this phone's IPv4 address(es) on the local network (Wi-Fi and hotspot interfaces, no loopback), e.g. "Your address: 192.168.1.23", so a host can tell a friend what to type. Use `getifaddrs` in our port code; draw with the game's own font in a free area of the screen |
| Enter Address | The box starts with the last address used successfully this run (or empty), not "localhost" |
| Keyboard | The existing soft-keyboard support (`patches/7kaa/0009`, `sk_text`) must cover every multiplayer text field (session name, password, player name, address, lobby chat). Check each path; add hooks only where a field takes input without `GetA::detect_click` or `Game::input_box`/`input_name_pass` |
| Background during a match | Find out from the code what `sys.pause()` does in a multiplayer game (does it send a pause to the other players?) and choose the behaviour that best keeps the match alive across a short switch-away (up to ~60 s), with file:line reasoning. Acceptable options: a networked pause that both phones honour, and/or raising `CONNECTION_LOST_TIME_OUT` on Android to 60 s. The main thread may still block in the background. Report what happens after the limit |
| Match notice | When a multiplayer match starts, show one game message: "Keep the app open: leaving it for more than a minute hands your kingdom to the computer." (wording may be adjusted to the actual limit) |
| Desync notice | When `remote.sync_test_level` reports a mismatch, show one visible game message ("Games out of sync: this match may play differently on each phone") once per match. No other behaviour change |
| Determinism | No libm replacement in this branch. List the sim call sites of sin/cos/atan2/sqrt in the engineering note as a known risk for phones on different Android versions |
| Cheats in multiplayer | Unchanged: the game already disables cheats in multiplayer (`remote.is_enable()`); our cheats panel must show them locked during a match |

---

## 2. Process rules

1. Agents do not run git or adb, and do not touch a phone. No tests. Do not delete files you did not create.
2. `third_party/` read-only. Upstream code changes only in a new `patches/7kaa/0012-*.patch` (unified diff against
   the tree produced by all earlier patches, same `-p` level as `scripts/apply-patches.sh`), under
   `#ifdef __ANDROID__`. Our own code goes in `port/android/` (e.g. a new `sk_net.*`) and the Java activity.
3. Another agent may be editing `port/android/sk_cheats.*`, `patches/7kaa/0008-*`, `docs/CHEATS.md` in a separate
   checkout. Do not touch those files. If the cheats panel needs a multiplayer lock, report it instead.
4. Finish with `scripts/apply-patches.sh` (all patches apply, no `.rej`/`.orig`) and a clean `scripts/build.sh`
   (debug) that compiles and links with no new warnings in `port/`.

---

## 3. Tasks

| # | Task | Agent |
|---|---|---|
| N1 | Build flags, linking, permissions, MulticastLock via JNI | Sonnet |
| N2 | Menu item visible, online service hidden, own-address display, Enter Address default, keyboard coverage | Sonnet (same agent) |
| N3 | Background behaviour, match notice, desync notice | Sonnet (same agent) |
| N4 | `docs/multiplayer/README.md` engineering note (how to play, ports, limits, determinism risk, two-phone test checklist); README section "Multiplayer (Wi-Fi, experimental)" | Sonnet (same agent) |

---

## 4. Verification checklist (orchestrator)

- [ ] `third_party/` untouched; all patches apply; clean debug and release builds pass.
- [ ] Diff matches the report; upstream edits under `#ifdef __ANDROID__`; file ownership respected.
- [ ] `USE_ENET` defined, `DISABLE_MULTI_PLAYER` gone, enet linked; no libcurl.
- [ ] MulticastLock acquired/released with the multiplayer object, never in single player.
- [ ] Background decision explained with file:line; single-player background behaviour unchanged.
- [ ] Desync and match notices only fire in multiplayer.

**Deferred to a two-phone test (UNVERIFIED):** discovery on a real router and on a hotspot, joining by address,
keyboard in the lobby, a 20-30 minute match staying in sync, behaviour on switch-away and screen-off, phones on
different Android versions.

---

## 5. Report format

1. What was built (files, one line each).
2. Build result: exact command, pass/fail, last 20 lines on failure.
3. Deviations from this contract and why.
4. What remains UNVERIFIED.
5. Files created/changed (paths).
