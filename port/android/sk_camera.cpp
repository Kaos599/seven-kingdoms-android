/*
 * Seven Kingdoms Android port: map camera. See sk_camera.h.
 *
 * Model
 *   world px   = engine pixel coordinates of the map (tile * 32). The engine draws the tiles
 *                [top_x_loc, top_x_loc + view/32) into the map rectangle, so view px (0,0) = world (top_x_loc*32, ...).
 *   (g_tx,g_ty) = camera target: world px of the top-left of the window.
 *   window     = sw x sh logical px (view / zoom, rounded), shown scaled into the map's on-screen rectangle.
 *   drawn origin = top_x_loc/top_y_loc at the last ZoomMatrix::draw, i.e. what the back buffer really contains.
 *   window offset inside the drawn view = target - drawn origin, clamped to [0, view - window].
 * Each flip: (1) reconcile with the engine origin (an origin change we did not request = a jump: minimap click, K/G,
 * message go-to: re-centre), (2) integrate the fling, (3) clamp the target to the world, (4) request a whole-tile
 * engine scroll when the window would not fit in the engine view, (5) present.
 */
#ifdef __ANDROID__

#include <math.h>
#include <stdlib.h>
#include <SDL.h>
// Upstream headers are noisy (1997 code): silence their warnings, keep ours.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlogical-op-parentheses"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wreorder-ctor"
#pragma clang diagnostic ignored "-Wconstant-logical-operand"
#include <OSYS.h>
#include <OWORLD.h>
#include <OBOX.h>
#include <OOPTMENU.h>
#include <OINGMENU.h>
#include <OCONFIG.h>
#include <OMOUSE.h>
#pragma clang diagnostic pop
#include "sk_camera.h"

static const int   TILE_PX        = 32;      // ZOOM_LOC_WIDTH / ZOOM_LOC_HEIGHT
static const float FLING_DECAY    = 6.0f;    // 1/s: v *= exp(-6 t); from 3000 px/s it is below the cut-off in ~0.8 s
static const float FLING_STOP_PXS = 25.0f;   // world px/s below which the fling ends
static const float FLING_MAX_PXS  = 6000.0f; // world px/s cap

static float g_zoom = SK_CAMERA_DEFAULT_ZOOM;
static float g_tx = 0, g_ty = 0;             // target, world px (valid when g_have)
static bool  g_have = false;
static int   g_seen_x = 0, g_seen_y = 0;     // engine origin (tiles) at the previous sync
static int   g_req_x = 0, g_req_y = 0;       // whole-tile scroll we asked for and have not seen applied yet
static int   g_drawn_x = 0, g_drawn_y = 0;   // origin (world px) of the last ZoomMatrix::draw
static float g_vx = 0, g_vy = 0;             // fling velocity, world px/s
static Uint32 g_last_ms = 0;

static SkCamView g_view = { 0, 0, { 0, 0, 0, 0 } };
static int g_sx = 0, g_sy = 0, g_sw = 0, g_sh = 0;   // presented window, logical px (inside the frame)

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int   clampi(int v, int lo, int hi)       { return v < lo ? lo : (v > hi ? hi : v); }

// Window size in logical px for a zoom. Single definition: forward map, inverse map and clamps all use it.
static int win_dim(int view, float z)
{
	int d = (int)lroundf(view / z);
	return clampi(d, 1, view);
}

// "The main map is showing": the game is running, normal view mode (not a report screen), no in-game / option
// menu, no message box. Sources: Sys::sys_flag == SYS_RUN (OSYS.h:43, same test as the cheats panel,
// patches/7kaa/0008), Sys::view_mode == MODE_NORMAL (OSYS.h:55,95; Sys::detect OSYS2.cpp:80 and Sys::disp_view
// OSYS2.cpp:569 treat every other mode as a report covering the map), option_menu / in_game_menu
// is_active() (OOPTMENU.h:58, OINGMENU.h:42; Sys::disp_frame OSYS2.cpp:527, Sys::detect OSYS2.cpp:88-98),
// Box::opened_flag (OBOX.h:52; set by Box::paint OBOX.cpp:117, which draws over the map and calls vga.flip).
static bool in_game(void)
{
	return sys.sys_flag == SYS_RUN && !sys.signal_exit_flag && world.zoom_matrix != NULL;
}

static bool main_map_showing(void)
{
	return in_game() && sys.view_mode == MODE_NORMAL && !option_menu.is_active() && !in_game_menu.is_active() &&
		!Box::opened_flag;
}

static void recentre(int ox, int oy, int sw, int sh)
{
	g_tx = (float)(ox * TILE_PX) + (float)(config.zoom_width - sw) / 2.0f;
	g_ty = (float)(oy * TILE_PX) + (float)(config.zoom_height - sh) / 2.0f;
	g_vx = g_vy = 0;
	g_req_x = g_req_y = 0;
}

// Is an origin change d (tiles) explained by the scroll req we asked for?
static bool explained(int d, int req)
{
	return d == 0 || (req != 0 && ((d > 0) == (req > 0)) && abs(d) <= abs(req));
}

// Whole-tile engine origin wanted so the window [t, t+len) fits inside [origin, origin+view).
static int want_tile(float t, int len, int view, int cur, int max_start)
{
	int lo = (int)ceilf((t + len - view) / TILE_PX);
	int hi = (int)floorf(t / TILE_PX);
	int n = (lo <= hi) ? clampi(cur, lo, hi) : (int)lroundf(t / TILE_PX);   // no tile fits both edges (z close to 1): nearest
	return clampi(n, 0, max_start);
}

void sk_camera_note_draw(int top_x_loc, int top_y_loc)
{
	g_drawn_x = top_x_loc * TILE_PX;
	g_drawn_y = top_y_loc * TILE_PX;
}

void sk_camera_present(SDL_Renderer *r, SDL_Texture *frame, const SDL_Rect *dst, int lw, int lh)
{
	Uint32 now = SDL_GetTicks();
	float dt = (now - g_last_ms) / 1000.0f;
	g_last_ms = now;
	if (dt < 0) dt = 0;
	if (dt > 0.05f) dt = 0.05f;

	g_view.enabled = g_view.active = 0;

	if (!in_game())
	{
		g_have = false;
		g_vx = g_vy = 0;
		return;
	}

	ZoomMatrix *zm = world.zoom_matrix;
	const int W = config.zoom_width, H = config.zoom_height;
	const int mx = ZOOM_X1, my = ZOOM_Y1;
	if (W <= 0 || H <= 0 || lw <= 0 || lh <= 0)
		return;
	const int sw = win_dim(W, g_zoom), sh = win_dim(H, g_zoom);
	const int ox = zm->top_x_loc, oy = zm->top_y_loc;   // engine origin now (may be ahead of what is drawn)

	//---- (1) reconcile with the engine origin: a change we did not ask for is a jump (minimap, K/G, go-to) ----//
	if (!g_have)
	{
		recentre(ox, oy, sw, sh);
		g_have = true;
	}
	else
	{
		int dx = ox - g_seen_x, dy = oy - g_seen_y;
		if (!explained(dx, g_req_x) || !explained(dy, g_req_y))
			recentre(ox, oy, sw, sh);
	}
	g_seen_x = ox;
	g_seen_y = oy;
	if (mouse.scroll_x == 0 && mouse.scroll_y == 0)
		g_req_x = g_req_y = 0;   // our request (if any) has been consumed by World::detect_scroll

	bool showing = main_map_showing();

	if (showing)
	{
		//---- (2) fling ----//
		if (g_vx != 0 || g_vy != 0)
		{
			g_tx += g_vx * dt;
			g_ty += g_vy * dt;
			float f = expf(-FLING_DECAY * dt);
			g_vx *= f;
			g_vy *= f;
			if (g_vx * g_vx + g_vy * g_vy < FLING_STOP_PXS * FLING_STOP_PXS)
				g_vx = g_vy = 0;
		}

		//---- (3) clamp the target to the world ----//
		float hix = (float)(zm->max_x_loc * TILE_PX - sw), hiy = (float)(zm->max_y_loc * TILE_PX - sh);
		if (hix < 0) hix = 0;
		if (hiy < 0) hiy = 0;
		if (g_tx < 0 || g_tx > hix) { g_tx = clampf(g_tx, 0, hix); g_vx = 0; }
		if (g_ty < 0 || g_ty > hiy) { g_ty = clampf(g_ty, 0, hiy); g_vy = 0; }

		//---- (4) ask the engine for whole-tile scrolls so the window fits its view ----//
		if (mouse.scroll_x == 0 && mouse.scroll_y == 0)
		{
			int maxsx = zm->max_x_loc - zm->disp_x_loc, maxsy = zm->max_y_loc - zm->disp_y_loc;
			if (maxsx < 0) maxsx = 0;
			if (maxsy < 0) maxsy = 0;
			int nx = want_tile(g_tx, sw, W, ox, maxsx), ny = want_tile(g_ty, sh, H, oy, maxsy);
			if (nx != ox || ny != oy)
			{
				mouse.process_scroll(nx - ox, ny - oy);   // int overload: whole tiles, applied by World::detect_scroll
				g_req_x = nx - ox;
				g_req_y = ny - oy;
			}
		}
	}

	//---- (5) present ----//
	if (!showing)
		return;

	float fx = dst->w / (float)lw, fy = dst->h / (float)lh;
	SDL_Rect md;
	md.x = dst->x + (int)lroundf(mx * fx);
	md.y = dst->y + (int)lroundf(my * fy);
	md.w = dst->x + (int)lroundf((mx + W) * fx) - md.x;
	md.h = dst->y + (int)lroundf((my + H) * fy) - md.y;
	g_view.enabled = 1;
	g_view.map_dst = md;

	int offx = clampi((int)lroundf(g_tx - g_drawn_x), 0, W - sw);
	int offy = clampi((int)lroundf(g_ty - g_drawn_y), 0, H - sh);
	g_sx = mx + offx;
	g_sy = my + offy;
	g_sw = sw;
	g_sh = sh;

	g_view.active = (sw != W || sh != H || offx != 0 || offy != 0) && md.w > 0 && md.h > 0;
	if (g_view.active)
	{
		SDL_Rect src = { g_sx, g_sy, g_sw, g_sh };
		SDL_RenderCopy(r, frame, &src, &md);
	}
}

int sk_camera_minimap_window(float *x, float *y, float *w, float *h)
{
	if (!g_view.enabled || !g_view.active)
		return 0;
	*x = (g_drawn_x + (g_sx - ZOOM_X1)) / (float)TILE_PX;
	*y = (g_drawn_y + (g_sy - ZOOM_Y1)) / (float)TILE_PX;
	*w = g_sw / (float)TILE_PX;
	*h = g_sh / (float)TILE_PX;
	return 1;
}

void sk_camera_get_view(SkCamView *v)
{
	*v = g_view;
}

// forward:  out = map_dst.x + (lx - sx) * map_dst.w / sw        (and the same for y)
// inverse:  lx  = sx + (out - map_dst.x) * sw / map_dst.w
int sk_camera_to_game(float px, float py, int *lx, int *ly)
{
	if (!g_view.enabled || !g_view.active)
		return 0;
	const SDL_Rect &d = g_view.map_dst;
	if (px < d.x || px >= d.x + d.w || py < d.y || py >= d.y + d.h)
		return 0;
	float fx = g_sx + (px - d.x) * g_sw / (float)d.w;
	float fy = g_sy + (py - d.y) * g_sh / (float)d.h;
	*lx = clampi((int)floorf(fx), g_sx, g_sx + g_sw - 1);
	*ly = clampi((int)floorf(fy), g_sy, g_sy + g_sh - 1);
	return 1;
}

void sk_camera_transform(float px0, float py0, float px1, float py1, float ratio)
{
	if (!g_view.enabled || !g_have || g_view.map_dst.w <= 0 || g_view.map_dst.h <= 0)
		return;
	const SDL_Rect &d = g_view.map_dst;
	const int W = config.zoom_width, H = config.zoom_height;
	int sw0 = win_dim(W, g_zoom), sh0 = win_dim(H, g_zoom);
	// world point under the old anchor
	float wx = g_tx + (px0 - d.x) * sw0 / (float)d.w;
	float wy = g_ty + (py0 - d.y) * sh0 / (float)d.h;
	if (ratio > 0.0f)
		g_zoom = clampf(g_zoom * ratio, SK_CAMERA_MIN_ZOOM, SK_CAMERA_MAX_ZOOM);
	int sw1 = win_dim(W, g_zoom), sh1 = win_dim(H, g_zoom);
	// keep that world point under the new anchor at the new scale
	g_tx = wx - (px1 - d.x) * sw1 / (float)d.w;
	g_ty = wy - (py1 - d.y) * sh1 / (float)d.h;
	g_vx = g_vy = 0;
}

void sk_camera_fling(float vx_out, float vy_out)
{
	if (!g_view.enabled || g_view.map_dst.w <= 0 || g_view.map_dst.h <= 0)
		return;
	const SDL_Rect &d = g_view.map_dst;
	int sw = win_dim(config.zoom_width, g_zoom), sh = win_dim(config.zoom_height, g_zoom);
	g_vx = -vx_out * sw / (float)d.w;
	g_vy = -vy_out * sh / (float)d.h;
	float sp = sqrtf(g_vx * g_vx + g_vy * g_vy);
	if (sp > FLING_MAX_PXS)
	{
		g_vx *= FLING_MAX_PXS / sp;
		g_vy *= FLING_MAX_PXS / sp;
	}
	if (sp < FLING_STOP_PXS)
		g_vx = g_vy = 0;
}

void sk_camera_stop_fling(void)
{
	g_vx = g_vy = 0;
}

float sk_camera_get_zoom(void)
{
	return g_zoom;
}

void sk_camera_reset_zoom(void)
{
	if (!g_view.enabled || g_view.map_dst.w <= 0)
		return;
	// zoom about the window centre: anchor = the centre of the map rectangle on screen
	float cx = g_view.map_dst.x + g_view.map_dst.w / 2.0f, cy = g_view.map_dst.y + g_view.map_dst.h / 2.0f;
	sk_camera_transform(cx, cy, cx, cy, SK_CAMERA_DEFAULT_ZOOM / g_zoom);
}

#endif // __ANDROID__
