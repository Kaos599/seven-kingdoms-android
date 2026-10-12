/*
 * Seven Kingdoms Android port: app lifecycle (background / foreground).
 *
 * Callers (the patched Vga::handle_messages) guard with #ifdef __ANDROID__.
 */
#ifndef SK_LIFECYCLE_H
#define SK_LIFECYCLE_H

#ifdef __ANDROID__

#include <SDL.h>

// Set SDL hints (event loop blocks while the app is in the background). Call before SDL_Init.
void sk_lifecycle_init(void);

// Offer an event to the lifecycle layer. Returns 1 if it was an SDL_APP_* event (consumed).
// Must be called from the game's main thread (the event loop), never from an event watch.
int sk_lifecycle_handle_event(const SDL_Event *event);

#endif // __ANDROID__

#endif // SK_LIFECYCLE_H
