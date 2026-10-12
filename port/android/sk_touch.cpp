/*
 * Seven Kingdoms Android port: touch input layer.
 *
 * Injection point: the game's own Mouse object (cursor position via Mouse::process_mouse_motion,
 * buttons via Mouse::add_event, map scroll via Mouse::process_scroll), NOT synthetic SDL mouse
 * events. The game keeps its own software cursor (Mouse::cur_x/cur_y, 800x600 logical space) and
 * reads button state from it, so feeding Mouse directly puts every click exactly under the finger
 * and needs no SDL mouse state, grab or warp. Keys (Esc from Back, toolbar hotkeys) are pushed as
 * SDL_KEYDOWN/UP so they take the game's normal keyboard path.
 *
 * Gestures (CONTRACT 5 phase 2):
 *   tap                   left click at the finger
 *   hold >= 400 ms        right click at the finger (growing ring while holding)
 *   one-finger drag       left-button drag (box select)
 *   two-finger tap        right click at the midpoint
 *   two-finger drag       scroll the map (Mouse::process_scroll -> World::detect_scroll)
 *   Back                  Esc
 */
#ifdef __ANDROID__

#include <math.h>
#include <SDL.h>
#include <OMOUSE.h>
#include <OCONFIG.h>
#include "sk_touch.h"

// ---------------------------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------------------------
static const Uint32 LONG_PRESS_MS       = 400;   // hold time for a right click
static const float  TOUCH_SLOP_LOGICAL  = 12.0f; // max movement for a tap/long-press, in game pixels (800x600)
static const Uint32 TWO_TAP_MAX_MS      = 350;   // two-finger tap must be shorter than this
static const Uint32 BUTTON_HOLD_MS      = 60;    // injected click: press-to-release delay (lets polled button state see it)
static const float  SCROLL_PX_PER_CELL  = 32.0f; // map cell size in the zoom window (ZOOM_LOC_WIDTH/HEIGHT)

static const float  TOOLBAR_MIN_MARGIN_FRAC = 0.09f; // side bar needs a margin at least this fraction of screen height
static const float  TOOLBAR_BUTTON_FRAC = 0.15f;     // button size, fraction of screen height (side bar)
static const float  TOOLBAR_GAP_FRAC    = 0.025f;    // gap between buttons, fraction of screen height
static const float  STRIP_BUTTON_FRAC   = 0.11f;     // button/handle size of the collapsible strip

static const float  RING_MAX_FRAC       = 0.07f; // long-press ring radius at 400 ms, fraction of screen height
static const Uint32 RING_DELAY_MS       = 120;   // ring appears after this hold time (taps stay clean)
static const Uint32 RING_FLASH_MS       = 260;   // confirmation flash after a right click

static const int GAME_W = 800;
static const int GAME_H = 600;

// ---------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------
enum Mode { M_IDLE, M_PENDING, M_DRAG, M_LONG, M_TWO, M_WAIT_UP, M_BUTTON };

struct Finger
{
	SDL_FingerID id;
	float x0, y0;  // start, output pixels
	float x, y;    // current, output pixels
};

static SDL_Window   *g_window = NULL;
static SDL_Renderer *g_renderer = NULL;

static Mode   g_mode = M_IDLE;
static Finger g_f[2];
static int    g_game_fingers = 0;   // tracked fingers currently down
static Uint32 g_t0 = 0;             // gesture start time
static bool   g_two_moved = false;
static float  g_two_last_mx = 0, g_two_last_my = 0; // last midpoint used for scrolling
static float  g_scroll_acc_x = 0, g_scroll_acc_y = 0;

static bool   g_release_pending = false;
static Uint32 g_release_at = 0;
static MouseEventType g_release_type = LEFT_BUTTON_RELEASE;

static Uint32 g_flash_at = 0;       // time of last right click (0 = none)
static float  g_flash_x = 0, g_flash_y = 0;

static bool   g_strip_open = false; // collapsible strip (no side margin)
static int    g_btn_finger_hit = -1;   // toolbar item under the finger, -1 none, -2 handle
static SDL_FingerID g_btn_finger = 0;

// ---------------------------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------------------------
struct Rect { int x, y, w, h; };

struct Geometry
{
	int out_w, out_h;
	float scale;            // output px per game px
	float gx, gy, gw, gh;   // game rect in output px
	bool side;              // toolbar lives in the right margin
};

static bool get_geometry(Geometry *g)
{
	if (!g_renderer)
		return false;
	if (SDL_GetRendererOutputSize(g_renderer, &g->out_w, &g->out_h) != 0 || g->out_w <= 0 || g->out_h <= 0)
		return false;
	float sx = (float)g->out_w / GAME_W;
	float sy = (float)g->out_h / GAME_H;
	g->scale = sx < sy ? sx : sy;
	g->gw = GAME_W * g->scale;
	g->gh = GAME_H * g->scale;
	g->gx = (g->out_w - g->gw) / 2.0f;
	g->gy = (g->out_h - g->gh) / 2.0f;
	g->side = g->gx >= g->out_h * TOOLBAR_MIN_MARGIN_FRAC;
	return true;
}

// Output px -> game px. Returns true if inside the game rect; the result is always clamped to it.
static bool to_logical(const Geometry &g, float px, float py, int *lx, int *ly)
{
	float fx = (px - g.gx) / g.scale;
	float fy = (py - g.gy) / g.scale;
	bool inside = fx >= 0 && fx < GAME_W && fy >= 0 && fy < GAME_H;
	if (fx < 0) fx = 0;
	if (fx > GAME_W - 1) fx = GAME_W - 1;
	if (fy < 0) fy = 0;
	if (fy > GAME_H - 1) fy = GAME_H - 1;
	*lx = (int)fx;
	*ly = (int)fy;
	return inside;
}

// ---------------------------------------------------------------------------------------------
// Toolbar items
// ---------------------------------------------------------------------------------------------
enum Glyph { G_PAUSE, G_SLOWER, G_FASTER, G_K, G_G, G_D };
enum { ITEM_COUNT = 6, ITEM_HANDLE = -2 };

struct Item { Glyph glyph; };

// Hotkeys with no mouse equivalent (docs/AUDIT.md E4, OSYS.cpp detect_set_speed / detect_letter_key):
//   pause = Space, slower/faster = digit keys 1..9 (speed 3*n), K/G/D = jump to king / general,
//   open oldest diplomatic message.
static const Item ITEMS[ITEM_COUNT] = {
	{ G_PAUSE }, { G_SLOWER }, { G_FASTER }, { G_K }, { G_G }, { G_D },
};

// Item rectangles for the current geometry; returns the number of visible items (0 if hidden).
static int layout_items(const Geometry &g, Rect *items, Rect *handle)
{
	handle->w = 0;
	if (g.side)
	{
		int margin = (int)g.gx;
		int gap = (int)(g.out_h * TOOLBAR_GAP_FRAC);
		int b = (int)(g.out_h * TOOLBAR_BUTTON_FRAC);
		int maxb = (g.out_h - (ITEM_COUNT + 1) * gap) / ITEM_COUNT;
		if (b > maxb) b = maxb;
		if (b > margin - 2 * gap) b = margin - 2 * gap;
		int total = ITEM_COUNT * b + (ITEM_COUNT - 1) * gap;
		int x = (int)(g.gx + g.gw) + (margin - b) / 2;
		int y = (g.out_h - total) / 2;
		for (int i = 0; i < ITEM_COUNT; i++)
			items[i] = Rect{ x, y + i * (b + gap), b, b };
		return ITEM_COUNT;
	}
	int b = (int)(g.out_h * STRIP_BUTTON_FRAC);
	int gap = b / 8;
	*handle = Rect{ 0, 0, b, b };
	if (!g_strip_open)
		return 0;
	for (int i = 0; i < ITEM_COUNT; i++)
		items[i] = Rect{ b + gap + i * (b + gap), 0, b, b };
	return ITEM_COUNT;
}

static bool in_rect(const Rect &r, float x, float y)
{
	return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// Which toolbar item is at (x,y)? -1 none, ITEM_HANDLE for the strip handle, else the index.
static int toolbar_hit(const Geometry &g, float x, float y)
{
	Rect items[ITEM_COUNT], handle;
	int n = layout_items(g, items, &handle);
	if (in_rect(handle, x, y))
		return ITEM_HANDLE;
	for (int i = 0; i < n; i++)
		if (in_rect(items[i], x, y))
			return i;
	// In the side margin a touch that misses every button is still not a game click.
	if (g.side && (x >= g.gx + g.gw || x < g.gx))
		return -3;
	return -1;
}

// ---------------------------------------------------------------------------------------------
// Injection helpers
// ---------------------------------------------------------------------------------------------
static void push_key(SDL_Keycode sym)
{
	SDL_Event e;
	SDL_zero(e);
	e.type = SDL_KEYDOWN;
	e.key.state = SDL_PRESSED;
	e.key.windowID = g_window ? SDL_GetWindowID(g_window) : 0;
	e.key.keysym.sym = sym;
	e.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
	e.key.keysym.mod = KMOD_NONE;
	SDL_PushEvent(&e);
	e.type = SDL_KEYUP;
	e.key.state = SDL_RELEASED;
	SDL_PushEvent(&e);
}

static void speed_key(bool faster)
{
	int s = config.frame_speed;
	int step = s >= 99 ? 8 : (s + 2) / 3;   // 0 (paused) -> 0, 3 -> 1, 24 -> 8, 99 -> 8
	if (faster)
		step = step >= 8 ? 9 : step + 1;     // 9 = key '9' = fastest
	else if (step > 1)
		step--;
	else
		step = 1;
	push_key((SDL_Keycode)('0' + step));
}

static void fire_item(int i)
{
	switch (ITEMS[i].glyph)
	{
	case G_PAUSE:  push_key(SDLK_SPACE); break;
	case G_SLOWER: speed_key(false); break;
	case G_FASTER: speed_key(true); break;
	case G_K:      push_key(SDLK_k); break;
	case G_G:      push_key(SDLK_g); break;
	case G_D:      push_key(SDLK_d); break;
	}
}

static void move_cursor(int lx, int ly)
{
	mouse.process_mouse_motion(lx, ly);
}

static void flush_release(void)
{
	if (!g_release_pending)
		return;
	g_release_pending = false;
	mouse.add_event(g_release_type);
	if (g_release_type == LEFT_BUTTON_RELEASE)
		mouse.reset_boundary();
}

static void schedule_release(MouseEventType type)
{
	g_release_pending = true;
	g_release_type = type;
	g_release_at = SDL_GetTicks() + BUTTON_HOLD_MS;
}

// Click = move, press now, release BUTTON_HOLD_MS later (from sk_touch_update).
static void click_at(const Geometry &g, float px, float py, bool right)
{
	flush_release();
	int lx, ly;
	to_logical(g, px, py, &lx, &ly);
	move_cursor(lx, ly);
	mouse.add_event(right ? RIGHT_BUTTON : LEFT_BUTTON);
	schedule_release(right ? RIGHT_BUTTON_RELEASE : LEFT_BUTTON_RELEASE);
	if (right)
	{
		g_flash_at = SDL_GetTicks();
		g_flash_x = px;
		g_flash_y = py;
	}
}

static Finger *find_finger(SDL_FingerID id)
{
	for (int i = 0; i < 2; i++)
		if (g_f[i].id == id && i < g_game_fingers)
			return &g_f[i];
	return NULL;
}

static void reset_state(void)
{
	if (g_mode == M_DRAG)
	{
		mouse.add_event(LEFT_BUTTON_RELEASE);
		mouse.reset_boundary();
	}
	flush_release();
	g_mode = M_IDLE;
	g_game_fingers = 0;
	g_scroll_acc_x = g_scroll_acc_y = 0;
}

// ---------------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------------
void sk_touch_init(void)
{
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");      // we synthesise the clicks ourselves
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1"); // Back arrives as a key instead of closing the app
	SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "touch: layer initialised (touch->mouse off, Back trapped)");
}

void sk_touch_update(void)
{
	SDL_ShowCursor(SDL_DISABLE);   // the game re-enables the system cursor on focus loss; touch has none
	Uint32 now = SDL_GetTicks();
	if (g_release_pending && (Sint32)(now - g_release_at) >= 0)
		flush_release();

	if (g_mode == M_PENDING && (Sint32)(now - g_t0) >= (Sint32)LONG_PRESS_MS)
	{
		Geometry g;
		if (get_geometry(&g))
		{
			click_at(g, g_f[0].x0, g_f[0].y0, true);
			g_mode = M_LONG;
		}
	}
}

int sk_touch_handle_event(const SDL_Event *e, SDL_Window *window, SDL_Renderer *renderer)
{
	g_window = window;
	g_renderer = renderer;

	switch (e->type)
	{
	case SDL_KEYDOWN:
	case SDL_KEYUP:
		if (e->key.keysym.sym == SDLK_AC_BACK)
		{
			if (e->type == SDL_KEYDOWN && !e->key.repeat)
			{
				push_key(SDLK_ESCAPE);
			}
			return 1;
		}
		return 0;

	case SDL_WINDOWEVENT:
		if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST || e->window.event == SDL_WINDOWEVENT_MINIMIZED)
		{
			reset_state();
		}
		return 0;

	case SDL_MULTIGESTURE:
		return 1;   // replaced by our own two-finger handling

	case SDL_FINGERDOWN:
	case SDL_FINGERMOTION:
	case SDL_FINGERUP:
		break;

	default:
		return 0;
	}

	Geometry g;
	if (!get_geometry(&g))
		return 1;
	float px = e->tfinger.x * g.out_w;
	float py = e->tfinger.y * g.out_h;
	SDL_FingerID id = e->tfinger.fingerId;
	Uint32 now = SDL_GetTicks();
	float slop = TOUCH_SLOP_LOGICAL * g.scale;

	//---------------------------------------------------------------- toolbar finger
	if (g_mode == M_BUTTON)
	{
		if (id != g_btn_finger)
			return 1;   // extra fingers are ignored while a button is held
		if (e->type == SDL_FINGERMOTION)
		{
			// keep the highlight only while still over the same item
			if (toolbar_hit(g, px, py) != g_btn_finger_hit)
				g_btn_finger_hit = -100;
		}
		else if (e->type == SDL_FINGERUP)
		{
			int hit = toolbar_hit(g, px, py);
			if (hit >= 0 || hit == ITEM_HANDLE)
			{
				if (hit == ITEM_HANDLE)
					g_strip_open = !g_strip_open;
				else if (hit == g_btn_finger_hit)
					fire_item(hit);
			}
			g_mode = M_IDLE;
		}
		return 1;
	}

	//---------------------------------------------------------------- finger down
	if (e->type == SDL_FINGERDOWN)
	{
		int hit = toolbar_hit(g, px, py);
		if (hit != -1)
		{
			// Toolbar / handle / dead margin. Only starts a toolbar press when nothing else is going on.
			if (g_mode == M_IDLE && g_game_fingers == 0 && hit != -3)
			{
				g_mode = M_BUTTON;
				g_btn_finger = id;
				g_btn_finger_hit = hit;
			}
			return 1;
		}
		int lx, ly;
		if (!to_logical(g, px, py, &lx, &ly))
			return 1;   // letterbox margin: never a game click

		if (g_mode == M_IDLE)
		{
			flush_release();
			g_f[0] = Finger{ id, px, py, px, py };
			g_game_fingers = 1;
			g_mode = M_PENDING;
			g_t0 = now;
		}
		else if (g_mode == M_PENDING && g_game_fingers == 1)
		{
			g_f[1] = Finger{ id, px, py, px, py };
			g_game_fingers = 2;
			g_mode = M_TWO;
			g_t0 = now;
			g_two_moved = false;
			g_two_last_mx = (g_f[0].x + px) / 2;
			g_two_last_my = (g_f[0].y + py) / 2;
			g_scroll_acc_x = g_scroll_acc_y = 0;
		}
		// else: third finger, or a finger during a drag/long-press: ignored
		return 1;
	}

	//---------------------------------------------------------------- motion / up of tracked fingers
	Finger *f = find_finger(id);
	if (!f)
		return 1;

	if (e->type == SDL_FINGERMOTION)
	{
		f->x = px;
		f->y = py;
		switch (g_mode)
		{
		case M_PENDING:
		{
			float dx = px - f->x0, dy = py - f->y0;
			if (dx * dx + dy * dy >= slop * slop)
			{
				// Drag: press at the start point, then follow the finger.
				int lx, ly;
				flush_release();
				to_logical(g, f->x0, f->y0, &lx, &ly);
				move_cursor(lx, ly);
				mouse.add_event(LEFT_BUTTON);
				to_logical(g, px, py, &lx, &ly);
				move_cursor(lx, ly);
				g_mode = M_DRAG;
			}
			break;
		}
		case M_DRAG:
		{
			int lx, ly;
			to_logical(g, px, py, &lx, &ly);
			move_cursor(lx, ly);
			break;
		}
		case M_TWO:
		{
			float mx = (g_f[0].x + g_f[1].x) / 2, my = (g_f[0].y + g_f[1].y) / 2;
			if (!g_two_moved)
			{
				float sx = (g_f[0].x0 + g_f[1].x0) / 2, sy = (g_f[0].y0 + g_f[1].y0) / 2;
				float d0x = g_f[0].x - g_f[0].x0, d0y = g_f[0].y - g_f[0].y0;
				float d1x = g_f[1].x - g_f[1].x0, d1y = g_f[1].y - g_f[1].y0;
				float dm = (mx - sx) * (mx - sx) + (my - sy) * (my - sy);
				float lim = slop * slop;
				if (d0x * d0x + d0y * d0y >= lim || d1x * d1x + d1y * d1y >= lim || dm >= lim)
				{
					g_two_moved = true;
					g_two_last_mx = sx;   // include the distance travelled before crossing the slop
					g_two_last_my = sy;
				}
			}
			if (g_two_moved)
			{
				// Content follows the fingers: fingers right => view moves left => negative scroll.
				g_scroll_acc_x -= (mx - g_two_last_mx) / g.scale / SCROLL_PX_PER_CELL;
				g_scroll_acc_y -= (my - g_two_last_my) / g.scale / SCROLL_PX_PER_CELL;
				g_two_last_mx = mx;
				g_two_last_my = my;
				int cx = (int)g_scroll_acc_x, cy = (int)g_scroll_acc_y;   // whole cells only, truncating toward 0
				if (cx || cy)
				{
					mouse.process_scroll(cx, cy);
					g_scroll_acc_x -= cx;
					g_scroll_acc_y -= cy;
				}
			}
			break;
		}
		default:
			break;
		}
		return 1;
	}

	// SDL_FINGERUP
	f->x = px;
	f->y = py;
	switch (g_mode)
	{
	case M_PENDING:   // tap
		click_at(g, f->x0, f->y0, false);
		g_mode = M_IDLE;
		g_game_fingers = 0;
		break;
	case M_DRAG:
	{
		int lx, ly;
		to_logical(g, px, py, &lx, &ly);
		move_cursor(lx, ly);
		mouse.add_event(LEFT_BUTTON_RELEASE);
		mouse.reset_boundary();
		g_mode = M_IDLE;
		g_game_fingers = 0;
		break;
	}
	case M_LONG:   // the right click already fired
		g_mode = M_IDLE;
		g_game_fingers = 0;
		break;
	case M_TWO:
		if (!g_two_moved && (Sint32)(now - g_t0) < (Sint32)TWO_TAP_MAX_MS)
			click_at(g, (g_f[0].x0 + g_f[1].x0) / 2, (g_f[0].y0 + g_f[1].y0) / 2, true);
		g_scroll_acc_x = g_scroll_acc_y = 0;
		// Wait for the other finger so its lift or later motion does not start a new gesture.
		g_game_fingers = 1;
		g_f[0] = (f == &g_f[0]) ? g_f[1] : g_f[0];
		g_mode = M_WAIT_UP;
		break;
	case M_WAIT_UP:
		g_game_fingers = 0;
		g_mode = M_IDLE;
		break;
	default:
		break;
	}
	return 1;
}

// ---------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------
static const Uint8 COL_BTN[4]     = { 20, 22, 34, 150 };
static const Uint8 COL_BTN_DOWN[4] = { 210, 175, 70, 200 };
static const Uint8 COL_EDGE[4]    = { 235, 225, 170, 210 };
static const Uint8 COL_GLYPH[4]   = { 245, 240, 215, 240 };

// 5x7 glyphs for the letter buttons, one row per byte, bit 4 = leftmost pixel.
static const Uint8 FONT_K[7] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
static const Uint8 FONT_G[7] = { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F };
static const Uint8 FONT_D[7] = { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };

static void set_col(SDL_Renderer *r, const Uint8 *c, int alpha_scale = 255)
{
	SDL_SetRenderDrawColor(r, c[0], c[1], c[2], (Uint8)(c[3] * alpha_scale / 255));
}

static void fill(SDL_Renderer *r, int x, int y, int w, int h)
{
	SDL_Rect rc = { x, y, w, h };
	SDL_RenderFillRect(r, &rc);
}

static void frame(SDL_Renderer *r, const Rect &b, int t)
{
	fill(r, b.x, b.y, b.w, t);
	fill(r, b.x, b.y + b.h - t, b.w, t);
	fill(r, b.x, b.y, t, b.h);
	fill(r, b.x + b.w - t, b.y, t, b.h);
}

// Filled triangle pointing left or right inside [x, x+w] x [y, y+h], one scanline per row.
static void triangle(SDL_Renderer *r, int x, int y, int w, int h, bool right)
{
	for (int j = 0; j < h; j++)
	{
		float t = 1.0f - fabsf((j + 0.5f) / h * 2.0f - 1.0f);   // 0 at top/bottom, 1 in the middle
		int len = (int)(w * t);
		if (len < 1) continue;
		fill(r, right ? x : x + w - len, y + j, len, 1);
	}
}

static void draw_letter(SDL_Renderer *r, const Rect &b, const Uint8 *rows)
{
	int px = b.w / 10;
	if (px < 1) px = 1;
	int x0 = b.x + (b.w - 5 * px) / 2, y0 = b.y + (b.h - 7 * px) / 2;
	for (int j = 0; j < 7; j++)
		for (int i = 0; i < 5; i++)
			if (rows[j] & (0x10 >> i))
				fill(r, x0 + i * px, y0 + j * px, px, px);
}

static void draw_glyph(SDL_Renderer *r, const Rect &b, Glyph gl)
{
	int m = b.w / 4;
	switch (gl)
	{
	case G_PAUSE:
	{
		int bw = b.w / 7;
		fill(r, b.x + b.w / 2 - bw - bw / 2, b.y + m, bw, b.h - 2 * m);
		fill(r, b.x + b.w / 2 + bw / 2, b.y + m, bw, b.h - 2 * m);
		break;
	}
	case G_SLOWER:
	case G_FASTER:
	{
		bool right = gl == G_FASTER;
		int tw = (b.w - 2 * m) / 2, th = b.h - 2 * m;
		triangle(r, b.x + m, b.y + m, tw, th, right);
		triangle(r, b.x + m + tw, b.y + m, tw, th, right);
		break;
	}
	case G_K: draw_letter(r, b, FONT_K); break;
	case G_G: draw_letter(r, b, FONT_G); break;
	case G_D: draw_letter(r, b, FONT_D); break;
	}
}

// Ring of radius `rad` and thickness `th` (output px), drawn as scanline spans.
static void draw_ring(SDL_Renderer *r, float cx, float cy, float rad, float th)
{
	float ro = rad + th / 2, ri = rad - th / 2;
	if (ri < 0) ri = 0;
	for (int dy = (int)-ro; dy <= (int)ro; dy++)
	{
		float yo = ro * ro - (float)dy * dy;
		if (yo < 0) continue;
		int xo = (int)sqrtf(yo);
		float yi = ri * ri - (float)dy * dy;
		int y = (int)cy + dy;
		if (yi <= 0)
			SDL_RenderDrawLine(r, (int)cx - xo, y, (int)cx + xo, y);
		else
		{
			int xi = (int)sqrtf(yi);
			SDL_RenderDrawLine(r, (int)cx - xo, y, (int)cx - xi, y);
			SDL_RenderDrawLine(r, (int)cx + xi, y, (int)cx + xo, y);
		}
	}
}

static void draw_toolbar(SDL_Renderer *r, const Geometry &g)
{
	Rect items[ITEM_COUNT], handle;
	int n = layout_items(g, items, &handle);
	int edge = g.out_h / 300 + 1;

	if (handle.w > 0)
	{
		bool down = g_mode == M_BUTTON && g_btn_finger_hit == ITEM_HANDLE;
		set_col(r, down ? COL_BTN_DOWN : COL_BTN);
		fill(r, handle.x, handle.y, handle.w, handle.h);
		set_col(r, COL_EDGE);
		frame(r, handle, edge);
		set_col(r, COL_GLYPH);
		int bh = handle.h / 9;      // three bars: "menu" icon
		for (int i = 0; i < 3; i++)
			fill(r, handle.x + handle.w / 5, handle.y + handle.h / 4 + i * (bh * 2 + bh / 2), handle.w * 3 / 5, bh);
	}
	for (int i = 0; i < n; i++)
	{
		bool down = (g_mode == M_BUTTON && g_btn_finger_hit == i);
		set_col(r, down ? COL_BTN_DOWN : COL_BTN);
		fill(r, items[i].x, items[i].y, items[i].w, items[i].h);
		set_col(r, COL_EDGE);
		frame(r, items[i], edge);
		set_col(r, COL_GLYPH);
		draw_glyph(r, items[i], ITEMS[i].glyph);
	}
}

static void draw_feedback(SDL_Renderer *r, const Geometry &g)
{
	Uint32 now = SDL_GetTicks();
	float rmax = g.out_h * RING_MAX_FRAC;
	float th = rmax / 8 + 2;

	if (g_mode == M_PENDING)
	{
		Uint32 held = now - g_t0;
		if (held >= RING_DELAY_MS)
		{
			float p = (float)(held - RING_DELAY_MS) / (float)(LONG_PRESS_MS - RING_DELAY_MS);
			if (p > 1) p = 1;
			SDL_SetRenderDrawColor(r, 255, 240, 160, (Uint8)(90 + 130 * p));
			draw_ring(r, g_f[0].x0, g_f[0].y0, rmax * (0.25f + 0.75f * p), th);
		}
	}
	if (g_flash_at && (Sint32)(now - g_flash_at) < (Sint32)RING_FLASH_MS)
	{
		float q = (float)(now - g_flash_at) / RING_FLASH_MS;
		SDL_SetRenderDrawColor(r, 255, 255, 255, (Uint8)(230 * (1 - q)));
		draw_ring(r, g_flash_x, g_flash_y, rmax * (1.0f + 0.5f * q), th * 1.5f);
	}
}

void sk_touch_draw(SDL_Renderer *r)
{
	g_renderer = r;
	Geometry g;
	if (!get_geometry(&g))
		return;
	static bool logged = false;   // one startup line
	if (!logged && g_window)
	{
		logged = true;
		int ww = 0, wh = 0;
		SDL_GetWindowSize(g_window, &ww, &wh);
		SDL_Log("touch: window=%dx%d output=%dx%d letterbox rect=(%.1f,%.1f %.1fx%.1f) scale=%.4f",
			ww, wh, g.out_w, g.out_h, g.gx, g.gy, g.gw, g.gh, g.scale);
	}

	// Draw in output pixels so the toolbar can use the letterbox margins: suspend the logical size,
	// then restore it and the draw state the game's flip() relies on.
	int lw = 0, lh = 0;
	SDL_RenderGetLogicalSize(r, &lw, &lh);
	Uint8 cr, cg, cb, ca;
	SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca);
	SDL_BlendMode obm;
	SDL_GetRenderDrawBlendMode(r, &obm);

	SDL_RenderSetLogicalSize(r, 0, 0);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

	draw_toolbar(r, g);
	draw_feedback(r, g);

	SDL_SetRenderDrawBlendMode(r, obm);
	SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
	SDL_RenderSetLogicalSize(r, lw, lh);
}

#endif // __ANDROID__
