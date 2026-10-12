/*
 * Seven Kingdoms Android port: on-screen keyboard for the game's text fields (GetA / OGETA.cpp).
 *
 * The game keeps SDL text input OFF (Mouse::init calls SDL_StopTextInput) and only turns it on for the
 * in-game chat. On Android SDL_StartTextInput is what raises the soft keyboard, so we tie it to the
 * game's own text-field class:
 *   - a tap on any GetA (GetA::detect_click) or the opening of an input-only dialog => request (show)
 *   - every GetA::detect_key poll while a field is live => heartbeat
 *   - Return/Done on a stand-alone GetA (GetA::detect) => done (hide)
 *   - no heartbeat for SK_TEXT_IDLE_MS (screen left, dialog closed) => hidden by sk_text_update()
 * Hooks live in patches/7kaa/0009-android-text-input.patch. Text input the game started itself (chat,
 * OSYS2.cpp:509, OR_NAT.cpp:410) is never stopped by this module.
 */
#ifndef SK_TEXT_H
#define SK_TEXT_H

#ifdef __ANDROID__

void sk_text_request(void);    // show the keyboard (idempotent; re-shows it if the user dismissed it)
void sk_text_heartbeat(void);  // a text field is alive this frame
void sk_text_done(void);       // the field was finished (Return): hide the keyboard we showed
void sk_text_update(void);     // once per frame (from sk_cheats_update): hide it when no field is alive

#endif // __ANDROID__

#endif // SK_TEXT_H
