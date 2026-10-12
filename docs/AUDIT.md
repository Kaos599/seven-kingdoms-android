# 7kaa 2.15.7 portability audit (arm64 Android)

Phase 0 deliverable (CONTRACT.md section 5). Read-only audit of `third_party/7kaa` (official 2.15.7 tarball, MD5 verified).
Every claim cites `file:line` relative to `third_party/7kaa/` unless a different root is given. Line numbers were
taken from the extracted tree on 2026-10-12.

Baseline: the game builds today for Linux x86-64 with autotools (`configure.ac`). The Android port needs an arm64
build of the same sources. Two facts shape everything below:

- arm64 Android (Bionic) and x86-64 Linux share the LP64 data model and little-endian byte order, so most
  integer/pointer assumptions carry over unchanged.
- `char` is unsigned on arm64 and signed on x86, and the game's configure step enforces signed `char`.

---

## A. Source inventory

| Group | Files | Lines | Where |
|---|---|---|---|
| Game sources, top level (`src/*.cpp`) | 281 | 200,159 | all 281 are listed in `src/Makefile.am:8-288` (`7kaa_SOURCES`) |
| Generic image routines (`src/imgfun/generic/*.cpp`) | 39 | | `src/imgfun/generic/Makefile.am` |
| OpenAL audio (`src/openal/*.cpp`) | 2 | | `src/openal/Makefile.am:2` |
| enet multiplayer wrapper (`src/enet/multiplayer.cpp`) | 1 | 1,464 | `src/enet/` |
| libcurl wrapper (`src/curl/WebService.cpp`) | 1 | 117 | `src/curl/` |
| x86 assembly (`src/imgfun/x86/*.asm` plus 3 `.inc`) | 45 (+3) | 7,690 (asm+inc) | `src/imgfun/x86/Makefile.am` |
| Headers (`include/*.h`) | 188 | 26,884 | `include/` (there are no headers under `src/`) |

- Total `.cpp` under `src/`: 324 (281 top level + 43 in subdirectories). Total `.cpp`+`.h` text under `src/`: 206,815 lines.
- Data tree shipped in the tarball (`data/`): 102 MB, nine folders (`ENCYC`, `ENCYC2`, `IMAGE`, `RESOURCE`, `SCENARI2`,
  `SCENARIO`, `SOUND`, `SPRITE`, `TUTORIAL`). Extensions: 107 `.RES`, 97 `.SPR`, 84 `.ICN`, 84 `.COL`, 38 `.SCN`, 38 `.SCP`,
  38 `.SCT`, 13 `.TUT`, 5 `.WAV` (in `SOUND/`).
- Music is NOT in the tarball. It is in `third_party/7kaa-music/MUSIC/` (10 `.WAV`, 47 MB).

### Source groups compiled into the game binary

Source of truth: `src/Makefile.am` and `configure.ac`.

| Library / binary | Sources | Condition | Evidence |
|---|---|---|---|
| `7kaa` executable | 281 top-level `.cpp` | always | `src/Makefile.am:6-288` |
| `libimgfun_generic.a` | 39 `.cpp` | when `USE_ASM` is not set (default) | `src/Makefile.am:312-316`, `src/imgfun/Makefile.am:1-5` |
| `libimgfun_x86.a` | 45 `.asm` (JWASM) | only with `--enable-asm` | `src/Makefile.am:312-316`, `configure.ac:118-121`, `configure.ac:192-199` |
| `libaudio_openal.a` | `openal_audio.cpp`, `wav_stream.cpp` | `AUDIO_OPENAL` (always on) | `src/Makefile.am:304-306`, `src/openal/Makefile.am:2` |
| `libmultiplayer.a` | `src/enet/multiplayer.cpp` | always built, but multiplayer is turned off by configure (see section I) | `src/Makefile.am:309` |
| `libWebService.a` | `src/curl/WebService.cpp` | only if `USE_CURL` | `src/Makefile.am:2-4`, `src/Makefile.am:318-320` |
| Windows resource | `ico.rc` | `USE_WINDOWS` only | `src/Makefile.am:322-324` |

Subdirectory `SUBDIRS` is `enet imgfun openal` plus `curl` when `USE_CURL` (`src/Makefile.am:1-4`).

### Platform-specific source

- **Windows-only:** `src/ico.rc` (`src/Makefile.am:322-324`).
- **Compiled out unless `ENABLE_INTRO_VIDEO` is defined:** `src/OVIDEO.cpp` (the whole file is under `#ifdef ENABLE_INTRO_VIDEO` at line 25),
  its header `include/OVIDEO.h`, and the DirectShow includes in `src/AM.cpp:27-30`, `AM.cpp:74`, `AM.cpp:135`.
  `ENABLE_INTRO_VIDEO` is not defined by `configure.ac`, so the intro movie code is empty in every current build.
- **Files that include Windows headers** (`windows.h`, `WINAPI`, `HINSTANCE`): `src/ico.rc`, `OERROR.cpp`, `OVGA.cpp`,
  `OVIDEO.cpp`, `OMISC.cpp`, `misc_uuid.cpp`, `OGFILEA.cpp`, `ODIR.cpp`, `OLOG.cpp`, `include/OVIDEO.h`. The Windows blocks sit behind
  `USE_WINDOWS` or `ENABLE_INTRO_VIDEO` guards. Not every guarded block was read.
- **Linux/POSIX-only blocks:**
  - `src/ODIR.cpp:29` (`USE_WINDOWS`, FindFirstFile) versus `src/ODIR.cpp:32` and `src/ODIR.cpp:104-106` (`USE_POSIX`, `glob`).
  - `src/OMISC.cpp:1254-1270` (`misc_mkdir`, Windows branch at `OMISC.cpp:1256`).
  - `src/misc_uuid.cpp:42` and `:49` (POSIX `/dev/urandom` at lines 99-101).
  - `src/OSYS.cpp:146` (`_NSGetExecutablePath`, macOS only through `HAVE__NSGETEXECUTABLEPATH`).
  - `src/OSYS.cpp:2844-2855` (`/proc/self/exe`) is inside `PACKAGE_DATA_WITH_EXE`, which no build file defines, so this branch is dead.
  - `src/OVGA.cpp:1054-1065` (`SDL_GetDisplayUsableBounds`), and the DPI and `SHCORE.DLL` code at `OVGA.cpp:998-1014` (Windows only).

---

## B. x86-specific code

Search patterns: `__asm`, `asm(`, `asm volatile`, `_asm`, `.S`/`.asm` files, `x86intrin.h`, `emmintrin.h`, `xmmintrin.h`,
`immintrin.h`, `__i386__`, `__x86_64__`, `_M_IX86`, `_M_X64`, `__cpuid`, `rdtsc`, `cpuid`.

Hits in game C++ (4 lines, all inline assembly):

| Location | Form | Function |
|---|---|---|
| `src/OFLAME.cpp:341` | `_asm { ... }` (MSVC) | `Flame::rise` |
| `src/OFLAME.cpp:473` | `__asm__ __volatile__ (...)` (GCC/Clang, i386 registers `%ecx`, `%esi`, `%ebx`, `pushl`) | `Flame::rise` |
| `src/OWORLD.cpp:2070` | `_asm { ... }` (MSVC) | `World::process_visibility` (`OWORLD.cpp:2050`) |
| `src/OWORLD.cpp:2086` | `__asm__ __volatile__ (...)` (GCC/Clang, `movb %0,%%ah`, `%%ebx`, `%%edx`) | `World::process_visibility` |

Guards (all four are inside `USE_ASM`):
- `src/OFLAME.cpp:240` `#ifndef USE_ASM`: the C++ fallback runs from line 241. `:305` `#else` starts the asm branch. `:340` `#ifdef _MSC_VER` and `:471` `#else` give the GCC inline asm.
- `src/OWORLD.cpp:2055` `#ifndef USE_ASM` gives the C++ fallback. `:2064` `#else` starts the asm branch. `:2069` MSVC and `:2085` GCC. `:2100-2101` close the blocks.

**Consequence:** the default build (`--enable-asm` not given, `configure.ac:118-121` defaults `enable_asm=no`) compiles only the
C++ fallbacks. `include/config.h.in:121-122` leaves `USE_ASM` undefined by default.

Other x86 items:
- `src/imgfun/x86/` (45 `.asm`, `ALL.inc`, `COLCODE.inc`, `IMGFUN.inc`) is built with JWASM (`src/imgfun/x86/Makefile.am`,
  `.asm.o` rule). `configure.ac:192-199` fails the configure step if `jwasm` is missing. JWASM is an x86-only assembler.
- `include/asmfun.h:27-33` defines `IMGcall` as `__attribute__((stdcall))` under `USE_ASM`, which is an x86 calling convention.
- Symbol parity: `src/imgfun/x86/` has `I_CTRL`, `I_XOR`, `IB_ATM`, `IB_TM1`, `IB_TM2`, `IB_TM3` with no generic `.cpp` counterpart.
  No reference to these names exists in `src/` or `include/` outside `imgfun/x86`, so the generic set is complete for the non-asm build.
- Not found: `.S` files (0), `x86intrin.h`/`emmintrin.h`/`immintrin.h`/`xmmintrin.h` (0), `__cpuid`/`rdtsc` (0), `__i386__`/`__x86_64__`/`_M_IX86`/`_M_X64` (0).

x86 FPU flags (`-mfpmath=387 -ffloat-store`) are covered in section I and risk J4. They are not asm, but they are x86-only.

---

## C. Pointer-size and type assumptions

**Data model.** x86-64 Linux (SysV) and arm64 Android (AAPCS64/Bionic) are both LP64: `int` 4, `long` 8, pointer 8, little-endian.
Layout assumptions that hold on the Linux build therefore hold on Android. Only a Windows (LLP64) build would diverge, and that is out of scope.

- `sizeof(long)` as a fixed width: `src/OMEM.cpp:41` (`CHK_VAL_SIZE`) with `*((long*)allocPtr)` guard words at
  `src/OMEM.cpp:127`, `:190`, `:304`, `:344`. Harmless on LP64.
- `src/OSYS3.cpp:72` tests `file_size() >= sizeof(long)*2` (16 bytes on LP64) for the seed-file header. Same on both LP64 targets.
- Pointer-to-integer casts: a targeted search for `(int)`/`(long)`/`(DWORD)` applied to pointers (`this`, `*Ptr`, `ptr`, `p`, `buf`) found none.
  The `(int)` casts in the codebase (for example `src/OFIRM.cpp:1029`, `(int) unitPtr->hit_points`) apply to struct value fields.
  `src/OSTR.cpp:142` `(int)(tmp-str_buf)` is a pointer difference into a bounded buffer and is safe.
- `sizeof(void*)`: no uses. `uintptr_t`/`intptr_t`: no uses in `src/` or `include/`.
- `#pragma pack(1)`: `src/OFONT.cpp:52-73`, and 16 headers: `include/OB_HOMIN.h:32`, `OSITE.h:47`/`:84`, `OMATRIX.h:71`,
  `OREBEL.h:45`, `OF_WAR.h:40`, `OFIRMDIE.h:64`/`:87`, `ODB.h:38`, `OSPY.h:54`, `OGFILE.h:39`, `OF_FACT.h:36`, `OB_FLAME.h:34`,
  `OUNIT.h:193`/`:213`, `OTECHRES.h:87`/`:116`, `OF_MONS.h:46`. Packing is ABI-identical on both targets, but packed members can be
  misaligned (risk J5).
- Raw struct I/O: 117 call sites pass `&x` to `file_write`, `file_read`, `fwrite` or `fread` (grep count). Save files are binary
  dumps. Their layout depends on natural alignment plus `pack(1)` only, so saves should be compatible between an x86-64 and an arm64 build
  of the same sources. This is inferred from the ABI, not tested.

**`char` signedness (critical).**
- `configure.ac:27-41` checks that the compiler treats `char` as signed. It adds `-fsigned-char` (line 36) and aborts if `CHAR_MIN == 0`
  (`AC_MSG_ERROR` at line 39).
- `src/Makefile.am:297` puts `GLOBAL_CFLAGS` (with `-fsigned-char`) into `AM_CXXFLAGS` for the game sources.
- Colour and palette values stored in `char` that exceed 127 are sign-sensitive: `src/OBUTTON.cpp:290`
  (`color_light = (char) V_WHITE`), `src/OU_CARA.cpp:1487` and `:1491` (`color_down`), `src/OFIRMA.cpp:543` and `:562`,
  `src/OTORNADO.cpp:376-395` (`(char)TORNADO_COLOR*` writes). `include/OFONT.h:50` (a commented-out `#define`) documents nation colour codes `0xF0`-`0xFF` in text.
  The field types of `color_light`/`color_down` were not inspected; they should be confirmed.
- Unsigned-by-default arm64 Clang breaks this silently unless `-fsigned-char` is applied to every game source (CONTRACT section 2 already requires it).

---

## D. File I/O

### D1. Data directory discovery

Entry: `Sys::set_game_dir()` at `src/OSYS.cpp:2887` (called from init at `src/OSYS.cpp:221`), which calls `Sys::chdir_to_game_dir()`
at `src/OSYS.cpp:2823`. The search order is:

1. The current directory, probed for `IMAGE/HALLFAME.ICN` (`OSYS.cpp:2823-2832`).
2. The `SKDATA` environment variable, `getenv("SKDATA")` at `OSYS.cpp:2834`, then `chdir()` to that path.
3. `/proc/self/exe`'s directory (`OSYS.cpp:2844-2855`). Dead in this build, see section A.
4. macOS bundle `../Resources` (`OSYS.cpp:2859-2866`, from `_NSGetExecutablePath` at `OSYS.cpp:146`).
5. `PACKAGE_DATA_DIR` through `chdir()` (`OSYS.cpp:2868-2873`), compiled in only when `USE_FHS` (`configure.ac:133-136` defaults to yes;
   `src/Makefile.am:292-295` defines `PACKAGE_DATA_DIR` and `LOCALE_DIR`).

After the directory is chosen, `set_one_dir()` (`OSYS.cpp:2925-2941`) checks each subfolder by probing a known filename. Everything else
is opened by a relative path, so the game depends on the process working directory.

`PATH_DELIM` is `"/"` on POSIX (`include/GAMEDEF.h:43-46`). `DIR_RES`, `DIR_SPRITE`, `DIR_SOUND` are relative literals (`GAMEDEF.h:53-55`).
`DIR_IMAGE`, `DIR_MUSIC`, etc. are `sys.dir_*` strings (`GAMEDEF.h:56-64`).

Relevance to CONTRACT: `SKDATA` is the intended hook. Set it before `Sys::init` (`OSYS.cpp:221`) and `chdir_to_game_dir` will honour it.

### D2. Config, saves and logs

`Sys::set_config_dir()` at `src/OSYS.cpp:518-545`:
- `getenv("SKCONFIG")` (`OSYS.cpp:523`), else `SDL_GetPrefPath(CONFIG_ORGANIZATION_NAME, CONFIG_APPLICATION_NAME)` (`OSYS.cpp:531`).
  The names are `"7kfans.com"` and `"7kaa"` (`include/GAMEDEF.h:49-50`).
- The directory is created with `misc.mkpath` (`OSYS.cpp:539`, implemented in `src/OMISC.cpp:1275`).
- SDL2's Android `SDL_GetPrefPath` returns `SDL_AndroidGetInternalStoragePath()` (`SDL2/src/filesystem/android/SDL_sysfilesystem.c:42-44`),
  so config and saves land in app-internal storage without a code change.

Files written under `dir_config`:
- `config.txt`: `src/ConfigAdv.cpp:179` and `:193`; also `src/OTUTOR.cpp:448`, `src/OGAMSCEN.cpp:171`.
- Save games: `src/OGFILEA.cpp:754`, `:823`. Autosaves `AUTO.SAV`/`AUTO2.SAV` at `src/OSYS.cpp:1045-1095`.
- `syn.sav` (sync test save): `src/OSYS.cpp:2124-2150`.
- Screenshots `7KXXXXXX.BMP` via `FilePath full_path(dir_config)`: `src/OSYS.cpp:2666-2673`.
- Diagnostics `sdl.txt`: `src/OVGA.cpp:819-825`. Cookies `cookies.txt` (libcurl only): `src/curl/WebService.cpp:55`.

### D3. I/O API

- Stdio only. `File::file_open` uses `fopen(fileName, "rb")` at `src/OFILE.cpp:67`. `File::file_create` uses `fopen(fileName, "wb+")` at `src/OFILE.cpp:108`.
  `file_write` is at `OFILE.cpp:152`, `file_read` at `OFILE.cpp:199` (`fread` at `:212`).
- Byte order is handled explicitly with `SDL_SwapLE16`/`SDL_SwapLE32` (`src/OFILE.cpp:282`, `:401`, `:439`).
- No `std::ifstream`, `std::ofstream`, `SDL_RWops`, `SDL_RWFromFile`, `opendir` or `readdir` anywhere in `src/` (grep: 0).
- Directory listing on POSIX uses `glob` plus `stat` (`src/ODIR.cpp:104-111`). Random bytes come from `open("/dev/urandom")` (`src/misc_uuid.cpp:99`).
- `mkdir` is used in 8 files (through `misc_mkdir`, `src/OMISC.cpp:1254`). `remove(`: 7 hits in 5 files. `rename(`: 2 hits.

### D4. Case sensitivity of data filenames

Method: `scratchpad/casecheck2.py` extracted every string literal with a data extension (`.RES .ICN .COL .SPR .SCN .SCT .SCP .TUT .WAV .BMP .AVI .BIN .SET .TXT .SAV`)
from `src/` and `include/`, then compared it with the files in `data/` and `7kaa-music/` using exact, case-sensitive matching.

- 80 unique literals. 59 match exactly on disk. **0 case-only mismatches.**
- 21 are not on disk, and each is explained:
  - runtime-written: `AUTO.SAV`, `AUTO2.SAV`, `NORMAL.SAV`, `ERROR.SAV`, `DEBUG1-3.SAV`, `syn.sav`, `sdl.txt`, `cookies.txt`, `7KXXXXXX.BMP` (template).
  - DEMO-only: `DEMO.WAV` (`src/OSYS.cpp` under `#ifdef DEMO`), `DEMO.SCN`, `STANDARD.TUT` (`OSYS.cpp`, `OTUTOR.cpp`).
  - intro movie, compiled out: `INTRO.AVI` (`AM.cpp`, `OSYS.cpp`).
  - built from a prefix plus a number: `I_HILL`, `I_PLANT`, `I_ROCK`, `I_TERAIN`, `I_TERANM` (for example `OHILLRES.cpp:60` appends "I_HILL" and the number). The disk holds `I_HILL1-3.RES` and so on.
  - `GIF.RES` appears only in comments (`src/ORES.cpp:55`, `src/OIMGRES.cpp:47`).
- Conclusion: data filenames are upper case, match the disk exactly, and will work on ext4 (case-sensitive). Game-written files use lower case
  (`config.txt`, `sdl.txt`, `syn.sav`, `cookies.txt`) and are consistent with their own writes.
- Env overrides: `SKMESSAGES` (`src/LocaleRes.cpp:92`), `SKLOCALE` (`LocaleRes.cpp:240-241`), `SKDEBUG` (`src/dbglog.cpp:42`).

---

## E. Input

### E1. Event handling

- Entry: `Vga::handle_messages()` (`src/OVGA.cpp:357`) with `while( SDL_PollEvent(&event) )` at `OVGA.cpp:361`. One switch handles everything.
- Handled events:
  - `SDL_QUIT` at `OVGA.cpp:365`
  - `SDL_MULTIGESTURE` at `OVGA.cpp:368-372` (two-finger only, to `mouse.process_scroll`)
  - `SDL_FINGERDOWN` at `OVGA.cpp:373-375` (`mouse.end_scroll`)
  - `SDL_MOUSEWHEEL` at `OVGA.cpp:376-378`
  - `SDL_WINDOWEVENT` at `OVGA.cpp:379-411` (expose, resize, focus, minimise)
  - `SDL_MOUSEMOTION` at `OVGA.cpp:413-471`
  - `SDL_MOUSEBUTTONDOWN` at `OVGA.cpp:472-482`, `SDL_MOUSEBUTTONUP` at `OVGA.cpp:483-494`
  - `SDL_KEYDOWN` at `OVGA.cpp:495-546`, `SDL_KEYUP` at `:547`, `SDL_TEXTINPUT` at `:550-552`
  - `SDL_RENDER_TARGETS_RESET` at `OVGA.cpp:553-555` (sets `need_redraw_flag`)
- Not handled (falls to `default`, which logs "unhandled event"): `SDL_TEXTEDITING`, the joystick and joyball events (`OVGA.cpp:557-561`), and all other types.
- **Lifecycle events: none handled.** No `SDL_APP_*` case exists anywhere in `src/` (grep: 0). `SDL_RENDER_DEVICE_RESET` is not handled.

### E2. Mouse

- Left button: `SDL_BUTTON_LEFT` maps to `LEFT_BUTTON` at `src/OVGA.cpp:475`; release gives `LEFT_BUTTON_RELEASE` (`OVGA.cpp:483-494`).
- Right button: `SDL_BUTTON_RIGHT` maps to `RIGHT_BUTTON` at `OVGA.cpp:479`; release gives `RIGHT_BUTTON_RELEASE`. Middle button is ignored.
- Some screens treat right press as cancel together with Esc: `src/OMOUSE.cpp:444` (sets `right_press`) and `OMOUSE.cpp:1086`.
- Absolute mode (`MOUSE_INPUT_ABS`): motion uses `event.motion.x/y` and divides by `get_window_scale` when letterboxing is off (`OVGA.cpp:413-431`).
  Relative mode uses `xrel`/`yrel` (`OVGA.cpp:468-470`).
- Mouse grab: a button press forces grab (`set_window_grab(WINGRAB_FORCE)`, `OVGA.cpp:481`), release restores it (`OVGA.cpp:493`).
  While grabbed, the pointer warps at the window boundary (`OVGA.cpp:434-462`, `SDL_WarpMouseInWindow` at `:462`).

### E3. Edge scrolling

- `World::detect_scroll()` at `src/OWORLD.cpp:311-375`. Called every frame at `OWORLD.cpp:295`.
- Active only when `vga.is_input_grabbed()`.
- Four edges: left at `OWORLD.cpp:334-337` (`cur_x <= bound_x1 + fix_world_warp_slop`), right at `:341-344`, top at `:348-351`, bottom at `:355-358`. Each calls `zoom_matrix->scroll(+/-1, 0)` or `(0, +/-1)`.
- Rate limit: `next_scroll_time = now + 500/(scroll_speed+1)` ms (`OWORLD.cpp:371`). `scroll_speed` defaults to 5 (`src/OCONFIG.cpp:167`).
- Boundary is the window rectangle: `Vga::update_boundary()` at `src/OVGA.cpp:595-610`.
- Wheel and two-finger scroll go through `Mouse::process_scroll` (`src/OMOUSE.cpp:943-972`) into `zoom_matrix->scroll` (`OWORLD.cpp:313-324`).
- **Touch implication:** no hover exists, so edge scroll cannot be used on a phone. Map panning needs a drag or two-finger gesture (CONTRACT section 5, phase 2).

### E4. Keyboard and hotkeys

- SDL keycode to game key map: `src/OMOUSE.cpp` (`SDLK_*` cases, about lines 1140-1340; `SDLK_ESCAPE` at `:1156`, `SDLK_TAB` at `:1212`, `SDLK_RETURN` at `:1246`, `SDLK_F1`-`SDLK_F12` at `:1327-1338`).
- `SDL_KEYDOWN` (`OVGA.cpp:495-546`) handles these before the generic key path:
  - Alt+Enter: toggle fullscreen (`sys.toggle_full_screen_flag`).
  - Alt+F4: quit (`sys.signal_exit_flag`).
  - Alt+Tab: minimise, only in fullscreen.
  - Ctrl+G: toggle input grab. Ctrl+M: cycle mouse mode (absolute / relative-warp).
- Function keys (`Sys::detect_function_key`, `src/OSYS.cpp:1784-1856`) switch the view:
  - F1 nation (`OSYS.cpp:1788`), F2 town (1794), F3 economy (1800), F4 trade (1806), F5 military (1812), F6 tech (1818), F7 spy (1824), F8 rank (1830), F9 news log (1836).
  - Esc (1784) returns to normal view. F10 (1843) and F11 (1849) have no handler in the view switch.
- Typing: `SDL_TEXTINPUT` goes to `mouse.add_typing_event` (`OVGA.cpp:550-552`). Keys in the space-to-z range are withheld from the key path while text input is active (`OVGA.cpp:538`).

---

## F. Video

- Native resolution: **800 x 600, 8-bit paletted** (`include/OVGA.h:34-36`, `VGA_WIDTH`, `VGA_HEIGHT`, `VGA_BPP`).
- Game framebuffer: an 8-bpp `SDL_Surface` (`src/OVGABUF.cpp:70`) with its palette set by `SDL_SetPaletteColors` (`OVGABUF.cpp:105`).
- SDL setup, in `Vga::init` (`src/OVGA.cpp`):
  - `SDL_Init(SDL_INIT_VIDEO)` at `:88`.
  - Window size from `init_window_size()` (`:1032-1090`). Not fullscreen-desktop: 800 x 600 (`:1044-1045`). Otherwise the desktop usable bounds are used (`:1064`).
  - `SDL_CreateWindow` at `:99` with `config_adv.vga_window_width/height` and `init_window_flags()`.
  - `SDL_CreateRenderer(window, -1, 0)` at `:111`.
  - `SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear")` at `:115`.
  - `SDL_RenderSetLogicalSize(renderer, 800, 600)` at `:117-118`, only when `vga_keep_aspect_ratio` is set. Letterboxing therefore comes from SDL, not from game code.
  - Streaming texture in the window pixel format, 800 x 600, at `:131-137`.
  - Conversion target `SDL_CreateRGBSurface` at `:161`.
- Frame present (`src/OVGA.cpp:806-810`): `SDL_BlitSurface` from the 8-bit front buffer to the target (806), `SDL_UpdateTexture` (807), `SDL_RenderClear` (808), `SDL_RenderCopy` (809), `SDL_RenderPresent` (810).
- Fullscreen: `Vga::set_full_screen_mode()` (`OVGA.cpp:640-686`). The flag is `SDL_WINDOW_FULLSCREEN_DESKTOP` if `vga_full_screen_desktop` is set, else `SDL_WINDOW_FULLSCREEN` (true mode change) (`:645-648`), applied at `:667` via `SDL_SetWindowFullscreen`. Use the desktop flag on Android.

---

## G. Audio

- Backend glue: `include/OAUDIO.h:21-29` includes `openal_audio.h` when `USE_OPENAL` is defined and otherwise fails with `#error`. `configure.ac:313-320` sets `USE_OPENAL`. OpenAL is found by pkg-config or `AM_PATH_OPENAL` (`configure.ac:232-245`).
- OpenAL setup (`src/openal/openal_audio.cpp`): `OpenALAudio::init_wav()` at `:206`. It loads sound effects from `RESOURCE/A_WAVE2.RES` (`:215`, `DIR_RES"A_WAVE2.RES"`), then `alcOpenDevice(NULL)` at `:217` (default device), `alcCreateContext` at `:226`, `alcMakeContextCurrent` next.
- Buffers and sources: `alGenSources` at `:1008`, `alGenBuffers` at `:1106`, `alBufferData` at `:1152`. Formats: mono or stereo, 8 or 16 bit (`openal_format` at `:107-118`).
- Sound-effect folder: `SOUND/` (5 `.WAV`, `include/GAMEDEF.h:55` `DIR_SOUND`).
- Music: the file list is `src/OMUSIC.cpp:41-64` (upper case: `WAR.WAV`, `NORMAN.WAV`, `MAYA.WAV`, `GREEK.WAV`, `VIKING.WAV`, `PERSIAN.WAV`, `CHINESE.WAV`, `JAPANESE.WAV`, plus `WIN.WAV`/`LOSE.WAV`, and the DEMO list at `:41-50`).
  Playback (`src/OMUSIC.cpp:172-185`) builds `DIR_MUSIC + filename`, checks `is_file_exist`, then calls `audio.play_loop_wav`.
  `sys.dir_music` is set to `MUSIC/` only if `NORMAN.WAV` (or `DEMO.WAV`) is found there (`src/OSYS.cpp:2900-2904`).
- WAV parser (`src/openal/wav_stream.cpp`): RIFF/WAVE check at `:139`. Restricted to PCM with 8 or 16 bits and 1 or 2 channels (`:152-153`). **Music must be PCM WAV.** There is no OGG or MP3 path.
- Threads: no thread creation in `src/` (grep for `SDL_CreateThread`, `std::thread`, `pthread_create`: 0). Audio streaming is driven from the main loop.
- Backends: `third_party/openal-soft/Alc/backends/` includes `opensl.cpp` and `oboe.cpp`, so openal-soft can target Android without a system OpenAL. The NDK does not ship OpenAL. (Backend selection itself was not tested.)
- Music is not GPL. `third_party/7kaa-music/COPYING-Music.txt` grants permission to redistribute "freely" with conditions: the set must be complete and unmodified, the authors acknowledged, and the notice kept.
  Those conditions should be checked against the intended distribution (CONTRACT says personal use).

---

## H. Networking

### H1. enet

- `src/enet/multiplayer.cpp` (1,464 lines) wraps libenet. `include/multiplayer.h:34` includes `<enet/enet.h>`.
  Calls: `enet_initialize()` at `:107`, `enet_deinitialize()` at `:159`, `enet_host_create(` at `:274`.
- Built into `libmultiplayer.a` (`src/Makefile.am:309`). libenet itself comes from pkg-config (`configure.ac:253-259`). `third_party/enet` (1.3.18) is the upstream library.
- Enabled only when multiplayer and enet are both on (`configure.ac:138-151`, `:174-188`). `USE_ENET` is defined at `configure.ac:358-362`.
- Protocol: messages carry frame counts and random seeds (`src/OREMOTEM.cpp:369-370`, `:482-484`), so it is a lockstep design.
- **Out of scope per CONTRACT section 1.** Compile enet only if the link needs it, and leave the menu hidden.

### H2. libcurl

- `src/curl/WebService.cpp` (117 lines). `include/WebService.h:29` includes `<curl/curl.h>`.
- `src/AM.cpp:93-95` includes `WebService.h` under `HAVE_LIBCURL`. `WebService ws;` is a global at `AM.cpp:141-143`.
- `src/OGAMEMP.cpp` has about 20 `HAVE_LIBCURL` blocks (for example `:39`, `:556-862`).
- Configure: `--disable-curl` (`configure.ac:123-126`), `HAVE_LIBCURL` (`:339-342`), libcurl pkg-config (`:261-267`). Forced off when multiplayer is off (`:185-188`).
- **Out of scope per CONTRACT.** Build without it: do not compile `src/curl/WebService.cpp`, and leave `HAVE_LIBCURL` undefined. The guards in `AM.cpp` and `OGAMEMP.cpp` cover all call sites.
- Multiplayer-off guards: `DISABLE_MULTI_PLAYER` at `src/OGAMMAIN.cpp:88`, `:312`, `:675`, and `src/AM.cpp:371`.

---

## I. Build configuration (what a CMake build must reproduce)

### I1. Compile flags and checks from `configure.ac`

| Item | Source | Notes for CMake |
|---|---|---|
| `-include $(top_builddir)/include/config.h` | `configure.ac:24` (`GLOBAL_CFLAGS`), applied by `src/Makefile.am:297` | Provide an equivalent `config.h` |
| `-fsigned-char` | `configure.ac:27-41`, `:36` | **Required** (C++ and any code that depends on it) |
| `-mfpmath=387 -ffloat-store` | `configure.ac:45-55`, `:50` | x86-only. The compile test fails on arm64: verified on this host (`error: unknown FP unit '387'`). Do not add on arm64 |
| C++11 minimum | `configure.ac:21` (`AX_CXX_COMPILE_STDCXX_11`, mandatory) | Use C++11 or later |
| `register` keyword check | `configure.ac:58-71`, `:370-378` | Define `REGISTER` empty if the compiler rejects `register` (C++17) |
| `SDL_CFLAGS` | `src/Makefile.am:298` | From SDL2 CMake |
| `-I include` | `src/Makefile.am:290` | |

### I2. Link dependencies

- SDL2 >= 2.0.4 (`configure.ac:247`).
- OpenAL (`configure.ac:232-245`).
- libenet (`:253-259`) and libcurl (`:261-267`). Both are optional, see section H.
- gettext/iconv (`:204-230`). Only needed when NLS is on. CONTRACT says no translations.
- Windows libraries (`:269-282`): not relevant to Android.

### I3. Preprocessor defines

| Define | Set when | Source | Android value |
|---|---|---|---|
| `USE_SDL` | input or video is SDL (always) | `configure.ac:329-332` | define |
| `USE_POSIX` | `platform=posix` (not MinGW) | `configure.ac:302-304`, `:105` | define |
| `USE_OPENAL` | always | `configure.ac:313-317` | define |
| `USE_WINDOWS` | MinGW only | `configure.ac:299-301` | do not define |
| `NO_MEM_CLASS` | always | `configure.ac:344` | define |
| `REGISTER` | see I1 | `configure.ac:370-378` | `register` or empty |
| `HAVE_KNOWN_BUILD` | only if `-mfpmath=387` compiles (x86) | `configure.ac:295-297` | **not defined on arm64** (see J4) |
| `DISABLE_MULTI_PLAYER` | multiplayer off | `configure.ac:334-336` | define (multiplayer is out of scope) |
| `USE_ENET`, `HAVE_LIBCURL` | multiplayer on | `configure.ac:339-342`, `:358-362` | leave undefined |
| `USE_FHS`, `PACKAGE_DATA_DIR`, `LOCALE_DIR` | `--enable-fhs` (default yes on Linux) | `configure.ac:133-136`, `:364-368`, `src/Makefile.am:292-295` | do not define `USE_FHS`; data comes from `SKDATA` |
| `USE_ASM` | `--enable-asm` only | `configure.ac:118-121`, `:352-356` | **must not be defined** |
| `DEBUG`, `DYNARRAY_DEBUG_ELEMENT_ACCESS` | `--enable-debug` only | `configure.ac:128-131`, `:345-351` | optional for debug builds |
| `ENABLE_NLS` | gettext present (CONTRACT: no translations) | `include/config.h.in:14`, `include/LocaleRes.h:27`, `:47` | leave undefined; `include/gettext.h:27-41` provides a fallback for `_()` |
| `ENABLE_INTRO_VIDEO` | not defined by configure | `src/AM.cpp:27`, `OVIDEO.cpp:25` | leave undefined |
| `PACKAGE_VERSION` | `AC_INIT` | `configure.ac:5` (`2.15.7`) | define |
| `HAVE_SETENV`, `HAVE_STRING_STRCASECMP`, `HAVE_STRINGS_STRCASECMP`, `HAVE__NSGETEXECUTABLEPATH` | configure checks | `configure.ac:90-95` | Bionic has setenv and strcasecmp; leave `_NSGetExecutablePath` undefined |

---

## J. Top 5 risks for an arm64 Android build (ranked)

**1. Data and config access assume a POSIX filesystem and a working-directory `chdir`.**
The game chdirs into its data root (`src/OSYS.cpp:2823-2873`, `2887-2919`) and opens every asset with relative stdio paths
(`src/OFILE.cpp:67`, `:108`). APK assets are not readable with `fopen`, and the game uses no `SDL_RWops` or other abstraction (grep: 0).
Mitigation, per CONTRACT: at first launch extract the 102 MB data tree and the music to `SDL_AndroidGetInternalStoragePath()`, then
set `SKDATA` before `Sys::init` (`OSYS.cpp:221`, `:2834`). Config and saves already land in the same internal storage through `SDL_GetPrefPath`
(`OSYS.cpp:531`, SDL `SDL_sysfilesystem.c:42-44`). Extraction failure must stop the app with a message box (CONTRACT rule 5).

**2. Entry point and app lifecycle are not Android-aware.**
`int main(int argc, char **argv)` is at `src/AM.cpp:300`. No `SDL_main.h` is included anywhere (grep: 0), so SDL2's Android activity
(`SDLActivity` calls `SDL_main`) needs an explicit hook. The event loop handles no `SDL_APP_*` lifecycle events (grep: 0; the switch in
`src/OVGA.cpp:361-566` has no such case). Background pause, audio suspend and resume are therefore not wired up.
`SDL_RENDER_DEVICE_RESET` is not handled. Only `SDL_RENDER_TARGETS_RESET` is (`OVGA.cpp:553`). Whether SDL2's GLES renderer
recovers the streaming texture after an Android context loss without app help was not tested. Mitigation: add SDL_APP handlers and the device-reset handler in `port/android/`, behind `#ifdef __ANDROID__` (CONTRACT phase 3).

**3. `char` signedness is a silent, visual-only failure mode.**
`configure.ac:27-41` refuses unsigned `char` and forces `-fsigned-char`. arm64 defaults to unsigned, so colour and palette
values stored in `char` (`src/OBUTTON.cpp:290`, `src/OU_CARA.cpp:1487`, `src/OFIRMA.cpp:543`, `src/OTORNADO.cpp:376-395`,
nation colour codes `0xF0-0xFF` documented in `include/OFONT.h:50`) would compare and sign-extend differently. The result is wrong colours and
glyphs, not a crash, so it will not show up in a launch test. Mitigation: `-fsigned-char` on every game source (CONTRACT section 2).
Check the types of `color_light`/`color_down` before relying on it.

**4. Configure's feature flags come from an x86 FPU probe.**
`configure.ac:45-55` sets `fpu_type=387` only if `-mfpmath=387` compiles. On arm64 it does not (verified: `error: unknown FP unit '387'`).
Knock-on effects, all from `configure.ac`:
- `HAVE_KNOWN_BUILD` is undefined (`:295-297`). The build then reports "unknown build" (`src/ConfigAdv.cpp:150-151`, flag `FLAG_UNKNOWN_BUILD`), shows `?` in the version string (`src/OGAMMAIN.cpp:378`), and writes the warning into the diagnostic log (`src/OVGA.cpp:840-841`).
- Multiplayer is forced off (`:174-177`), and enet and curl with it (`:185-188`). This matches CONTRACT (multiplayer out of scope).
- `-ffloat-store` is dropped (`:50`). It existed to make x87 float results deterministic, so a port should not assume the same float behaviour as the x86 build.
Mitigation: a CMake build should define `HAVE_KNOWN_BUILD` deliberately after review, and should not replicate the x87 flags. Multiplayer is out of scope, so determinism only matters for single-player if the game uses floats in saved state (not checked).

**5. Unaligned and packed-struct access.**
`#pragma pack(1)` appears in 16 headers and `src/OFONT.cpp` (section C). Many byte buffers are read through typed pointers:
`src/OREMOTEM.cpp:285`, `:290`, `:300-306`, `:353`, `:374`, `:414`, `:432`; `src/enet/multiplayer.cpp:338`, `:867`;
`src/OBUTTON.cpp:348-349`; `src/OFLAME.cpp:83`; `src/ODYNARR.cpp:399`; `src/OMEM.cpp:127-344` (`*(long*)`).
arm64 Linux and Android tolerate most unaligned scalar loads, but not all instructions. Compilers may also assume alignment and emit vector
loads, which is undefined behaviour. The x86 build hides this. Mitigation: run one debug build with `-fsanitize=alignment` (UBSan) on
the device or host, and convert the hot spots to `memcpy`-based accessors. The 117 raw file-I/O sites are safe on the same ABI, so save files should stay compatible. This should be verified with a round-trip save from each build.

### Not ranked, but relevant

- OpenAL on Android: no system OpenAL. openal-soft has `opensl` and `oboe` backends (section G). Backend choice is untested.
- Music format: PCM WAV only (`wav_stream.cpp:152-153`). The 10 WAV files from `7kaa-music` are the source of truth.
- Music licence: `COPYING-Music.txt` permits redistribution under conditions (section G). Check these against CONTRACT's personal-use framing before distributing an APK.
- x86 assembly is excluded by default. Do not set `USE_ASM` (I3).

---

## Method and limits

- Read-only: `grep`, `sed`, `wc` and a Python scratch script (`scratchpad/casecheck2.py`, not in the repo). Nothing under `third_party/` was modified.
- Not done: no cross-compile, no game launch, no data-load test. The arm64 `-mfpmath=387` result is a host compile probe on Apple silicon.
- Not inspected in depth: field types in `include/OVGA.h` and `OVGABUF.h`, and the full `Mouse` state machine (section E).
