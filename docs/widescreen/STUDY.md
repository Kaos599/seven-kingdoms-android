# 7kaa 2.15.7 native widescreen: feasibility (read-only study)

Source: `/Users/hd/OS/7kaa-android/third_party/7kaa` (2.15.7, paths below are relative to it). Prior art cloned to
`scratchpad/7kaa_hd` (HEAD cf1cedb, base commit 3f229fd = upstream 2.15.8). Nothing in the android repo was touched.

## 1. Verdict

**Feasible, medium effort. Widening the in-game map view only (approach A) is a well-trodden path: a community fork
(kresimirv/7kaa_HD) already did exactly that on 2.15.8 with ~1,160 added / ~780 removed lines in 63 source files,
and 51 of those 63 files are byte-identical in 2.15.7.** Menus and full-screen art can stay 800x600 and be centred.
The one thing that has to be made by us is new top-bar art (a tiled/mirrored filler for a ~540 px gap).
The original 800x600 UI was designed with one dominant macro set (`ZOOM_*`, `INFO_*`, `MAP_*`), so the in-game HUD is
mostly cheap; the cost is in menus/dialogs/reports (hundreds of absolute coordinates) and in compile-time `enum`s.

## 2. How hard-coded is 800x600?

Counts are from `grep -w` over `src/` + `include/` (hits / files).

| symbol | hits / files | note |
|---|---|---|
| `VGA_WIDTH` / `VGA_HEIGHT` | 96/24, 78/23 | defined once, `include/OVGA.h:34-35` |
| `ZOOM_X1/Y1/X2/Y2` | 230/41, 208/39, 85/28, 70/24 | `include/OWORLDMT.h:61-64` |
| `ZOOM_WIDTH/HEIGHT` | 69/13, 70/13 | `OWORLDMT.h:66-67` (576 = 18 tiles, 544 = 17 tiles) |
| `ZOOM_LOC_WIDTH/HEIGHT` | 151/36, 142/34 | tile = 32 px, stays 32 |
| `MAP_X1/Y1/X2/Y2` (minimap) | 25/10, 23/10, 20/9, 9/6 | `OWORLDMT.h:48-51`, `MAP_X1 = 588+...` literal |
| `INFO_X1/Y1/X2/Y2` | 273/24, 166/24, 266/23, 26/7 | `include/OINFO.h:44-47` = 586/265/790/589 |
| literal 800/799 | 9 | only `OVGA.cpp:1044,1076,1087` and `OVGA.h:34` matter; rest unrelated (gold, decay) |
| literal 600/599 | 13 | same, window-size fallbacks in `OVGA.cpp` |

### (a) Central constants that "just work" when changed
- `VGA_WIDTH/HEIGHT` feed the surface (`OVGABUF.cpp:64-82`, `init(char front,int w,int h)` already takes w/h), the streaming
  texture (`OVGA.cpp:131-137`), the SDL surface (`:161-163`), mouse bounds (`OMOUSE.cpp:118-119, 302-303`), and
  `Vga::flip` (the Android patch 0007 already computes scale from `VGA_WIDTH/VGA_HEIGHT`).
- The 96+78 `VGA_*` uses are ~70% "clear/blt whole screen" and "centre text in the screen" (`OGAMMAIN.cpp:102,121,395`, `vgautil.cpp:77,106`,
  `OGAMCRED.cpp:149-331`). These only need `VGA_WIDTH` to become a runtime value.
- `INFO_*`-relative layout: files using `INFO_X1/X2/Y1` (24 files: all firm/unit/town info pages `OFIRMIF*.cpp, OF_*.cpp, OTOWNIF.cpp,
  OUNITIF.cpp, OU_*.cpp, OSITEDRW.cpp, OSPYA.cpp, OWALLRES.cpp`) are written as `INFO_X1+n`. A scan for literals 570-799 outside menu files finds
  only ~25 hits (OVGA 7, OWORLDMT.h 4, OWORLD_M 3, OINFO.h 3, OINFO.cpp 2, scattered 1 each). So the info panel is effectively movable by
  redefining `INFO_X1`/`INFO_X2`/`MAP_X1` (what 7kaa_HD did: `OINFO.h` `#define INFO_X1 ZOOM_WIDTH+10`).

### (b) Hard-coded pixel positions in UI code
- Main menu, single/multi-player, load/save, options, scenario, tutorial, credits, end-of-game, encyclopaedia, hall of fame:
  `OGAMEMP.cpp` (5,722 lines, mp lobby), `OGAMSING.cpp` (1,100), `OGAMMAIN.cpp` (902; `enum {SWORD1_X=258,SWORD1_Y=194}` at `:57`),
  `OGFILE*.cpp` (~5,500 lines total, `FILE_MAIN_MENU_X1` etc.), `OTUTOR*.cpp`, `OOPTMENU.cpp`, `OGAMENCY.cpp`, `OGAMHALL.cpp`, `OGAMEND.cpp`.
  All written for absolute 800x600 coordinates; none use a base origin. This is the bulk of the work if menus are to be centred (see approach A).
- Top bar (in game): food/cash/date text at literal x = 307, 305, 460, 476, 447 (`OINFO.cpp:393-460`, `disp_heading`); map-mode buttons at literal 579/586
  (`OWORLD_M.cpp:111, 121`); left menu buttons at the stone-tablet art. `TOP_MENU_X2 = ZOOM_X2-4` (`OINFO.h:41`).
- Reports (Kingdoms, Villages, Economy, Military, Technology, Trade, Espionage, Ranking, News, AI): `OR_NAT/TOWN/ECO/MIL/TECH/TRADE/SPY/RANK/NEWS/AI.cpp`, ~4,200 lines.
  They draw INSIDE the zoom window (`OR_ECO.cpp:40-49`: `INCOME_BROWSE_X2 = ZOOM_X2-6` etc.), so they are tied to the world-view rectangle, not full screen, and
  keep the right panel visible. Inner columns are literal offsets from `X1`. If the zoom window widens they would stretch with ragged columns unless pinned.
- Dialogs centred on the zoom window (`OINGMENU.cpp:49-50`, `OTALKRES.cpp:39-44`, `ONEWS.cpp`, `OTUTOR.cpp`, `OF_MARK.cpp`) use `ZOOM_*` and re-centre automatically once the constants are variable.

### (c) Full-screen 800x600 bitmaps in `data/`
Format: `RES` container = `uint16 count`, then `count+1` index entries of `{char name[9]; uint32 offset}` (`src/ORESX.cpp:36-48`, `include/ORESX.h:34-38`).
Image records are `int16 width, int16 height, <w*h raw 8-bit palette bytes>`, no compression, palette `data/RESOURCE/PAL_STD.RES` (all verified by decoding;
`scratchpad/res.py`, MAINSCR rendered to `scratchpad/mainscr.png`).

| file | records exactly 800x600 | used at |
|---|---|---|
| `I_IF.RES` | `MAINSCR`, `M_MAIN`, `OPTIONS`, `SCENARIO`, `TUTORIAL` (+ dialogs 295x344, 350x400, 638x398) | `OINFO.cpp:267` (in-game HUD), `OGAMMAIN.cpp:119,468,725`, `OOPTMENU.cpp:223`, `OGAMSCE2.cpp:285`, `OTUTOR2.cpp:217` |
| `I_MENU.RES` | 10: `MPG-PG1/PG2/BSC/O1/O2/GOAL`, `SPG-BSC/O1/O2/GOAL` | multiplayer / single-player setup screens (`OGAMEMP.cpp`, `OGAMSING.cpp`) |
| `I_MENU2.RES` | 4: `MPG-PG1, SPG-BSC, MPG-BSC, MPG-PG2` | same (the "real" menu skins) |
| `I_ENCYC.RES` | `ENCYC` | `OGAMENCY.cpp:148` |
| `TUT_PICT.RES` | 758x142 x13 (not full-screen) | tutorial strips |
| `data/ENCYC*/`, `data/IMAGE/*.ICN`, `CREDITS1.ICN/CHINESE.ICN` | 468.8 KB each (~800x600 ICN/COL pairs) | credits / loading screens; not decoded in detail |

So 17 full-screen paletted 800x600 images. Only ONE (`MAINSCR`) must be re-made for the in-game widescreen; the other 16 can be centred on black.

### (d) Logic that depends on the size
- Map viewport in cells: `Matrix::init` (`OMATRIX.cpp:31-70`, called with `ZOOM_*` at `OWORLD_Z.cpp:290-292`) computes `disp_x_loc = image_width/loc_width`
  (`OMATRIX.cpp:168-174`). All scroll limits (`:198-199, 312-387`) and minimap cargo size (`OWORLD.cpp:140-141`) derive from `disp_x_loc`. So they adapt automatically.
- Non-adaptive points (inclusive-edge constants; all would change to runtime values):
  - mouse hit-test/selection: `OWORLD.cpp:1618, 1656-1657`; `OWORLD_Z.cpp:789-790, 810-811, 919-920`; `OSYS.cpp:3167-3170`.
  - drawing clip: `OWORLD_Z.cpp:1052-1061, 1844-1853, 1945-2067` (sprite clip vs `ZOOM_*`); vibration copy `:530-542` (`ZOOM_WIDTH`).
  - cursor frame-select clamp `OMOUSECR.cpp:285-288`, `OSYS.cpp:364` (`set_frame_border`), `OSYS.cpp:446` (`anim_line`), rain/snow bounds `OWORLD_Z.cpp:569, 689`, lightning bounds (`OLIGHTN`).
  - fog and blacken: loops `OWORLD_Z.cpp:1083-1230` use `disp_x_loc`; 7kaa_HD had to add `std::min(..., max_x_loc)` clamps (`OWORLD_Z.cpp:330, 474, 1090, 1166, 1292`) for views larger than the map.
  - **Compile-time uses that break the moment `ZOOM_*` becomes a variable**: ~19 files declare `enum { X = ZOOM_X1+6, ... }` or `INFO_X1` in enums
    (`OR_RANK/AI/TOWN/NAT/TRADE/MIL/ECO/SPY/NEWS.cpp`, `OTOWNIF.cpp`, `OFIRMIF2.cpp`, `OF_INN.cpp`, `OF_HARB.cpp`, `OTALKRES.cpp`, `ONEWS.cpp`, `OTUTOR*.cpp`, `OINGMENU.cpp`, `OGAMSCE2.cpp`).
    Also array sizes `OMOUSECR.h:150-153` (`frame_*_save_scr[VGA_WIDTH+4]`). 7kaa_HD converted these to `#define`s and fixed arrays of 2000.
  - **Static-init order**: `World world;` is a global (`AM.cpp:233`) whose ctor builds `new MapMatrix`/`new ZoomMatrix` (`OWORLD.cpp:68-69`) from `ZOOM_*`. A runtime value is 0 at that
    point; 7kaa_HD re-creates both matrices in `Config::init_resolution` (`OCONFIG.cpp` diff).

## 3. Main map view: widen only the zoom window?

Yes. Concrete example, 1334x600 logical: `ZOOM_WIDTH = floor((1334-224)/32)*32 = 1088` (34 cells vs 18), `ZOOM_HEIGHT = 544` unchanged; panel at `x = 1088+10` (the original has the 10 px gutter plus a 224 px right bar).
This is the 7kaa_HD formula (`Config::init_resolution`).

What breaks / must be touched:
- Terrain/sprite draw bounds, fog, weather: handled by making `ZOOM_*` runtime and adding the `min(..., max_x_loc)` clamps above. Largest map is 200x200 and 34 < 200 so no issue on real maps; small-map clamps are for correctness.
- Minimap viewport rectangle: automatic via `cur_cargo_width = disp_x_loc` (`OWORLD.cpp:140`, `OWORLD_M.cpp:329`). Minimap itself must move with the panel (`MAP_X1` literal 588 -> `INFO_X1+2`).
- Scrolling limits / selection / cursor clamp: automatic or one-line fixes listed in 2(d). No game-state (simulation) code touches the view, so multiplayer sync and saves are unaffected, except that 7kaa_HD's commit log says it fixed "save loading stability": the matrices' dimensions are re-derived on load, so a load must call the re-init (verify).
- Top bar background: `MAINSCR` is one 800x600 picture (see `mainscr.png`): left tablet buttons x 0-262, a plain stone strip 262-580 carrying the food/cash/date text, then a right block 580-800 (map mode buttons, above the panel). Widening requires inserting dx = W-800 pixels of stone into the strip between 262-580 (the text drawn at 305-575 stays left-anchored) and shifting the right block with the panel.
  Fix: draw `MAINSCR` in three slices (left 0..~575, filler tiled/mirrored, right 576..799 at `x = W-224`), plus a plain stone fill for the black area under/behind the world view (that region is the white rectangle in the art; the world draws over it anyway).
  Moves with panel: map-mode buttons (`OWORLD_M.cpp:111,121,128`), `TOP_MENU_X2` (`OINFO.h:41`), the date field (`OINFO.cpp:393-460` literals 460/476/447/575) stays where it is if the filler is added to the right of it.
- The 10 px bottom border: 600 is not a multiple of 32 after 56, giving slack. 7kaa_HD fills it (`OWORLD_Z.cpp` `draw_frame` bar + `OINFO.cpp` cropped bottom-right slice).
- Menus and reports centred: **menus and full-screen screens: feasible, ~25 sites**, each reads `VGA_X1 = (VGA_WIDTH>>1)-400` and adds it to hard-coded coordinates (7kaa_HD's `OGAMEMP.cpp` diff is 555 lines for this) after clearing the whole screen black and drawing the 800x600 art at the centre offset (`OGAMMAIN.cpp:119-130` in HD). Mouse coordinates are absolute in the framebuffer so the `+X1` offset must be applied in both draw and detect code (buttons are created with the same coordinates, so it works if every `Button.create` argument is offset, as in HD).
  **Reports are the awkward case**: they draw in the zoom window, so they become 1088 wide. Either (i) keep them at 576 and centre them in the view, which needs an x-origin offset in ~11 report files, or (ii) re-flow (high effort). HD took a middle route: `#define` the X2 (`OR_ECO.cpp` diff) so reports stretch to the wide view (ragged but usable).

## 4. Prior art: kresimirv/7kaa_HD

- **What**: "HD resolution patch and UI fixes" on 2.15.8 (base 3f229fd, "Initial commit - 7kaa 2.15.8"), 31 commits, 2026-05-01 to 2026-06-24, single author, 1 star, 0 forks, 0 issues, 2 releases (Win x64 zip, Linux AppImage; no Android).
- **Licence**: repo keeps upstream `COPYING` (GPL v2, "7kaa trademark used with permission" note, `COPYING:4-7`); GitHub shows NOASSERTION (no SPDX). Derived from upstream GPL, so a derived Android patch is fine under the same licence; the 11.5 MB art file is not separately licensed (see below).
- **Resolutions**: any `vga_resolution_width/height` in `config.txt` up to 3180x2160 (README). Examples: 1280x720, 1920x1080, 2560x1440, 1280x1024, 1600x1200. Widescreen 16:9 is the main target, so 20:9 behaves the same way (formula is generic).
- **Changed files**: 63 in `src/` + `include/` (`git diff --stat 3f229fd HEAD`): `OVGA.h` (VGA_W/H -> `config.win_width/height`, new `VGA_X1/VGA_Y1`), `OWORLDMT.h` (runtime ZOOM_WIDTH/HEIGHT, MAP_X1 = ZOOM_WIDTH+10), `OINFO.h` (INFO_X1 = ZOOM_WIDTH+10, TOP_MENU_X2 = VGA_WIDTH-4), `OCONFIG.cpp/h` (`init_resolution`), `ConfigAdv.cpp/h` (reads the two config keys), `OIMGRES.cpp` (`put_cropped_area`), `OVGABUF.cpp` (`put_large_bitmap(..., useStretch)`), `IB2.cpp/IB_A.cpp` (scalable blit in `imgfun/generic`), `OWORLD_Z.cpp` (clamps, lightning, frame fill), all `OR_*.cpp`, menus (`OGAMEMP.cpp` +555, `OGAMSING.cpp` +194, `OGAMENCY/OGAMEND/OGAMMAIN/OGFILEA/OOPTMENU`), tutorial (`OTUTOR*.cpp`), firm/town info with enums (`OFIRMIF2, OF_HARB, OF_INN, OTOWNIF`), `OMOUSECR.h` (arrays), `LocaleRes.cpp`.
- **Panel**: panel stays fixed 224 px wide and glued to the right edge (`INFO_X1 = ZOOM_WIDTH+10`). Top bar: "cropped HUD".
- **Full-screen art**: for resolution > 800x600 it ships a new **`I_IF_3840_2160.RES` (11.6 MB, a 3840x2160 re-draw of MAINSCR)** and crops it: top bar [0..zoom_width) from the left 3616 px, right block 224 px from x=3616, right strip, and bottom-right corner (`OINFO.cpp` `disp_panel` diff: `put_cropped_area`), so it simply crops a huge stone background (no filler logic). Menus/main-menu: black background + the original 800x600 image centred (`OGAMMAIN.cpp` diff). Load/Save centred.
- **Maintenance**: last commit 2026-06-24, no open issues, one maintainer. Treat as a one-shot reference; it targets 2.15.8 (the Android port is on 2.15.7).
- **Applicability to 2.15.7**: 51 of 63 touched files are byte-identical between 2.15.7 and the HD base; the 12 that differ (`ConfigAdv.h/.cpp, OFIRM, OF_HARB, OGAMCRED, OGAMEMP, OSYS, OSYS2, OTALKRES, OTOWNIF, OVGA, OWORLD`) are 2.15.7 -> 2.15.8 upstream edits and need hand-merging. So: cherry-pick the diff as a patch series and rebase. But note Android needs different decisions: HD leaves SDL logical size and an OS window; patch 0007 does manual letterbox from `VGA_WIDTH/HEIGHT` and already works with a different logical size; `sk_touch.cpp:45-46` (`GAME_W=800, GAME_H=600`) must read `VGA_WIDTH/HEIGHT`.
- **Other efforts**: `starportx/7kaa` fork (2019, last push 2019-04-27): "Configurable HD mode, new HD .res for SD768/HD1080, scalable blit" (HD's stated predecessor); `sraboy/skaa_editor` (resource editor, 2016). 7kfans news page and the Steam "fullscreen" guide only cover window scaling; PCGamingWiki lists widescreen as unsupported at 800x600. I found no official or SourceForge widescreen branch (a web search only; forum threads at 7kfans.com were not exhaustively searched).

## 5. Assets that can't stretch and options

| asset | issue | cheapest fix |
|---|---|---|
| `MAINSCR` (I_IF.RES) | top bar strip 262-580 and panel/background; bottom/right frame | split + mirror-tile 100-300 px of the stone strip (mirror avoids seams) via a small Python script at build time -> **new ~1356x610 RES record**, or compute at runtime from 800x600 original (`put_bitmap_area` of columns, flipped). 16 B of code vs 11.6 MB asset; choose runtime mirror. |
| `M_MAIN`, `OPTIONS`, `SCENARIO`, `TUTORIAL`, `ENCYC`, `I_MENU(2)` x14 | full-screen 800x600 art | centre on a black or stone-filled backdrop (HD does black). Optionally fill margins with a darkened tile / blurred edge for polish. |
| `LOADGAME/SAVEGAME/GAMEMENU` (dialogs) | fixed-size | just centre (they are not full screen). |
| `OGAMCRED`/title/`CHINESE.ICN`... | 800x600 ICN frames | centre as above. |
| zoom background | none | world is drawn tile-by-tile, no art needed. |

Mirror-tile for stone is acceptable because it is a noise texture; the hard-edged bezel lines at y=48-56 (top) and the frame lines need to be preserved by tiling a one-tile slice at the right phase.

## 6. Scale and aspect on 2712x1220

| logical | scale | notes |
|---|---|---|
| **1356x610** | **exactly 2.000x** (2712/1356, 1220/610) | integer, crisp pixels; zoom = floor((1356-224)/32)*32 = 1120 (35 cells), height slack 10 px (610-56-544), plus 12 px width slack |
| 1334x600 | 2.033x (height-limited), 2711 px wide | the study example; fractional scaling blurs with linear filter (`OVGA.cpp:122` hint) |
| 1344x608 | 2.007x | zoom exactly 35x17 cells, near-integer, almost no slack |
| 1088x600 | 2.033x, 2212 px wide (250 px margin each side) | 34x... view of 27 cells; **keeps ~250 px side margins for the toolbar / cheats panel** |

Recommendation: grow BOTH (1356x610 at exact 2.0x). Pure width-only (1334x600) works but the 2.033 scale makes 1-px lines uneven and
the 34 extra rows of vertical slack are irrelevant (the map view is already 17 rows). The extra 10 rows of height only add a filler strip.
Use nearest-neighbour or the existing linear filter at exact 2x (looks fine either way). Cell size on screen = 64 px (vs 65 today), so tap targets/units stay the same.

## 7. Approaches

### A. Widen the world view only, panel fixed, menus centred (recommended core)
- Scope: runtime `VGA_WIDTH/HEIGHT`, `ZOOM_*`, `INFO_X*`, `MAP_X1`; matrices built after config; convert enums/arrays; clamp fixes; split-draw `MAINSCR`; centre menus via `VGA_X1/Y1`; reports pinned (either 576 centred or stretch).
- Pros: matches the actual goal (fullscreen gameplay, 2x more map on screen); keeps simulation/save code untouched; panel layout and art unchanged; proven by HD fork.
- Cons: ~25-30 menu sites with absolute coordinates (the long tail); report columns; 12 files to hand-merge from 2.15.8; static-init ordering must be right; any missed absolute coordinate gives a mis-positioned control (visual, not crash, but `err_when` asserts for `x2>=VGA_WIDTH` in debug).
- Effort: **~14-22 agent-hours** (core HUD ~6-8 h; centred menus ~5-8 h; reports ~2-4 h; tests/emulator iteration ~3 h; sk_touch GAME_W/H and the letterbox math ~1 h).
- Risk: medium. Bugs are visual and local; the failure mode is a mis-centred dialog. Biggest single risk: some menu not centred so the mouse hits the wrong place.
- Toolbar and cheats panel: the margins disappear (at 1356x610 the image fills the screen). Toolbar must become an overlay drawn over the world view (the strip mode already exists, `sk_touch.cpp:78`, `TOOLBAR_MIN_MARGIN_FRAC` logic at `:36`), semi-transparent, or sit over the top bar's empty stone filler (it is exactly where the 540 px of new filler lives, a natural home). Cheats panel: a slide-in overlay on the left edge, over the map. Both need to be re-laid-out in `sk_touch.cpp` and the new `sk_cheats.cpp` (not present in the repo yet) by the ratio `GAME_W/H = VGA_WIDTH/HEIGHT`.

### B. Port 7kaa_HD (rebase its patch series onto 2.15.7)
- Same as A in outcome but taking its code verbatim: bigger blast radius (1,164/779 lines, 63 files), configurable up to 3180x2160, 11.6 MB art, LocaleRes.cpp changes, Windows/AppImage build scripts irrelevant.
- Pros: tested, saves the discovery work; fastest way to a first picture; already includes the report/menu fixes.
- Cons: based on 2.15.8 (12 conflicting files, plus the port's own patches `0002/0007` touch `OVGA.cpp` too); uses a `config.win_width` global plus "2000-byte" arrays and macro `#define`s without parentheses (`OR_ECO.cpp`: `#define X2 ZOOM_X2 - 6`, precedence hazards); uses a 3840x2160 art file (needs licensing review, size 11.6 MB in the APK); designed for desktop SDL (window), lacks Android specifics; no maintainer.
- Effort: **~8-14 agent-hours** (rebase ~4-6 h, bug fixes ~2-4 h, Android integration incl. sk_touch ~2-4 h).
- Risk: medium-low technically, but undocumented by-product bugs; HD's later "fix crashes at high resolutions, save loading" commits suggest rough edges.
- Toolbar/cheats: same as A.

### C. Cheap fakes
- C1 "extended background": keep 800x600 game; fill the letterbox margins with blurred/stretched/darkened copy of the frame or stone texture in `Vga::flip` (patch 0007). 2-4 h, risk very low. No new gameplay area; toolbar and cheats panel stay exactly as designed in the margins. It does not solve "fullscreen gameplay".
- C2 stretch to fill: 2.2/1.33 = 1.67x horizontal distortion; ugly, makes touch precision unequal. Not recommended.
- C3 zoom-crop: scale so width fills (3.39x), 360 of 600 rows visible; cuts off the panel. Not viable.
- C4 half-measure A-lite: widen the framebuffer to ~1088x600 and run A (smaller margins kept for toolbar/cheats): see §6; same work as A.

## 8. Recommendation

**Do A, borrowing from B**: start from the 7kaa_HD diff for the mechanical parts (runtime `ZOOM_*`/`VGA_*`, `init_resolution`, clamps, `OMOUSECR` arrays, `put_cropped_area`), but
(1) pick logical **1356x610** (exact 2.0x on 2712x1220), keep the panel 224 px, ZOOM 1120x544 (35x17 cells);
(2) **do not** ship the 11.6 MB 3840x2160 art; synthesize the wide `MAINSCR` at runtime from the existing one (mirror-tile the stone strip), written as a small build-time or load-time function;
(3) keep menus 800x600 centred on black via `VGA_X1/Y1`, and pin reports at 576 px centred in the view (or accept the stretched HD behaviour first and refine later);
(4) re-seat the toolbar over the new top-bar filler / left-edge overlay and the cheats panel as a slide-over, and fix `GAME_W/H` in `sk_touch.cpp`.
Stage it: (i) phase 1, in-game HUD only, gating a config switch, menus still 800x600 centred by simple pixel offset in `flip`-level (blit whole 800x600 for menus), to reach a playable demo in ~8 h; (ii) phase 2 convert menus/reports. A **flip-level shortcut for menus** is worth trying: render menus into an 800x600 buffer and have `Vga::flip` centre it (only valid if `VGA_WIDTH` is switched at the menu/game boundary, i.e. re-init the surface when a game starts). That would avoid the ~25-site coordinate conversion; cost is a surface re-create on entering/leaving a game (`OVGA.cpp:131-163`) and mouse-offset in `sk_touch`; unproven, test first.

Open questions: (1) does 7kaa read the saved view dims on load (HD's "save loading" fix suggests yes); (2) multiplayer lobby screens (`OGAMEMP.cpp`) are irrelevant for a single-user phone port and could be left 800x600 (likely unreachable); (3) LocaleRes.cpp diff in HD (font/locale) not analysed; (4) fractional scale / low-res handling of 2x nearest on 20:9 devices of other sizes (use the same rule: keep height 610 and pick W = round(ratio*610), zoom = floor((W-224)/32)*32).
