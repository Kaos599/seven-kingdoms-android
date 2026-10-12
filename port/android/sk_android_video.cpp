/*
 * Seven Kingdoms Android port: video configuration.
 */
#ifdef __ANDROID__

#include <SDL.h>
#include <ConfigAdv.h>
#include "sk_android.h"

void sk_android_apply_video_config(void)
{
	// The game renders at its logical size (wide on wide phones, 800x600 on 4:3, chosen later by
	// Config::init_resolution) and relies on SDL's logical size to letterbox on the phone's screen. Config.txt must not be able to switch this off.
	config_adv.vga_full_screen = 1;
	config_adv.vga_full_screen_desktop = 1;
	config_adv.vga_keep_aspect_ratio = 1;
	config_adv.vga_allow_highdpi = 0;
	SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
		"video: fullscreen desktop + keep aspect ratio forced on (logical size set by Config::init_resolution)");
}

#endif // __ANDROID__
