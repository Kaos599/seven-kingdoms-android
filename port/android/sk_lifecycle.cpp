/*
 * Seven Kingdoms Android port: app lifecycle.
 *
 * Upstream has no SDL_APP_* handling (docs/AUDIT.md risk 2). This layer:
 *
 *   DIDENTERBACKGROUND  pause the game (Sys::pause) and mark it user-paused so that nothing resumes it
 *                       by itself; pause the OpenAL device (ALC_SOFT_pause_device) so sound and music
 *                       stop at once and resume from where they were.
 *   DIDENTERFOREGROUND  resume the OpenAL device, request a full redraw. The game stays PAUSED: the
 *                       player taps the toolbar Pause button (Space) to continue, so no units are lost
 *                       while the screen is coming back.
 *   TERMINATING         log only.
 *   LOWMEMORY           log only.
 *
 * Why the event loop needs no extra blocking: SDL's Android backend defaults to
 * SDL_HINT_ANDROID_BLOCK_ON_PAUSE=1. After the app events above have been delivered, the next
 * SDL_PumpEvents (inside SDL_PollEvent in Vga::handle_messages) blocks on a semaphore until the
 * activity resumes, so the main thread does not spin in the background. The hint is set explicitly
 * in sk_lifecycle_init. SDL also pauses its own audio devices then, but not OpenAL's, hence the
 * explicit device pause here.
 *
 * Thread note: with the blocking pump, SDL sends all SDL_APP_* events (except TERMINATING and
 * LOWMEMORY, which are posted from the Java thread and merely queued) from the main thread, so the
 * handler can touch game state directly.
 *
 * Multiplayer: the pause is networked by the game itself (see sk_net_flush in the background handler and
 * docs/multiplayer/README.md); ENet peers get a 60 s timeout (patch 0012) so a short switch-away does not
 * drop the match.
 *
 * No autosave on background: Sys::auto_save only fires on a frame boundary on the first day of an even
 * month and game_file.save_game() runs save_process(), which is not safe from inside the event pump.
 * The game's own AUTO.SAV (every 2 game months, in the pref path) and the player's saves are relied on.
 */
#ifdef __ANDROID__

#include <android/log.h>
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <OSYS.h>
#include <OCONFIG.h>
#include "sk_lifecycle.h"
#include "sk_net.h"

#define LOG_TAG "7kaa"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static bool g_audio_paused = false;

typedef void (ALC_APIENTRY *PauseDeviceFn)(ALCdevice *);

static ALCdevice *current_audio_device()
{
	ALCcontext *ctx = alcGetCurrentContext();
	return ctx ? alcGetContextsDevice(ctx) : NULL;
}

static void pause_audio()
{
	if (g_audio_paused)
		return;
	ALCdevice *dev = current_audio_device();
	if (!dev || !alcIsExtensionPresent(dev, "ALC_SOFT_pause_device"))
	{
		LOGI("lifecycle: no OpenAL device or ALC_SOFT_pause_device missing, audio not paused");
		return;
	}
	PauseDeviceFn fn = (PauseDeviceFn)alcGetProcAddress(dev, "alcDevicePauseSOFT");
	if (!fn)
		return;
	fn(dev);
	g_audio_paused = true;
}

static void resume_audio()
{
	if (!g_audio_paused)
		return;
	g_audio_paused = false;
	ALCdevice *dev = current_audio_device();
	if (!dev)
		return;
	PauseDeviceFn fn = (PauseDeviceFn)alcGetProcAddress(dev, "alcDeviceResumeSOFT");
	if (fn)
		fn(dev);
}

void sk_lifecycle_init(void)
{
	// Block the main thread inside SDL's event pump while backgrounded (SDL's default, set on purpose).
	SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "1");
}

int sk_lifecycle_handle_event(const SDL_Event *event)
{
	switch (event->type)
	{
	case SDL_APP_WILLENTERBACKGROUND:
		return 1;   // work is done on DIDENTERBACKGROUND, right before SDL blocks the loop

	case SDL_APP_DIDENTERBACKGROUND:
		LOGI("lifecycle: background (sys_flag=%d frame_speed=%d)", (int)sys.sys_flag, (int)config.frame_speed);
		if (sys.sys_flag == SYS_RUN)
		{
			if (config.frame_speed)
				sys.pause();
			sys.user_pause_flag = 1;   // stays paused after returning, see header comment
			// Multiplayer: Sys::pause() -> Sys::set_speed(0) broadcasts MSG_SET_SPEED (OSYS.cpp), so the other
			// phones pause too. ENet only queues that packet; push it out before SDL blocks this thread.
			sk_net_flush();
		}
		pause_audio();
		return 1;

	case SDL_APP_WILLENTERFOREGROUND:
		return 1;

	case SDL_APP_DIDENTERFOREGROUND:
		LOGI("lifecycle: foreground");
		resume_audio();
		sys.need_redraw_flag = 1;
		return 1;

	case SDL_APP_TERMINATING:
		LOGI("lifecycle: terminating");
		return 1;

	case SDL_APP_LOWMEMORY:
		LOGI("lifecycle: low memory");
		return 1;
	}
	return 0;
}

#endif // __ANDROID__
