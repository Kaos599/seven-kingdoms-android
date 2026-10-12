# Widescreen port: contract

Branch `feature/widescreen` (from `dev`). Goal: the game fills a wide phone screen. The in-game view
(map + panel) renders at a wide logical resolution; menus and full-screen screens stay 800x600 and are
centred. Source of the widescreen code: the GPL fork **kresimirv/7kaa_HD** at commit `cf1cedb`
(base `3f229fd` = upstream 2.15.8).

Evidence this contract is based on: `docs/widescreen/STUDY.md` (desk study) and `docs/widescreen/SPIKE.md`
(measured spike: HD's diff applies to 2.15.7 with 0 conflicts, our patches with 1 trivial conflict,
44 compile errors of a single kind, APK built).

`docs/CONTRACT.md` stays binding (upstream pristine, `#ifdef __ANDROID__` for our own changes, no secrets,
no tests, report format) except where this file overrides it.

---

## 1. Scope

**In scope (this branch, now)**
- Vendor HD's diff as patch files and apply it before our own patches.
- Fix HD's unparenthesised macros.
- Pick the logical resolution from the phone's screen on Android.
- Drop HD's 11.6 MB `I_IF_3840_2160.RES`; build the wide top bar at runtime from the original `MAINSCR`.
- Retarget the touch layer, toolbar and cheats panel to the wide resolution.
- An opt-in side-by-side build (separate application ID) so the widescreen build can later be installed
  next to the released app without touching its saves.
- Docs.

**Out of scope (later phase, needs the owner)**
- Running on the phone, visual checks, bug fixing found at runtime, screenshot review.
- Pinning reports to 576 px (HD lets them stretch; accept for now).
- Multiplayer screens.
- Merging into `dev` (the PR stays a **draft** until on-device testing is done).

---

## 2. Fixed decisions

| Item | Decision |
|---|---|
| HD source | `.../scratchpad/7kaa_hd` clone, `git diff 3f229fd cf1cedb -- src include` (63 files, +1164/-779). Record repo URL, commit, licence (GPL v2, inherits upstream `COPYING`) in `third_party/VERSIONS.md` |
| Patch layout | New directory `patches/hd/`, applied **before** `patches/7kaa/` by `scripts/apply-patches.sh`. `patches/hd/0001-7kaa_hd-cf1cedb.patch` = HD diff verbatim. Our fixes on top of HD are `patches/hd/0002+`. Our existing `patches/7kaa/0001-0008` are regenerated only where they no longer apply (known: the `#include` hunk in `0004` and the knock-on in `0005`, both `OVGA.cpp`) |
| Logical resolution (Android) | Height **610**. Width = largest multiple of **32** that is ≤ `screen_w / screen_h * 610` (rounded down), minimum 800. On the owner's 2712x1220 phone this gives **1344x610** (exact 2.0x, 12 px side bars). Computed in `Config::init_resolution` under `#ifdef __ANDROID__` from `SDL_GetDesktopDisplayMode` (or the window size); desktop behaviour unchanged |
| Screens ≤ 4:3 | If the computed width < 800 or the screen is ≤ 4:3, use 800x600 (current behaviour) |
| Top-bar art | Always load `I_IF.RES` (never `I_IF_3840_2160.RES`; revert the selection at `OSYS.cpp:~425`). In `Info::disp_panel` (`OINFO.cpp`), replace HD's four `put_cropped_area` crops with: left part of `MAINSCR` as-is, a filler built by **mirror-tiling** a stone strip from the original's top bar (no visible seams, keep the bezel lines at the right phase), the right block (map-mode buttons + panel, 224 px) anchored to the right edge, and the bottom/right frame. Cache the composed wide image once per resolution; no per-repaint file reads |
| HD macros | Parenthesise every macro body HD introduced that contains an operator (`#define X2 (ZOOM_X2 - 6)`); ~129 of them. Mechanical; no other edits in that patch |
| Compiler | Add `-Wno-c++11-narrowing` for game sources only (HD's runtime initialisers in braced lists). Do not add it to `port/` |
| Touch | `sk_touch.cpp` `GAME_W/GAME_H` (800x600) become the runtime `VGA_WIDTH/VGA_HEIGHT`; everything derived from them follows |
| Toolbar | With no side margin the existing collapsible-strip mode applies; move its handle from (0,0) (it covers the game's top-left menu buttons) into the new top-bar filler area, right of the date text and left of the map-mode buttons |
| Cheats panel | With no side margin it uses its existing overlay mode, as a panel sliding in from the left edge over the map; it must not cover the right-hand info panel |
| Side-by-side build | Gradle property `-PsideBySide` (passed by `scripts/build.sh ... --side-by-side`): `applicationIdSuffix ".hd"`, label "Seven Kingdoms HD". Default build unchanged (`com.sevenkfans.sevenkingdoms`) |
| Saves | No save-format change. HD's camera clamps (`OMATRIX.cpp`, `OWORLD_Z.cpp`) handle loading an 800x600 save into a wider view |

---

## 3. Process rules

1. Agents **do not run git**. The orchestrator commits each task as its own commit(s) on `feature/widescreen`.
2. Agents run one at a time in `/Users/hd/OS/7kaa-android` (one Gradle build at a time).
3. Every task ends with `scripts/apply-patches.sh` applying all of `patches/hd/*` then `patches/7kaa/*` to a
   pristine copy, and `scripts/build.sh` (debug) **compiling and linking**. A compile is required; running the
   app is not.
4. New patches are unified diffs generated with `diff -u` (or `git diff --no-index`) against the tree produced
   by the patches before them, with the same `-p` level `apply-patches.sh` uses.
5. Our own Android-only changes stay under `#ifdef __ANDROID__`. HD's changes are not guarded (they are the new
   baseline on this branch).
6. No device. No `adb`. No tests.

---

## 4. Tasks

| # | Task | Agent | Output |
|---|---|---|---|
| W1 | Vendor HD diff as `patches/hd/0001`; teach `apply-patches.sh` the `patches/hd/` step; regenerate `0004`/`0005` hunks; narrowing flag; VERSIONS.md provenance; side-by-side Gradle option | Haiku | compiling build |
| W2 | Parenthesise HD macros (`patches/hd/0002-parenthesize-macros.patch`) | Haiku | compiling build, count of macros changed |
| W3 | Android resolution choice + always `I_IF.RES` + runtime mirror-tiled top bar (`patches/hd/0003+`) | Sonnet | compiling build |
| W4 | Touch layer, toolbar handle, cheats overlay retargeted to the runtime resolution (`port/android/`) | Sonnet | compiling build |
| W5 | README "Widescreen (experimental)" section, `docs/widescreen/` notes | Haiku | docs |

---

## 5. Verification checklist (orchestrator, per task)

- [ ] `third_party/` untouched (diff against the release archive).
- [ ] `apply-patches.sh` applies every patch in order to a pristine copy; no `.rej`/`.orig` files left.
- [ ] `scripts/build.sh` compiles and links from a clean build dir; no new warnings in `port/`.
- [ ] APK contains only `lib/arm64-v8a/`; does **not** contain `I_IF_3840_2160.RES`.
- [ ] Diff matches the agent's report; Android-only changes are `#ifdef __ANDROID__`-guarded.
- [ ] W2: no remaining HD-added `#define` with an operator and no parentheses (`grep`).
- [ ] W3: the resolution formula yields 1344x610 for 2712x1220 and 800x600 for a 4:3 screen (worked through in
      the report from the code).
- [ ] W4: no literal 800/600 left in `port/android/` except comments and the ≤4:3 fallback.
- [ ] Default build keeps application ID `com.sevenkfans.sevenkingdoms`; `--side-by-side` produces `.hd`.

**Deferred to the on-device phase (UNVERIFIED until then):** menus centred and clickable, top bar seamless,
reports readable, scrolling/minimap, save/load of an 800x600 save, toolbar and cheats overlay usable.

---

## 6. Report format (every agent)

1. What was built (files, one line each).
2. Build result: exact command, pass/fail, last 20 lines on failure.
3. Deviations from this contract and why.
4. What remains UNVERIFIED.
5. Files created/changed (paths).
