/*
 * Seven Kingdoms Android port: on-screen keyboard for text fields (see sk_text.h).
 *
 * Input path once the keyboard is up (SDL 2.32 Android, SDLActivity.java SDLInputConnection):
 *   - typed characters arrive as SDL_TEXTINPUT -> Vga::handle_messages (OVGA.cpp, SDL_TEXTINPUT case)
 *     -> Mouse::add_typing_event -> GetA::detect_key reads mouse.typing_char;
 *   - Backspace arrives as SDL_KEYDOWN SDLK_BACKSPACE (nativeGenerateScancodeForUnichar('\b')) and
 *     Enter/Done as SDL_KEYDOWN SDLK_RETURN (onNativeSoftReturnKey() is false unless the
 *     SDL_RETURN_KEY_HIDES_IME hint is set, so SDL forwards it), both mapped to KEY_BACK_SPACE /
 *     KEY_RETURN by Mouse::is_key (OMOUSE.cpp SDLK_BACKSPACE case) and handled in GetA::detect_key.
 *   - OVGA.cpp drops printable SDL_KEYDOWNs while text input is active, so nothing is typed twice.
 */
#ifdef __ANDROID__

#include <SDL.h>
#include "sk_text.h"

static const Uint32 SK_TEXT_IDLE_MS = 700;   // no live text field for this long => close the keyboard

static bool   g_mine  = false;   // we started SDL text input (the game's own chat start is never touched)
static Uint32 g_beat  = 0;

void sk_text_request(void)
{
	if (!SDL_IsTextInputActive())
		g_mine = true;
	// Called even when already active: after the user dismissed the IME (Back key) SDL still reports active,
	// and a second SDL_StartTextInput re-shows it.
	SDL_StartTextInput();
	g_beat = SDL_GetTicks();
}

void sk_text_heartbeat(void)
{
	if (g_mine)
		g_beat = SDL_GetTicks();
}

void sk_text_done(void)
{
	if (g_mine)
	{
		SDL_StopTextInput();
		g_mine = false;
	}
}

void sk_text_update(void)
{
	if (!g_mine)
		return;
	if (!SDL_IsTextInputActive())      // the game stopped it itself
	{
		g_mine = false;
		return;
	}
	if ((Sint32)(SDL_GetTicks() - g_beat) > (Sint32)SK_TEXT_IDLE_MS)
	{
		SDL_StopTextInput();
		g_mine = false;
	}
}

#endif // __ANDROID__
