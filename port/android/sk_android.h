/*
 * Seven Kingdoms Android port: hooks called from the (patched) game sources.
 * Everything here is Android-only; callers guard with #ifdef __ANDROID__.
 */
#ifndef SK_ANDROID_H
#define SK_ANDROID_H

#ifdef __ANDROID__

// Make sure the game data tree exists in app-internal storage (extracting it from the APK assets
// on first launch or after an update) and point SKDATA at it. On any failure this shows a message
// box with the reason and exits; it only returns when the data is ready.
void sk_android_prepare_data(void);

// Force the video settings the Android build depends on: fullscreen + keep-aspect-ratio, so that
// SDL_RenderSetLogicalSize(800, 600) letterboxes. Call after config_adv.init().
void sk_android_apply_video_config(void);

#endif // __ANDROID__

#endif // SK_ANDROID_H
