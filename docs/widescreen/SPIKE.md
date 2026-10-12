# Widescreen spike: HD fork onto our 2.15.7 Android port (measured)

Scratch: `hd-spike/` (`src7k` git repo: pristine -> hd-on-2.15.7 -> android-patches -> spike-force-1356x610; `port/` = build copy; `hd.diff`; logs).
All times are agent wall-clock for the measured step (total ~6 min incl. two full native builds). Human time is higher; see estimate.

## Measured results
| Step | Result | Hunks / counts | Time |
|---|---|---|---|
| 1 Setup / HD diff | `hd.diff` 4397 lines, 63 files, +1164/-779. No files added or deleted (so no new sources for CMake). | desk numbers verified: 51 files byte-identical in 2.15.7, 12 differ (exactly the listed 12; OWORLD.cpp is the 12th, the doc lists it too) | 1 min |
| 2 Apply HD to 2.15.7 | **All 63 files apply clean with plain `patch -p1`: 0 rejects, 0 fuzz, 0 offsets**, also `git apply --check` passes. Includes all 12 files that differ upstream 7->8. | 0 conflicting hunks. Upstream 7->8 deltas in those 12 files are 1-60 lines each and never overlap HD's hunks (residual vs HD HEAD = exactly the upstream 7->8 delta, i.e. we keep 2.15.7 behaviour there) | 1 min, nothing to resolve |
| 3 Our patches 0001-0008 on top | 7 of 8 apply clean. **1 hunk fails**: 0004 hunk 1 (`#include <sk_touch.h>` after `ConfigAdv.h`) because HD added `#include <OCONFIG.h>` there. 0005 hunk 1 cascades (it needs 0004's include). Both = trivial context drift, fixed by adding the two `#ifdef __ANDROID__ #include` lines by hand (1 edit). | 2 hunks failed of ~25 across patches, same root cause | 3 min |
| 4 Compile (clang/NDK 27, arm64) | **Compiles, links, APK built.** First attempt: 1 failing TU (`OGAMMAIN.cpp`), 44 errors, all one class: `-Wc++11-narrowing` (int in `short` brace-init, e.g. `{6+SWORD1_X,...}` now runtime because `SWORD1_X` is `(VGA_WIDTH>>1)-142`). Fix = one CMake flag `-Wno-c++11-narrowing` (HD builds with GCC where it is a warning). 320 of 321 other TUs compiled first try. After fix: 0 errors, `BUILD SUCCESSFUL`. | errors 44 -> 0; all 44 classified macro/precedence-adjacent HD-on-compiler mismatch, 0 Android-specific, 0 missing-file, 0 HD-on-2.15.7 mismatch | ~3 min (clean native build of SDL+openal+game ~190 s) |
| 4b Forced 1356x610 | `Config::init_resolution` (called from `AM.cpp:329` after `config_adv.init`, before `Vga::init`) overridden under `__ANDROID__`: 3 lines. | | 1 min |
| Compile errors | before fixes 44 (1 file) / after 0 | | |

**Result: it compiles.** APK: `hd-spike/port/android/app/build/outputs/apk/debug/app-debug.apk` (103 MB; includes HD's `I_IF_3840_2160.RES` 11.6 MB staged into the copy's data). **Not run on a device (adb off-limits), so zero runtime evidence**: no crash/no-crash, no picture.

## 3840x2160 art: where it is used, what happens without it
- `OSYS.cpp:425` (Sys::init_objects): any resolution above 800x600 picks `I_IF_3840_2160.RES` instead of `I_IF.RES` for `image_interface`.
- Missing file: `ResourceIdx::init` -> `File::file_open(name)` with default `handleError=1` (`OFILE.h:50`, `OFILE.cpp`) -> `err.run("error opening file ...")`. **Fatal error at boot, no fallback, no blank screen.**
- Decoded both RES files: HD's file has the same 22 records as `I_IF.RES`; **only MAINSCR differs** (3840x2160, 8.3 MB vs 800x600). Everything else in it (M_MAIN, buttons...) is identical. So the 11.6 MB file is a pure MAINSCR swap.
- Only consumer: `Info::disp_panel` (`OINFO.cpp:262-300`), 4x `ImageRes::put_cropped_area` (`OIMGRES.cpp:281-341`), each call allocates `new char[8.3MB]` and reads the whole image from storage then copies a strip. 4 calls per panel repaint = ~33 MB of IO+alloc per repaint: a latency risk on a phone (unmeasured).

## Static review
**sk_touch.cpp / sk_cheats.cpp 800x600 sites.** `sk_touch.cpp`: `GAME_W/GAME_H` defined at `:47-48`, used at `:103,104,106,107,119 (x2),121,123` = 8 lines / 10 uses, all in `get_geometry` and `to_logical`. Comments at `:6,:33` (slop 12 px is "game pixels", fine). `SCROLL_PX_PER_CELL=32` stays valid. `sk_cheats.cpp`: **0 hard-coded sites**; it takes `SkCheatsView` (out size, game_x, game_w). `sk_android_video.cpp:12,19` is a log string. No 800/600 in Java/XML. Patch 0007 `flip()` already uses `VGA_WIDTH/HEIGHT`. So retargeting = replace the two constants with `VGA_WIDTH/VGA_HEIGHT` (~10 lines).
**Toolbar / cheats when margins vanish.** Both already have a no-margin mode: toolbar = collapsible strip with handle at (0,0) sized 0.11*out_h (`layout_items`, `sk_touch.cpp:~160-190`), cheats = left overlay panel (`compute_layout`, `sk_cheats.cpp:115-125`). At 1356x610 on 2712x1220 `gx`=0 so `g.side` is false and both fall into those modes automatically. Problem: handle at (0,0) is 134 physical px = 67 logical, bigger than the 56 px top bar and sits on the game's own top-left menu buttons. Better home: the top-bar stone filler between the date text (ends ~x=575) and the map-mode buttons (`MAPMODE_X1`), i.e. logical x ~590..1100, y 0..56. The strip then opens rightwards over the filler. Cheats overlay on the left over the map is fine. Estimate 2-3 h with on-device tuning.
**Top bar from original MAINSCR at runtime.** Exact place: `Info::disp_panel` (`OINFO.cpp:262-300`), replace the 4 `put_cropped_area(... "MAINSCR", 3616 ...)` calls and revert `OSYS.cpp:425` to always `I_IF.RES` (drops the 11.6 MB file). New work: (a) left 0..~576 of original top bar copied 1:1; (b) filler = mirror-tile of a ~270 px stone slice (e.g. src x 262..575) to cover `zoom_width-576` px (544 at 1120) -> needs a small `put_cropped_area` variant with horizontal flip + wrap (~40 lines, reuse `OIMGRES.cpp:293`), bezel lines at y 48-56 must keep phase; (c) right 224 px block at `zoom_width` (src x 576..799); (d) bottom 10 px border across the full width (also mirrored), plus the right-edge slack (below). Cache the decoded 800x600 once to avoid per-repaint reads. Estimate 3-5 h incl. visual iteration (art tuning, not code, dominates).
**Unparenthesised macros.** HD adds/changes 205 `#define` lines; **129 distinct macros have an operator in the body and no parentheses** (`ZOOM_X1 + 6`, `ZOOM_X2 - 6`, `INFO_X1 ZOOM_WIDTH+10`, `INFO_X2 ZOOM_WIDTH+10+204`, `*_BROWSE_Y1 X + 6` ...; they replaced upstream `enum` constants, which were safe). Scripted scan for uses after `-`,`*`,`/`,`%` or before `*`,`/`,`%`: **8 live hazardous sites** (+3 in comments):
`OR_NAT.cpp:680-681` `(REPORT_DET_X2-REPORT_DET_X1)/2` (= (Z2-6-Z1+6)/2, wrong by 6 px -> detail panel offset), `OGFILEA.cpp:97-98` `SCROLL_WIDTH/HEIGHT = X2-X1+1` (off by 12 -> save-file scroll area), `OGAMSCE2.cpp:297` buffer size `(SCROLL_X2-SCROLL_X1+1)*(SCROLL_Y2-SCROLL_Y1+1)` (wrong size: over- or under-alloc; the under case is a heap overflow risk), `OINFO.cpp:299` `(INFO_X2-INFO_X1+1)` = 225 vs 205 (over-alloc, harmless), `ONEWS.cpp:358` `> NEWS_X2-NEWS_X1+1` (news text-fit off by 24). Fix = parenthesise all 129 bodies (mechanical, sed-able, 1 h incl. rebuild). My scan misses uses through other macros' bodies (macro-in-macro), so treat 8 as a floor.

## Other findings from the code (not runtime-tested)
- **Resolution choice: 1356 is wrong, use 1344x610.** HD's layout needs `width = zoom_width + 224` with zoom_width a multiple of 32. At 1356: zoom=1120, panel art ends at 1344, **12 px strip at the right edge never painted** (black), and `MAPMODE_X1 = VGA_WIDTH-220` = 1136 vs panel at 1124 (12 px off), `TOP_MENU_X2 = VGA_WIDTH-4` also drifts. 1344x610 is exact; scale 2.0x -> 2688x1220 on 2712x1220 (12 px black each side, still no toolbar margin). 
- `INFO_Y1=265`, `INFO_Y2=589` stay absolute (panel is 590 tall, fine at 610; 10 px bottom filler needed).
- Menus: HD draws 800x600 art centred (`VGA_X1/VGA_Y1`, 5 px vertical slack at 610). Mouse/touch hit testing relies on every `Button.create` being offset; untested.
- Static-init: only `OSPRITE2/OUNITM` static arrays use `ZOOM_LOC_*` (constant 32), fine; `world` matrices re-created in `init_resolution` (HD design). Save/load re-init path unverified.
- `-Wno-c++11-narrowing` hides real narrowing (short coordinates); fine for <=3840 but is a latent overflow if resolution grows past 32767 (not a concern).
- Cost: 11.6 MB extra asset + 33 MB read per panel repaint if the 3840 art is kept.

## Revised estimate (human-hours, focused)
| Work item | Desk | Revised | Basis |
|---|---|---|---|
| Merge HD onto 2.15.7 | 4-6 | **0.5** | measured: clean, 0 conflicts |
| Android patches | part of 2-4 | **0.5** | 1 trivial include conflict |
| Compile | included | **0.5** | 1 flag; 44 errors one class |
| Macro parenthesisation + hazard fixes | not budgeted | **1.5** | 129 macros, 8+ bad sites |
| Top-bar art (runtime mirror) | 2-4 | **3-5** | code small, art iteration large |
| Resolution/edge fixes (1344, right slack, map-mode x) | not budgeted | **1-2** | |
| Touch/toolbar/cheats re-layout | 2-4 | **2-3** | handle placement + `GAME_W/H` |
| On-device iteration (menus, reports, save/load, scroll, fog) | ~3 | **6-10** | zero runtime data yet; reports/menus ragged; dominant risk |
| **Total** | **8-14** | **~15-23 h** (merge+compile = 1.5 h, rest is device/visual) | |
Confidence: high (90%) on merge/compile (measured), low-medium (50%) on the 15-23 h total, because nothing was run on a device.

## Top 3 surprises / risks
1. Merge cost is ~0: the whole HD diff applies to 2.15.7 with no fuzz at all; the desk "12 files need hand-merging" was pessimistic. The real cost has moved to runtime verification.
2. The 3840x2160 file is a MAINSCR-only swap and a missing file is a hard boot error (`err.run`); each panel repaint re-reads 8.3 MB four times. Runtime mirror-tiling (drop the file) is clearly better but is the main visual-iteration item.
3. HD's macro rewrite (129 unparenthesised macros replacing safe enums) already produces wrong sizes at 8+ sites incl. one allocation size; and 1356 wide leaves a 12 px unpainted stripe + misaligned map-mode buttons, use 1344.

## Recommendation
**Go with the HD route** (at 1344x610): merge+compile cost is near zero and it hands us 63 files of resolved layout work (reports, menus, clamps) that a from-scratch route would have to rediscover. Gate: first run the scratch APK on the phone and look (menus, reports, scroll, save/load) before committing to the 15-23 h, since the unmeasured risk is all runtime. Do not take the 3840 art; do the runtime mirror bar. From-scratch is not justified.
