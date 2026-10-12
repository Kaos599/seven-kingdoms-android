/*
 * Seven Kingdoms Android port: in-game cheats panel.
 *
 * A touch panel (opened from the "C" toolbar button) that drives the game's OWN cheat keys by
 * injecting SDL key events (Shift+1 x3, Shift+2 x3, Shift+3 x3 to enable; then one key per cheat).
 * Game state (in game? cheats enabled? immortal king / fast build on?) is read back through
 * sk_cheats_get_game_state(), defined in patches/7kaa/0008-android-cheats.patch (OSYS.cpp).
 * See docs/CHEATS.md for the cheat list and source references.
 */
#ifndef SK_CHEATS_H
#define SK_CHEATS_H

#ifdef __ANDROID__

#include <SDL.h>

// Snapshot of the game state the panel needs. Filled by the patched game source (OSYS.cpp).
struct SkCheatsGameState
{
	int in_game;      // sys.sys_flag == SYS_RUN (a game is running)
	int can_cheat;    // in game, human nation, not multiplayer, no in-game/option menu open
	int cheat_on;     // the game's own cheat mode is enabled (nation cheat_enabled_flag)
	int king_undie;   // config.king_undie_flag (immortal king)
	int fast_build;   // config.fast_build
};

// Defined in the patched OSYS.cpp.
void sk_cheats_get_game_state(SkCheatsGameState *st);

// Screen geometry the panel needs, in output pixels (filled by sk_touch).
struct SkCheatsView
{
	int   out_w, out_h;   // renderer output size
	float game_x, game_w; // letterboxed game rect (horizontal extent)
	int   strip_h;        // height of the collapsible strip row (overlay mode keeps clear of it)
};

// Open / close the panel. Toggle is what the "C" toolbar button calls.
void sk_cheats_toggle(void);
void sk_cheats_close(void);
int  sk_cheats_is_open(void);

// Once per Vga::handle_messages (from sk_touch_update): feeds queued key injections to the game.
void sk_cheats_update(SDL_Window *window);

// Touch routing. type: 0 = finger down, 1 = motion, 2 = up. Coordinates are output pixels.
// Returns 1 if the panel consumed the event (the game and the rest of the touch layer must not see it).
int sk_cheats_touch(const SkCheatsView *v, int type, SDL_FingerID id, float px, float py);

// Drop any in-progress panel press (focus loss).
void sk_cheats_reset_touch(void);

// Draw the panel. The caller has already set blending and suspended the logical size.
void sk_cheats_draw(SDL_Renderer *r, const SkCheatsView *v);

#endif // __ANDROID__

#endif // SK_CHEATS_H
