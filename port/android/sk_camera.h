/*
 * Seven Kingdoms Android port: map camera (zoom + smooth pan), presentation layer only.
 *
 * The engine keeps drawing its whole map view (ZOOM_WIDTH x ZOOM_HEIGHT at ZOOM_X1/ZOOM_Y1) at 1:1 and
 * scrolls in whole 32 px tiles. The camera shows a window of that view, size view/z, scaled up to fill the map's
 * on-screen rectangle, and requests whole-tile engine scrolls so the window always stays inside what the engine drew.
 * Everything outside the map rectangle is presented 1:1 exactly as before. See docs/PHONE-UX-CONTRACT.md.
 *
 * Callers: patched Vga::flip (sk_camera_present), ZoomMatrix::draw (sk_camera_note_draw), MapMatrix::draw_square
 * (sk_camera_minimap_window), and sk_touch.cpp (the rest). Patch: patches/7kaa/0010-android-camera.patch.
 */
#ifndef SK_CAMERA_H
#define SK_CAMERA_H

#ifdef __ANDROID__

#include <SDL.h>

// Default zoom of a new game / after a reset. One constant so it can be changed after on-device testing.
static const float SK_CAMERA_DEFAULT_ZOOM = 1.0f;
static const float SK_CAMERA_MIN_ZOOM     = 1.0f;
static const float SK_CAMERA_MAX_ZOOM     = 2.0f;

// What the touch layer needs to know about the last presented frame (output pixels).
struct SkCamView
{
	int      enabled;   // the main map is showing: camera gestures apply inside map_dst
	int      active;    // the presented map differs from a plain 1:1 copy: touches inside map_dst need the inverse map
	SDL_Rect map_dst;   // the map rectangle on screen, output px (valid when enabled)
};

// Vga::flip, after the 1:1 frame copy: per-frame sync with the engine, then (if the camera is active) overdraw the
// map rectangle with the scaled window. frame_dst = where the whole logical frame was copied; lw/lh = its logical size.
void sk_camera_present(SDL_Renderer *r, SDL_Texture *frame, const SDL_Rect *frame_dst, int lw, int lh);

// ZoomMatrix::draw: the engine view origin (tiles) the back buffer is being drawn with.
void sk_camera_note_draw(int top_x_loc, int top_y_loc);

// MapMatrix::draw_square: the camera window in map tiles (absolute map coordinates). Returns 0 (draw the engine's
// own box) unless the camera is active.
int sk_camera_minimap_window(float *x, float *y, float *w, float *h);

void sk_camera_get_view(SkCamView *v);

// Output px -> game (logical) px through the inverse of the presented mapping. Returns 1 if the point is inside the
// map rectangle and the camera is active (lx/ly are then the game coordinates); 0 otherwise (caller maps as before).
int sk_camera_to_game(float px, float py, int *lx, int *ly);

// Move/zoom so that the world point under (px0,py0) ends up under (px1,py1) with the zoom multiplied by ratio.
// ratio = 1: plain pan following the finger. Output px. Ignored when the camera is not enabled.
void sk_camera_transform(float px0, float py0, float px1, float py1, float ratio);

// Fling: initial finger velocity in output px/s (content keeps following the finger's direction). Decays to a stop.
void sk_camera_fling(float vx_out, float vy_out);
void sk_camera_stop_fling(void);

float sk_camera_get_zoom(void);
void  sk_camera_reset_zoom(void);   // back to SK_CAMERA_DEFAULT_ZOOM about the window centre (for the U2 toolbar button)

#endif // __ANDROID__

#endif // SK_CAMERA_H
