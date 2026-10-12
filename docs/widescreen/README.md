# Widescreen port: engineering note

Branch `feature/widescreen` (from `dev`). Status: **compiles and links; not yet run on a device.**
Contract: `docs/WIDESCREEN-CONTRACT.md`. Study and spike: `STUDY.md`, `SPIKE.md` here.

## What it is

The in-game view (map plus right-hand panel) renders at a wide logical size chosen from the phone's screen.

- Height is always 610 logical pixels.
- Width is the largest multiple of 32 not above `aspect x 610`.
- Screens of 4:3 or narrower keep HD's 800x600.

| Screen | Logical size |
|---|---|
| 2712x1220 (exact 2.0x) | 1344x610 |
| 1280x720 | 1056x610 |
| 4:3 | 800x600 |

Menus and full-screen screens stay 800x600 and are centred in the wide frame.

## Where the code comes from

The widescreen code is adapted from **kresimirv/7kaa_HD** at commit `cf1cedb` (GPL v2, inherited from upstream
`COPYING`). Its base is upstream 2.15.8 (`3f229fd`). The fork's diff is vendored verbatim, then our patches change
it. Provenance is in `third_party/VERSIONS.md`.

## Patches

`patches/hd/` holds the widescreen base. Each file is a git-style diff applied with `-p1`.

| Patch | What it does |
|---|---|
| `0001-7kaa_hd-cf1cedb.patch` | HD's diff, byte-identical to `git diff 3f229fd..cf1cedb -- src include` (63 files, +1164/-779). Applies to 2.15.7 with no fuzz. Not edited. |
| `0002-parenthesize-macros.patch` | Wraps the 129 bare macros HD added (145 `#define` lines in 21 files) in parentheses. Unparenthesised, `#define REPORT_DET_X2 ZOOM_X2 - 6` expanded wrongly in arithmetic: a detail panel off by 6 px (`OR_NAT.cpp:681`), a text fit off by 16 px (`ONEWS.cpp:359`), an allocation 20 columns too big (`OINFO.cpp:272`). Only `#define` bodies change. |
| `0003-android-resolution-from-display.patch` | Android only. `Config::init_resolution` reads the display size with `SDL_GetDesktopDisplayMode` and applies the formula above. On Android `OSYS.cpp` always loads `I_IF.RES`, so the 3840x2160 art is never loaded, and a clean build confirms it is not packaged in the APK. Desktop paths are unchanged. |
| `0004-android-wide-panel-from-mainscr.patch` | Android only. Builds the wide panel background once at startup from the original 800x600 `MAINSCR` in `I_IF.RES`; each repaint is a row copy. Replaces HD's four crops of the 11.6 MB 3840x2160 image. |

`patches/7kaa/0004` and `0005` were regenerated only for context lines after `0001` landed; their changes are unchanged.

## Patch order

`scripts/apply-patches.sh` (run by `scripts/build.sh`):

1. Copies `third_party/7kaa/{src,include}` fresh into `build/7kaa-src` (`rsync -c --delete`). `third_party/` is never written.
2. Applies `patches/hd/*.patch` in sorted order (0001 to 0004), then `patches/7kaa/*.patch` (0001 to 0008).
3. Runs each as `patch -p1 -s`; any reject (or a stray `.rej`/`.orig`) aborts the run and prints the hunks.

## Geometry at 1344x610

| Region | Logical rectangle |
|---|---|
| Top bar | y 0..55 |
| Map view | x 0..1119, y 56..599 (1120x544 = 35x17 tiles of 32 px) |
| Right block (map-mode buttons, minimap, info panel) | x 1120..1343 (224 px, flush right) |
| Bottom frame | y 600..609 (source rows 590..599) |

The right block sits at `x = zoom_width = W - 224`. The top-bar gap is filled by mirror-tiling the stone at source
columns 424..575. The first tile is flipped so it continues column 575 without a seam. Rows are copied whole so the
bezel lines line up.

## Android UI in wide mode

- **Toolbar handle** sits in the top-bar stone filler, right of the date text, at logical x 1062..1112 (50 px; right
  edge 8 px left of `zoom_width`). It used to be at (0,0), over the game's top-left menu buttons.
- **Strip** opens downward from the handle over the map view, clear of the right block. Hidden in menus.
- **Cheats panel** is a left overlay over the map: x 0..373 (one third of `zoom_width`), below the top bar.
- **4:3 screens** keep the previous layout.
- **Touch layer** (`port/android/sk_touch.cpp`) reads `config.win_width` and `config.win_height` instead of a fixed 800x600.

## Side-by-side build

```sh
scripts/build.sh debug --side-by-side
scripts/build.sh release --side-by-side
```

This builds application ID `com.sevenkfans.sevenkingdoms.hd` with the label "Seven Kingdoms HD" (Gradle
`-PsideBySide`, `applicationIdSuffix ".hd"`). It installs next to the released app with its own data, so the released
app's saves are untouched. The default build keeps `com.sevenkfans.sevenkingdoms`.

A debug build is signed with the debug key, a release build with the release keystore, so their signatures differ and
neither can update the other. On a phone with the release app, use `--side-by-side` for any test install.

`port/CMakeLists.txt` adds `-Wno-c++11-narrowing` for game sources only, for HD's runtime braced initialisers.

## Status

Compiles and links (`scripts/build.sh`, debug). Not run on a device. No `adb` and no tests were run.

Still UNVERIFIED:

- Contract section 5, deferred list: menus centred and clickable; top bar seamless; reports readable; scrolling and
  minimap; a save made at 800x600 loads into the wide view (relies on HD's camera clamps in `OMATRIX.cpp` and
  `OWORLD_Z.cpp`); toolbar and cheats overlay usable.
- `SDL_GetDesktopDisplayMode` returns the full screen size on the phone. The formula depends on it.
- The top-bar mirror-tile seam may be visible.
- The toolbar handle may overlap the right edge of a centred in-game popup by about 10 px.
- Kingdoms and Economy reports stretch with ragged columns. This is HD's behaviour, accepted for now.

## On-device checklist (next phase; needs the owner and the phone)

Install `scripts/build.sh debug --side-by-side` next to the released app, then:

- [ ] On a 2712x1220 phone, the map is 1120x544 and the window is 1344x610 logical.
- [ ] On a 4:3 device, the layout is 800x600, as before.
- [ ] Main menu, load/save and options are 800x600, centred; every button responds to a tap.
- [ ] Top bar: no seam or visible tile repeat between the left tablets and the right block.
- [ ] Map view scrolls with a two-finger drag; right edge and bottom frame look right.
- [ ] Minimap: tap moves the view; the frame is complete.
- [ ] Info panel: date, messages and buttons respond to taps.
- [ ] Toolbar handle opens the strip downward; Pause, Slower, Faster, K, G, D each work; strip hidden in menus.
- [ ] Cheats panel opens from C over the map; Cheats ON and each button work; no overlap with the right block.
- [ ] Measure the overlap of a centred in-game popup with the toolbar handle (expected up to about 10 px).
- [ ] Log `SDL_GetDesktopDisplayMode` once and compare with the phone's screen size.
- [ ] Kingdoms and Economy reports: readable; note ragged columns or clipped text.
- [ ] A save made at 800x600 loads in the wide build; camera and view are usable.
- [ ] Long session: no crash after many repaints or after switching apps (pause still works).
- [ ] Default (non side-by-side) release APK still installs as an update over the released app.
