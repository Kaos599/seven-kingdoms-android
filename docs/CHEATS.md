# Seven Kingdoms 2.15.7 cheats

Source of truth: `third_party/7kaa/src` (2.15.7). Line numbers are for the pristine upstream files.
Cross-checked against the 7kfans 2.15.4 manual ("c Add $1000, n Add all technology, u Enable/disable King immortal
mode"), GameFAQs, GamesRadar and the Steam guide ("hold Shift, press 111222333"). Older web lists give other keys
(for example "T" for technology, "*" for food); those belong to the original 1997 game. 2.15.7 uses the keys below.

## Enabling cheat mode

Hold Shift and press `1 1 1 2 2 2 3 3 3` (typed text `!!!@@@###`).

- Key events `KEYEVENT_CHEAT_ENABLE1/2/3` are bound to `shift+1`, `shift+2`, `shift+3` (`OMOUSE.cpp:197-199`; names in `ConfigAdv.cpp:97-99`).
- The sequence is `cheat_str[]` (`OSYS.cpp:126-136`), matched by `Sys::detect_key_str` (`OSYS.cpp:2594-2612`).
- `Sys::process_key` (`OSYS.cpp:1515-1555`): when the player has a nation and the game is not multiplayer, the full
  sequence shows "Cheat Mode Enabled." and sets `(~nation_array)->cheat_enabled_flag = 1` (`OSYS.cpp:1541-1544`;
  field at `ONATIONB.h:192`). From then on every key in `Sys::detect_cheat_key` is live.
- The flag is saved with the game (`OGF_REC.h`), reset per game, and shown as "(Cheated)" in the game-over score (`OGAMEND.cpp:598-601`).
- There is no key to turn cheat mode off again.
- Cheats do nothing in multiplayer (`remote.is_enable()` checks at `OSYS.cpp:1537`, `OSYS.cpp:1862`).

## Cheat keys (after cheat mode is on)

All are plain keys (no Shift), handled in `Sys::detect_cheat_key` (`OSYS.cpp:1860-1996`).

| Cheat | What it does | Key | file:line |
|---|---|---|---|
| Add cash | +1000 cash to the player nation | `c` | `OSYS.cpp:1876-1879` -> `NationBase::add_cheat` (`ONATIONB.cpp:955`) |
| Add food | +1000 food | `\` | `OSYS.cpp:1881-1884` -> `NationBase::add_food` (`ONATIONB.cpp:1003`) |
| All technology and gods | Raises every technology level and enables all Greater Beings (message box) | `n` | `OSYS.cpp:1886-1890` -> `TechRes::inc_all_tech_level` (`OTECHRES.cpp:420`), `GodRes::enable_know_all` (`OGODRES.cpp:120`) |
| Reveal map | Unveils and marks the whole map visited | `/` | `OSYS.cpp:1892-1895` -> `World::unveil`, `World::visit` |
| Town population | Adds 10 people to the selected town | `;` | `OSYS.cpp:1897-1915` -> `Town::init_pop(race, 10, 100)` |
| Immortal king (toggle) | The human player's king can't lose hit points; message box | `u` | `OSYS.cpp:1917-1924` flips `config.king_undie_flag` (`OCONFIG.h:159`); effect at `OUNIT.cpp:950-954` (king HP refilled each frame) |
| Max prayer points | Selected Seat of Power base gets `MAX_PRAY_POINTS` | `=` | `OSYS.cpp:1926-1937` |
| Firm full HP | Finishes building the selected firm / refills its hit points | `-` | `OSYS.cpp:1939-1945` |
| Fast build (toggle) | Everything the player builds or researches finishes quickly; message box | `z` | `OSYS.cpp:1947-1955` flips `config.fast_build` (`OCONFIG.h:157`); used in `OFIRM.cpp:1385,1419`, `OF_BASE.cpp:459`, `OF_RESE.cpp:547`, `OF_WAR.cpp:915`, `OTOWNIF.cpp:724,2019` |
| Combat level +20 | Selected unit, capped at 100 | `[` | `OSYS.cpp:1958-1966` |
| Skill level +20 | Selected unit's skill, capped at 100 | `]` | `OSYS.cpp:1969-1978` |
| Spy skill +20 | Selected unit's spying skill, capped at 100 | `'` | `OSYS.cpp:1981-1993` |
| *Panel only:* REPUTATION +10 / -10 | Player nation's reputation +10 / -10, clamped to -100..100 (same as scenario key `r` / `e`) | (none, direct call) | copied from `OSYS.cpp:2270-2288`; patch 0008 `sk_cheats_reputation` |
| *Panel only:* RACE POP +10 / -10 | +10 / -10 people of the race browsed in the selected own town (same as scenario key `w` / `q`) | (none, direct call) | copied from `OSYS.cpp:2230-2268`, race from `OTOWNIF.cpp:2242`; patch 0008 `sk_cheats_race_pop` |
| *Panel only:* HEAL ALL | Every living unit of the player's nation to its maximum hit points; every player firm not under construction likewise | (none, direct call) | units: `OUNIT.cpp:952-953` (king refill) + `OSYS.cpp:1939-1945` (firm key); patch 0008 `sk_cheats_heal_all` |

Selection-based cheats (`;` `=` `-` `[` `]` `'`) do nothing when the right thing is not selected.

The panel-only rows are not keys. They call new game-side functions in `patches/7kaa/0008-android-cheats.patch`
directly, because the scenario keys (`r`, `e`, `w`, `q`) are only reachable with `DEBUG.SYS`/`TESTING.SYS`/`CHEAT.SYS`.
Each one uses the same gate as the cheat keys (single player, human nation, cheat mode on; `sk_cheats_get_game_state`).

**RACE POP vs TOWN POP.** `TOWN POP +10` (key `;`, `OSYS.cpp:1897-1915`) adds 10 people of a *random* race
(`random_race()`), with no check of the race. `RACE POP +10 / -10` acts on one race: the race row selected in the
town's own race list (`Town::get_selected_race`, `OTOWNIF.cpp:2242`, i.e. `browse_race`), not the town's majority
race. It does nothing unless that race has people in the town. `-10` kills 10 people of that race (`kill_town_people`),
as the scenario key `q` does. Results on the button: DONE, NO TOWN (no own town selected), NO RACE.

**REPUTATION.** Changes `reputation` directly and clamps to -100..100, as the scenario keys do. It does not use
`NationBase::change_reputation` (`ONATIONB.cpp:1031`), which the keys do not use either.

**HEAL ALL.** Units: alive units of the player's nation (the filter of `sk_cheats_unit_cheat`, no selection test) get
`hit_points = max_hit_points`. Firms: the player's firms get `hit_points = max_hit_points`, as `FIRM FULL HP` does for
one firm. Exception: firms still under construction are skipped. The construction code completes a firm as soon as
`hit_points >= max_hit_points` (`OFIRM.cpp:1437`), so refilling one would finish the building. The button flashes the
number of units healed (firms are not counted).
`config.king_undie_flag` and `config.fast_build` are reset at game start (`OBATTLE.cpp:181-182`) and saved in savegames (`OGF_REC.cpp:383,385`).

## Not covered

Debug-only keys exist (`Sys::detect_debug_cheat_key`, `OSYS.cpp:2001-2164`, Ctrl+key; `detect_scenario_cheat_key`,
`OSYS.cpp:2169-2542`) but only run when the data folder contains `DEBUG.SYS`, `TESTING.SYS` or `CHEAT.SYS`
(`OSYS.cpp:207-215`, `1524-1531`). The normal game never reaches them, so the Android panel does not inject them. Four of the scenario keys (`r`, `e`,
`w`, `q`) are copied into the panel as direct calls (see the REPUTATION and RACE POP rows above).

## Android panel mapping

The "C" toolbar button opens `port/android/sk_cheats.cpp`. It injects the keys above as SDL key events (Shift is
set through `SDL_SetModState`, because the game reads Shift from `SDL_GetModState()`). State read-back (in game?
cheat flag, immortal king, fast build) comes from `sk_cheats_get_game_state()` in `patches/7kaa/0008-android-cheats.patch`.
The rows marked *Panel only* (REPUTATION, RACE POP, HEAL ALL) and the multi-unit cheats are direct calls into
`patches/7kaa/0008-android-cheats.patch` (`sk_cheats_reputation`, `sk_cheats_race_pop`, `sk_cheats_heal_all`,
`sk_cheats_unit_cheat`); they do not inject keys. Layout: two button columns in the left margin, three in the wide panel
and the overlay (the last button spans a short last row). Every button is at least 44 logical px tall in the wide
layout at a 1220 px tall screen; that figure is a calculation from the layout code, not measured on a device.
