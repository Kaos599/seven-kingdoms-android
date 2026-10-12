/*
 * Seven Kingdoms Android port: game-side accessors for the phone touch layer (U2).
 *
 * Defined in the patched game sources (patches/7kaa/0011-android-touch-orders.patch):
 *   sk_game_tap_is_command  OPOWER.cpp  (Power::choose_cursor, the game's own hover-cursor logic)
 *   sk_game_request         OPOWER.cpp  (queue a selection action)
 *   sk_game_run_pending     OPOWER.cpp  (called from Sys::detect_view, OSYS2.cpp, where the game may redraw)
 */
#ifndef SK_GAME_H
#define SK_GAME_H

#ifdef __ANDROID__

// Would the game treat a RIGHT click at logical point (lx,ly) as an order for the current selection (move, attack,
// assign/station into a firm or town, board a ship, ...)? 1 = yes, 0 = no (select; also: nothing selected, selection
// not the player's own units, a command such as build/settle is pending, not in the normal map view, point outside
// the map view). Read-only.
int sk_game_tap_is_command(int lx, int ly);

// Selection actions run by the game itself, on the game's own thread inside Sys::detect_view.
enum SkGameAction
{
	SK_ACT_DESELECT = 1,     // Power::reset_selection() + info.disp()
	SK_ACT_GROUP_SELECT,     // GroupSelect::select_grouped_units(arg)   (same as the Alt+n key)
	SK_ACT_GROUP_ASSIGN,     // GroupSelect::group_units(arg)            (same as the Ctrl+n key)
};
void sk_game_request(int action, int arg);
void sk_game_run_pending(void);

#endif // __ANDROID__

#endif // SK_GAME_H
