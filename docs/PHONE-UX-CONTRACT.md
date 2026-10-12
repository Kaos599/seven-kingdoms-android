# Phone UX: contract

Branch `feature/phone-ux` (from `feature/widescreen`). Goal: the main map handles like a phone game (drag to pan,
pinch to zoom), a tap gives units their orders (move, attack, station in a building) without a hold, box-select
and control groups work without a mouse or keyboard, text fields raise the on-screen
keyboard, and the unit cheats apply to every selected unit.

Evidence: the code study summarised in the owner conversation of 2026-10-12. Key facts:
- the engine view is tile-granular (`Matrix::scroll`, `src/OMATRIX.cpp:404-410`; pixel origin `top_x_loc * 32`),
  with no sub-tile offset anywhere;
- the map view size is set once at startup (`config.zoom_width/height`, `src/OCONFIG.cpp:131-132`);
- `Vga::flip` uploads the whole back buffer every frame and letterboxes it by hand (`src/OVGA.cpp:845-871`, our patch 0007);
- the current two-finger scroll calls `mouse.process_scroll` in whole 32 px cells (`port/android/sk_touch.cpp:589-613`).

`docs/CONTRACT.md` and `docs/WIDESCREEN-CONTRACT.md` stay binding (upstream pristine, `#ifdef __ANDROID__`, no
secrets, no tests, report format) except where this file overrides them. The 4:3 layout (800x600 with side bars)
must keep working.

---

## 1. Fixed decisions

| Item | Decision |
|---|---|
| Camera model | A presentation-layer camera in our code. The engine keeps rendering its full map view (`ZOOM_WIDTH x ZOOM_HEIGHT` at the map origin) at 1:1. The camera shows a **window** of that view, size `view / z`, scaled up to fill the map's on-screen rectangle. UI outside the map rectangle (top bar, right block, toolbar, cheats panel) is always copied at 1:1. No changes to engine drawing or hit-testing code |
| Camera position | The camera target is kept in **world pixels**. Every frame (in `Vga::flip` or just before it): read the engine's `top_x_loc/top_y_loc`; if the window does not fit inside the engine view at its current position, request a whole-tile engine scroll (`mouse.process_scroll`) towards it; the window offset = target − engine origin, clamped to the engine view. This absorbs the one-frame delay of the engine's scroll and its map-edge clamping. When the engine view moves without our request (minimap click, K/G jump, message "go to"), the camera re-centres on the new engine view |
| Zoom | `z` from **1.0 to 2.0**. Default **1.0** (today's look). At `z = 1.0` the window equals the view, so panning falls back to whole-tile steps; above that, panning is pixel-smooth. The default is one constant so it can be changed after on-device testing |
| Scaling filter | Nearest-neighbour, as today |
| When the camera applies | Only while the main map is showing (normal view mode, no report screen, no in-game menu or message box covering the map). Otherwise the frame is shown 1:1, exactly as today |
| Touch mapping | Every touch inside the map rectangle is inverse-mapped through the camera before it reaches the game. Taps land on the unit under the finger at every zoom level |
| Minimap | Its view box shows the camera window, not the full engine view (within 1 tile) |
| One-finger drag on the map | Pans the camera, following the finger. Releasing with speed starts a fling that decays to a stop in under ~1 s; a new touch stops it |
| Two fingers on the map | Pinch zooms about the finger midpoint; moving the midpoint pans at the same time. Two-finger tap with no movement or spread stays a right click |
| Smart tap on the map | A tap sends a **right click** when the game itself would treat a right click there as a command for the current selection (move, attack, assign/station a unit inside a building, settle in a town, board a ship, etc.), and a **left click** otherwise (select). The decision reuses the game's own logic for that question: the code that picks the hover cursor shape / validates a right-click order for the selection at that location, called through a small accessor. No new rules about what counts as a command. With nothing selected, or a selection that is not the player's own units, a tap is always a left click (today's behaviour) |
| Long-press | The ring appears at ~0.4 s as today. Lift without moving = **the opposite of what a tap would do there** (so a long-press selects the building a tap would have sent the units into, and a long-press on empty ground with nothing selected stays a right click). Move after the ring = **box-select** from the press point; lift ends it |
| Deselect | A toolbar **Deselect** button (the game's own deselect, as when clicking empty ground with nothing to order) |
| Stationing units | Selecting a unit and tapping one of the player's buildings must assign it as the PC right click does. Find out why it does not work with today's long-press (report the cause with file:line) and make sure the new path does not have the same fault |
| Drags outside the map | Unchanged (UI panels, minimap and menus keep today's behaviour) |
| Toolbar additions | **Groups 1-5** (tap = select group n, hold ~0.5 s = assign the current selection to group n; via the game's own `group_select` functions or their key events) and **Zoom reset** (back to the default zoom). Buttons stay ≥ 44 logical px; use a second column if the strip runs out of room |
| Text fields | Whenever a game text field takes input (new-game player name, save-game name, any other `GetA`-style field), the Android keyboard opens (`SDL_StartTextInput`); it closes when the field stops taking input. Typed text, Backspace and the keyboard's Done/Enter key reach the field |
| Cheats on many units | `COMBAT +20`, `SKILL +20` and `SPY SKILL +20` apply to **every selected unit of the player's nation**, with exactly the per-unit effect and limits of the game's own cheat code. The other cheats are unchanged |

---

## 2. Process rules

1. Agents **do not run git**. The orchestrator commits.
2. Two work streams may run at once, each in its own checkout with its own `build/` and Gradle output. Each owns
   files the other must not touch:

   | Stream | Checkout | Owns | Must not touch |
   |---|---|---|---|
   | A (camera, gestures, toolbar) | `/Users/hd/OS/7kaa-android` | `port/android/sk_touch.*`, any new `port/android/sk_camera.*`, `patches/7kaa/0007-*`, new `patches/7kaa/0010-*` | `sk_cheats.*`, `patches/7kaa/0008-*`, `0009-*` |
   | B (keyboard, cheats) | `/Users/hd/OS/7kaa-android-wt-input` | `port/android/sk_cheats.*`, `patches/7kaa/0008-*`, new `patches/7kaa/0009-*`, any new `port/android/sk_text*` | `sk_touch.*`, `0007-*`, `0010-*` |

   Both may add lines to `port/CMakeLists.txt`. Avoid editing the same upstream source file in two streams. If B
   must touch `OVGA.cpp`, say so in the report.
3. `third_party/` is read-only. In the B checkout its contents are symlinks to the main checkout; never write
   through them.
4. Every task ends with `scripts/apply-patches.sh` applying every patch to a pristine copy (no `.rej`/`.orig`
   files) and `scripts/build.sh` compiling and linking. Running the app is not required. No `adb`, no device.
5. Patches are unified diffs against the tree produced by the patches before them, at the `-p` level
   `apply-patches.sh` uses. Our changes to upstream files are under `#ifdef __ANDROID__`.

---

## 3. Tasks

| # | Task | Stream | Agent |
|---|---|---|---|
| U1 | Camera: window/zoom state, world-pixel target, per-frame engine sync, scaled map copy in `Vga::flip`, inverse touch mapping, one-finger pan with fling, pinch zoom, minimap box | A | Sonnet |
| U2 | Smart tap, long-press as the opposite click, long-press-then-drag box-select, stationing fix, toolbar Groups 1-5, Deselect and Zoom reset | A (after U1) | Sonnet |
| U3 | On-screen keyboard for game text fields | B | Sonnet |
| U4 | Unit cheats apply to all selected own units | B | Sonnet (same agent as U3) |
| U5 | README controls table and cheats table updated | orchestrator | — |

---

## 4. Verification checklist (orchestrator)

- [ ] `third_party/` untouched; every patch applies in order to a pristine copy; clean debug build compiles and links.
- [ ] Diff matches the agent's report; upstream edits are `#ifdef __ANDROID__`; stream file ownership respected.
- [ ] U1: at `z = 1.0` and outside the map view, the presented frame is identical to today's (same single 1:1 copy).
  The camera is bypassed for reports, menus and message boxes. Inverse mapping is the exact inverse of the
  forward mapping (worked through in the report for one point at `z = 1.5`).
- [ ] U1: engine-initiated jumps (minimap, K/G) re-centre the camera; map-edge clamping cannot push the window
  outside the engine view.
- [ ] U2: the tap decision calls the game's own command/cursor logic (file:line), not a re-implementation; the
  long-press is its exact opposite; two-finger tap still gives a right click; group and deselect buttons call the
  game's own code; the stationing cause is explained with file:line.
- [ ] U3: keyboard start/stop is tied to the field's input state, not to a specific screen.
- [ ] U4: per-unit effect copied from the game's cheat code with file:line, own-nation units only.
- [ ] Merge of stream B into `feature/phone-ux` applies and builds.

**Deferred to the on-device check (UNVERIFIED until then):** pan and fling feel, pinch smoothness, tap accuracy
when zoomed, minimap box, box-select gesture, group buttons, keyboard on the name field, multi-unit cheats.

---

## 5. Report format (every agent)

1. What was built (files, one line each).
2. Build result: exact command, pass/fail, last 20 lines on failure.
3. Deviations from this contract and why.
4. What remains UNVERIFIED.
5. Files created/changed (paths).
