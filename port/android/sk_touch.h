/*
 * Seven Kingdoms Android port: touch input layer.
 *
 * Turns SDL_FINGER* events into the game's own Mouse input (cursor position, button events, scroll)
 * and draws the hotkey toolbar and long-press feedback on top of the presented frame.
 * Callers (the patched Vga::handle_messages / Vga::flip) guard with #ifdef __ANDROID__.
 */
#ifndef SK_TOUCH_H
#define SK_TOUCH_H

#ifdef __ANDROID__

#include <SDL.h>

// Set SDL hints (no touch->mouse synthesis, trap the Back button). Call before SDL_Init.
void sk_touch_init(void);

// Call once per Vga::handle_messages() before polling: fires long-press and delayed button releases.
void sk_touch_update(void);

// Offer an event to the touch layer. Returns 1 if it was consumed (the game must not see it).
// Consumes SDL_FINGER*, SDL_MULTIGESTURE and the Back key; resets gesture state on focus loss.
int sk_touch_handle_event(const SDL_Event *event, SDL_Window *window, SDL_Renderer *renderer);

// Draw the toolbar and feedback rings. Call after the game texture is copied, before present.
void sk_touch_draw(SDL_Renderer *renderer);

#endif // __ANDROID__

#endif // SK_TOUCH_H
