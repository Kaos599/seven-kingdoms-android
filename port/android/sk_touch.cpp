/*
 * Seven Kingdoms Android port: touch input layer.
 *
 * Injection point: the game's own Mouse object (cursor position via Mouse::process_mouse_motion,
 * buttons via Mouse::add_event, map scroll via Mouse::process_scroll), NOT synthetic SDL mouse
 * events. The game keeps its own software cursor (Mouse::cur_x/cur_y, logical space VGA_WIDTH x VGA_HEIGHT) and
 * reads button state from it, so feeding Mouse directly puts every click exactly under the finger
 * and needs no SDL mouse state, grab or warp. Keys (Esc from Back, toolbar hotkeys) are pushed as
 * SDL_KEYDOWN/UP so they take the game's normal keyboard path.
 *
 * Gestures (CONTRACT 5 phase 2, PHONE-UX-CONTRACT):
 *   tap                   smart tap: RIGHT click when the game would treat a right click there as an order for the
 *                         selected own units (sk_game_tap_is_command, patch 0011), else LEFT click (select)
 *   hold >= 400 ms        growing ring, then armed; lift without moving = the OPPOSITE click of what a tap would do
 *   hold, then move       box-select (game's own left-button drag frame) from the press point; lift ends it
 *   one-finger drag       on the main map: pan the camera (sk_camera.cpp), fling on release; elsewhere: left-button drag
 *   two-finger tap        right click at the midpoint
 *   two fingers on map    pinch zoom about the midpoint + pan (camera); elsewhere: scroll in whole cells
 *   Back                  Esc (closes the cheats panel first when it is open)
 *   toolbar               Pause, Slower, Faster, K, G, D, "C" cheats panel (sk_cheats.cpp), Deselect, Zoom reset,
 *                         Groups 1-5 (tap = select group, hold 0.5 s = assign the selection to the group)
 */
#ifdef __ANDROID__

#include <math.h>
#include <SDL.h>
#include <OMOUSE.h>
#include <OCONFIG.h>
#include "sk_touch.h"
#include "sk_cheats.h"
#include "sk_camera.h"
#include "sk_game.h"

// ---------------------------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------------------------
static const Uint32 LONG_PRESS_MS       = 400;   // hold time for a right click
static const float  TOUCH_SLOP_LOGICAL  = 12.0f; // max movement for a tap/long-press, in game pixels (scale ~2 on every supported phone)
static const Uint32 TWO_TAP_MAX_MS      = 350;   // two-finger tap must be shorter than this
static const Uint32 BUTTON_HOLD_MS      = 60;    // injected click: press-to-release delay (lets polled button state see it)
static const float  SCROLL_PX_PER_CELL  = 32.0f; // map cell size in the zoom window (ZOOM_LOC_WIDTH/HEIGHT)
static const Uint32 FLING_SAMPLE_MS     = 100;   // finger velocity = movement over the last this-many ms
static const float  FLING_MIN_SPEED_FRAC = 0.25f; // release speed (screen heights per second) below which no fling starts

static const Uint32 GROUP_HOLD_MS       = 500;   // toolbar group button: hold this long to assign the selection to the group
static const Uint32 ITEM_FLASH_MS       = 300;   // toolbar button highlight after it fired
static const int    BUTTON_MIN_LOGICAL  = 44;    // smallest toolbar button, game px (touch-target floor)
static const float  TOOLBAR_BUTTON_FRAC = 0.15f;     // button size, fraction of screen height (side bar)
static const float  TOOLBAR_GAP_FRAC    = 0.025f;    // gap between buttons, fraction of screen height
static const float  STRIP_BUTTON_FRAC   = 0.11f;     // button/handle size of the collapsible strip

static const float  RING_MAX_FRAC       = 0.07f; // long-press ring radius at 400 ms, fraction of screen height
static const Uint32 RING_DELAY_MS       = 120;   // ring appears after this hold time (taps stay clean)
static const Uint32 RING_FLASH_MS       = 260;   // confirmation flash after a right click

// 4:3 base size. Used (a) as the logical size when config.win_* is not set yet, and (b) to tell wide
// mode (logical size differs from it) from the unchanged 4:3 mode. Menus stay this size when centred.
static const int BASE_W = 800;
static const int BASE_H = 600;

// Wide mode (logical size != 4:3 base). Logical game pixels; the map view starts below the top bar
// (ZOOM_Y1) and ends at config.zoom_width, where the right block (map-mode buttons, minimap, panel) begins.
static const int WIDE_TOP_BAR_H      = 56;  // ZOOM_Y1: top bar height
static const int WIDE_BTN_LOGICAL    = 50;  // handle / strip button size (~100 physical px at scale 2)
static const int WIDE_GAP_LOGICAL    = 6;   // gap between strip buttons
static const int WIDE_HANDLE_RIGHT_PAD = 8; // handle's right edge sits this far left of zoom_width
static const float WIDE_PANEL_MAP_FRAC = 1.0f / 3.0f; // cheats panel width as a fraction of the map view (zoom_width)

// ---------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------
// M_LONG = the ring completed (armed): lift = opposite click, move = box-select (becomes M_DRAG).
enum Mode { M_IDLE, M_PENDING, M_DRAG, M_LONG, M_TWO, M_WAIT_UP, M_BUTTON, M_PAN };

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
static bool   g_two_cam = false;    // the two-finger gesture drives the camera (started on the map)
static float  g_two_last_dist = 0;  // finger spread at the last camera update
static bool   g_pan_start_map = false; // the single finger went down inside the map rectangle (camera pan on drag)
static bool   g_drag_cam = false;   // the left-button drag is a box-select on the map: both ends go through the camera
static float  g_pan_last_x = 0, g_pan_last_y = 0;

struct Sample { Uint32 t; float x, y; };
static const int N_SAMPLES = 8;
static Sample g_samples[N_SAMPLES];  // recent finger positions for the fling velocity (ring buffer)
static int    g_sample_n = 0;

static bool   g_release_pending = false;
static Uint32 g_release_at = 0;
static MouseEventType g_release_type = LEFT_BUTTON_RELEASE;

static Uint32 g_flash_at = 0;       // time of last right click (0 = none)
static float  g_flash_x = 0, g_flash_y = 0;

static bool   g_strip_open = false; // collapsible strip (no side margin)
static int    g_btn_finger_hit = -1;   // toolbar item under the finger, -1 none, -2 handle
static SDL_FingerID g_btn_finger = 0;
static Uint32 g_btn_t0 = 0;             // toolbar press time (group buttons: hold to assign)
static bool   g_btn_done = false;       // the press already fired (hold action): the lift must not fire again
static int    g_item_flash = -1;        // toolbar item that just fired (highlight), -1 none
static Uint32 g_item_flash_at = 0;

// ---------------------------------------------------------------------------------------------
// Toolbar items
// ---------------------------------------------------------------------------------------------
enum Glyph { G_PAUSE, G_SLOWER, G_FASTER, G_K, G_G, G_D, G_C, G_DESEL, G_ZOOM, G_GRP };
enum { ITEM_COUNT = 15, ITEM_HANDLE = -2 };

struct Item { Glyph glyph; int arg; };   // arg: group number for G_GRP

// Hotkeys with no mouse equivalent (docs/AUDIT.md E4, OSYS.cpp detect_set_speed / detect_letter_key):
//   pause = Space, slower/faster = digit keys 1..9 (speed 3*n), K/G/D = jump to king / general,
//   open oldest diplomatic message, C = cheats panel (Android-only, sk_cheats.cpp).
// Added for phones: Deselect, Zoom reset (camera) and Groups 1-5 (Alt+n / Ctrl+n of the game).
static const Item ITEMS[ITEM_COUNT] = {
	{ G_PAUSE, 0 }, { G_SLOWER, 0 }, { G_FASTER, 0 }, { G_K, 0 }, { G_G, 0 }, { G_D, 0 }, { G_C, 0 },
	{ G_DESEL, 0 }, { G_ZOOM, 0 },
	{ G_GRP, 1 }, { G_GRP, 2 }, { G_GRP, 3 }, { G_GRP, 4 }, { G_GRP, 5 },
};

// ---------------------------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------------------------
struct Rect { int x, y, w, h; };

struct Geometry
{
	int out_w, out_h;
	int lw, lh;             // logical game size (VGA_WIDTH x VGA_HEIGHT), read at call time
	bool wide;              // logical size differs from the 4:3 base: wide toolbar/cheats layout
	bool in_game;           // a game is running (not a menu); only meaningful in wide mode
	int zoom_w;             // config.zoom_width: map view width in game px (wide mode)
	float scale;            // output px per game px
	float gx, gy, gw, gh;   // game rect in output px
	bool side;              // toolbar lives in the right margin
};

static bool side_layout(const Geometry &g, int *cols, int *b, int *gap);

static bool get_geometry(Geometry *g)
{
	if (!g_renderer)
		return false;
	if (SDL_GetRendererOutputSize(g_renderer, &g->out_w, &g->out_h) != 0 || g->out_w <= 0 || g->out_h <= 0)
		return false;
	g->lw = config.win_width;
	g->lh = config.win_height;
	if (g->lw <= 0 || g->lh <= 0)
	{
		g->lw = BASE_W;
		g->lh = BASE_H;
	}
	g->wide = g->lw != BASE_W || g->lh != BASE_H;
	g->zoom_w = config.zoom_width;
	g->in_game = true;
	if (g->wide)
	{
		SkCheatsGameState st;
		sk_cheats_get_game_state(&st);
		g->in_game = st.in_game != 0;
		if (g->zoom_w <= 0 || g->zoom_w > g->lw)
			g->zoom_w = g->lw;
	}
	float sx = (float)g->out_w / g->lw;
	float sy = (float)g->out_h / g->lh;
	g->scale = sx < sy ? sx : sy;
	g->gw = g->lw * g->scale;
	g->gh = g->lh * g->scale;
	g->gx = (g->out_w - g->gw) / 2.0f;
	g->gy = (g->out_h - g->gh) / 2.0f;
	int sc, sb, sg;
	g->side = !g->wide && side_layout(*g, &sc, &sb, &sg);   // margin wide enough for >= 44 px buttons in <= 3 columns
	return true;
}

// Output px -> game px. Returns true if inside the game rect; the result is always clamped to it.
static bool to_logical(const Geometry &g, float px, float py, int *lx, int *ly)
{
	float fx = (px - g.gx) / g.scale;
	float fy = (py - g.gy) / g.scale;
	bool inside = fx >= 0 && fx < g.lw && fy >= 0 && fy < g.lh;
	if (fx < 0) fx = 0;
	if (fx > g.lw - 1) fx = (float)(g.lw - 1);
	if (fy < 0) fy = 0;
	if (fy > g.lh - 1) fy = (float)(g.lh - 1);
	*lx = (int)fx;
	*ly = (int)fy;
	return inside;
}

static int layout_items(const Geometry &g, Rect *items, Rect *handle);

// Geometry the cheats panel needs (it lives in the left letterbox margin, or overlays the game).
static SkCheatsView cheats_view(const Geometry &g)
{
	SkCheatsView v;
	v.out_w = g.out_w;
	v.out_h = g.out_h;
	v.game_x = g.gx;
	v.game_w = g.gw;
	int b = (int)(g.out_h * STRIP_BUTTON_FRAC);
	v.strip_h = b + b / 8;   // overlay mode stays clear of the collapsible strip row(s)
	if (!g.wide && !g.side && g_strip_open)
	{
		Rect items[ITEM_COUNT], handle;
		int n = layout_items(g, items, &handle);
		for (int i = 0; i < n; i++)
			if (items[i].y + items[i].h + b / 8 > v.strip_h)
				v.strip_h = items[i].y + items[i].h + b / 8;
	}
	v.wide = g.wide ? 1 : 0;
	if (g.wide)
	{
		// Wide panel: slides in over the left part of the map view, below the top bar, clear of the right block.
		float top = WIDE_TOP_BAR_H * g.scale;
		v.wide_x = g.gx;
		v.wide_y = g.gy + top;
		v.wide_w = g.zoom_w * WIDE_PANEL_MAP_FRAC * g.scale;
		v.wide_h = g.gh - top;
	}
	else
		v.wide_x = v.wide_y = v.wide_w = v.wide_h = 0;
	return v;
}

// ---------------------------------------------------------------------------------------------
// Toolbar layout
// ---------------------------------------------------------------------------------------------
// Side bar (4:3, letterbox margin on the right): the first column count (1..3) whose buttons are >= 44 game px.
static bool side_layout(const Geometry &g, int *cols_out, int *b_out, int *gap_out)
{
	int margin = (int)g.gx;
	int gap = (int)(g.out_h * TOOLBAR_GAP_FRAC);
	int bmin = (int)(BUTTON_MIN_LOGICAL * g.scale + 0.5f);
	for (int cols = 1; cols <= 3; cols++)
	{
		int rows = (ITEM_COUNT + cols - 1) / cols;
		int b = (int)(g.out_h * TOOLBAR_BUTTON_FRAC);
		int maxb = (g.out_h - (rows + 1) * gap) / rows;
		if (b > maxb) b = maxb;
		int maxw = (margin - (cols + 1) * gap) / cols;
		if (b > maxw) b = maxw;
		if (b >= bmin)
		{
			*cols_out = cols;
			*b_out = b;
			*gap_out = gap;
			return true;
		}
	}
	return false;
}

// Item rectangles for the current geometry; returns the number of visible items (0 if hidden).
static int layout_items(const Geometry &g, Rect *items, Rect *handle)
{
	handle->w = 0;
	if (g.wide)
	{
		// Menus (800x600 art centred in the wide frame): the toolbar's hotkeys do nothing there (pause, speed,
		// king/general/diplomacy keys, cheats), and its natural spot would overlap the centred art. Hidden.
		if (!g.in_game)
			return 0;
		// In game: handle in the top-bar stone filler, right of the date text and left of the right block.
		int b = (int)(WIDE_BTN_LOGICAL * g.scale);
		int gap = (int)(WIDE_GAP_LOGICAL * g.scale);
		int hx = (int)(g.gx + (g.zoom_w - WIDE_HANDLE_RIGHT_PAD - WIDE_BTN_LOGICAL) * g.scale);
		int hy = (int)(g.gy + (WIDE_TOP_BAR_H - WIDE_BTN_LOGICAL) * 0.5f * g.scale);
		*handle = Rect{ hx, hy, b, b };
		if (!g_strip_open)
			return 0;
		// The strip hangs downward from the handle over the map view (x < zoom_width, y >= top bar); when the map
		// view is too short for one column it continues in further columns to the left.
		int y0 = (int)(g.gy + WIDE_TOP_BAR_H * g.scale) + gap;
		int rows = ((int)(g.gy + g.gh) - y0) / (b + gap);
		if (rows < 1) rows = 1;
		if (rows > ITEM_COUNT) rows = ITEM_COUNT;
		for (int i = 0; i < ITEM_COUNT; i++)
			items[i] = Rect{ hx - (i / rows) * (b + gap), y0 + (i % rows) * (b + gap), b, b };
		return ITEM_COUNT;
	}
	if (g.side)
	{
		int cols, b, gap;
		side_layout(g, &cols, &b, &gap);
		int margin = (int)g.gx;
		int rows = (ITEM_COUNT + cols - 1) / cols;
		int total_w = cols * b + (cols - 1) * gap;
		int total_h = rows * b + (rows - 1) * gap;
		int x0 = (int)(g.gx + g.gw) + (margin - total_w) / 2;
		int y0 = (g.out_h - total_h) / 2;
		for (int i = 0; i < ITEM_COUNT; i++)
			items[i] = Rect{ x0 + (i / rows) * (b + gap), y0 + (i % rows) * (b + gap), b, b };
		return ITEM_COUNT;
	}
	int b = (int)(g.out_h * STRIP_BUTTON_FRAC);
	int gap = b / 8;
	*handle = Rect{ 0, 0, b, b };
	if (!g_strip_open)
		return 0;
	// Row of buttons right of the handle; wraps into further rows when the screen is not wide enough.
	int per_row = (g.out_w - (b + gap)) / (b + gap);
	if (per_row < 1) per_row = 1;
	for (int i = 0; i < ITEM_COUNT; i++)
		items[i] = Rect{ b + gap + (i % per_row) * (b + gap), (i / per_row) * (b + gap), b, b };
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
	g_item_flash = i;
	g_item_flash_at = SDL_GetTicks();
	switch (ITEMS[i].glyph)
	{
	case G_PAUSE:  push_key(SDLK_SPACE); break;
	case G_SLOWER: speed_key(false); break;
	case G_FASTER: speed_key(true); break;
	case G_K:      push_key(SDLK_k); break;
	case G_G:      push_key(SDLK_g); break;
	case G_D:      push_key(SDLK_d); break;
	case G_C:      sk_cheats_toggle(); break;
	case G_DESEL:  sk_game_request(SK_ACT_DESELECT, 0); break;               // Power::reset_selection (patch 0011)
	case G_ZOOM:   sk_camera_reset_zoom(); break;
	case G_GRP:    sk_game_request(SK_ACT_GROUP_SELECT, ITEMS[i].arg); break; // GroupSelect::select_grouped_units
	}
}

// Hold action of a toolbar item (group buttons only): assign the current selection to the group.
static void hold_item(int i)
{
	g_item_flash = i;
	g_item_flash_at = SDL_GetTicks();
	if (ITEMS[i].glyph == G_GRP)
		sk_game_request(SK_ACT_GROUP_ASSIGN, ITEMS[i].arg);                  // GroupSelect::group_units
}

// Output px -> game px for a click. Inside the map rectangle while the camera is active the inverse camera mapping
// applies (exact inverse of what sk_camera_present drew); everywhere else it is the plain letterbox mapping.
static bool to_game(const Geometry &g, float px, float py, int *lx, int *ly)
{
	if (sk_camera_to_game(px, py, lx, ly))
		return true;
	return to_logical(g, px, py, lx, ly);
}

// Like to_game, but a point outside the map rectangle is first pulled onto its edge (box-select end points that
// leave the map stay on the map instead of falling back to the 1:1 letterbox mapping).
static void to_game_in_map(const Geometry &g, float px, float py, int *lx, int *ly)
{
	SkCamView v;
	sk_camera_get_view(&v);
	if (v.enabled && v.map_dst.w > 0 && v.map_dst.h > 0)
	{
		if (px < v.map_dst.x) px = (float)v.map_dst.x;
		if (px > v.map_dst.x + v.map_dst.w - 1) px = (float)(v.map_dst.x + v.map_dst.w - 1);
		if (py < v.map_dst.y) py = (float)v.map_dst.y;
		if (py > v.map_dst.y + v.map_dst.h - 1) py = (float)(v.map_dst.y + v.map_dst.h - 1);
	}
	to_game(g, px, py, lx, ly);
}

static bool in_map_rect(float px, float py)
{
	SkCamView v;
	sk_camera_get_view(&v);
	return v.enabled && px >= v.map_dst.x && px < v.map_dst.x + v.map_dst.w &&
		py >= v.map_dst.y && py < v.map_dst.y + v.map_dst.h;
}

static void sample_reset(void)
{
	g_sample_n = 0;
}

static void sample_add(Uint32 t, float x, float y)
{
	g_samples[g_sample_n % N_SAMPLES] = Sample{ t, x, y };
	g_sample_n++;
}

// Finger velocity (output px/s) over the last FLING_SAMPLE_MS before `now`. Returns false if too little data.
static bool sample_velocity(Uint32 now, float *vx, float *vy)
{
	int n = g_sample_n < N_SAMPLES ? g_sample_n : N_SAMPLES;
	if (n < 2)
		return false;
	const Sample &last = g_samples[(g_sample_n - 1) % N_SAMPLES];
	if ((Sint32)(now - last.t) > (Sint32)FLING_SAMPLE_MS)
		return false;   // finger rested before lifting
	const Sample *first = &last;
	for (int i = 2; i <= n; i++)
	{
		const Sample &s = g_samples[(g_sample_n - i) % N_SAMPLES];
		if ((Sint32)(last.t - s.t) > (Sint32)FLING_SAMPLE_MS)
			break;
		first = &s;
	}
	Uint32 dt = last.t - first->t;
	if (dt < 8)
		return false;
	*vx = (last.x - first->x) * 1000.0f / dt;
	*vy = (last.y - first->y) * 1000.0f / dt;
	return true;
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

// Click = move, press now, release BUTTON_HOLD_MS later (from sk_touch_update). The cursor is moved to the point first
// and stays there until the next gesture: Power::detect_action reads mouse.cur_x/cur_y (OPOWER.cpp, test_detect) when
// the game processes the press a frame later, not the click position stored in the event.
// flash: confirmation ring (always for right clicks).
static void click_at(const Geometry &g, float px, float py, bool right, bool flash = false)
{
	flush_release();
	int lx, ly;
	to_game(g, px, py, &lx, &ly);
	move_cursor(lx, ly);
	mouse.add_event(right ? RIGHT_BUTTON : LEFT_BUTTON);
	schedule_release(right ? RIGHT_BUTTON_RELEASE : LEFT_BUTTON_RELEASE);
	if (right || flash)
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
	g_two_cam = false;
	g_pan_start_map = false;
	g_drag_cam = false;
	g_btn_done = false;
	sample_reset();
	sk_camera_stop_fling();
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
	sk_cheats_update(g_window);
	if (config.win_width != BASE_W || config.win_height != BASE_H)
	{
		// Wide mode: the toolbar and the cheats panel only exist in game; drop them when a menu shows.
		SkCheatsGameState st;
		sk_cheats_get_game_state(&st);
		if (!st.in_game)
		{
			g_strip_open = false;
			sk_cheats_close();
		}
	}
	SDL_ShowCursor(SDL_DISABLE);   // the game re-enables the system cursor on focus loss; touch has none
	Uint32 now = SDL_GetTicks();
	if (g_release_pending && (Sint32)(now - g_release_at) >= 0)
		flush_release();

	// Ring complete: armed. The click happens on lift (opposite of a tap) or the gesture becomes a box-select.
	if (g_mode == M_PENDING && (Sint32)(now - g_t0) >= (Sint32)LONG_PRESS_MS)
		g_mode = M_LONG;

	// Group button held: assign the current selection to the group (once; the lift then does nothing).
	if (g_mode == M_BUTTON && !g_btn_done && g_btn_finger_hit >= 0 && g_btn_finger_hit < ITEM_COUNT &&
		ITEMS[g_btn_finger_hit].glyph == G_GRP && (Sint32)(now - g_btn_t0) >= (Sint32)GROUP_HOLD_MS)
	{
		g_btn_done = true;
		hold_item(g_btn_finger_hit);
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
				if (sk_cheats_is_open())
					sk_cheats_close();   // Back closes the panel instead of sending Esc
				else
					push_key(SDLK_ESCAPE);
			}
			return 1;
		}
		return 0;

	case SDL_WINDOWEVENT:
		if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST || e->window.event == SDL_WINDOWEVENT_MINIMIZED)
		{
			reset_state();
			sk_cheats_reset_touch();
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

	//---------------------------------------------------------------- cheats panel (never reaches the game)
	{
		SkCheatsView cv = cheats_view(g);
		int type = e->type == SDL_FINGERDOWN ? 0 : (e->type == SDL_FINGERMOTION ? 1 : 2);
		if (sk_cheats_touch(&cv, type, id, px, py))
			return 1;
	}

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
				else if (hit == g_btn_finger_hit && !g_btn_done)
					fire_item(hit);
			}
			g_mode = M_IDLE;
		}
		return 1;
	}

	//---------------------------------------------------------------- finger down
	if (e->type == SDL_FINGERDOWN)
	{
		sk_camera_stop_fling();   // a new touch stops a fling
		int hit = toolbar_hit(g, px, py);
		if (hit != -1)
		{
			// Toolbar / handle / dead margin. Only starts a toolbar press when nothing else is going on.
			if (g_mode == M_IDLE && g_game_fingers == 0 && hit != -3)
			{
				g_mode = M_BUTTON;
				g_btn_finger = id;
				g_btn_finger_hit = hit;
				g_btn_t0 = now;
				g_btn_done = false;
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
			g_pan_start_map = in_map_rect(px, py);   // a drag from here pans the camera (or box-selects after the ring)
			g_drag_cam = false;
			sample_reset();
		}
		else if ((g_mode == M_PENDING || g_mode == M_PAN) && g_game_fingers == 1)
		{
			bool was_pan = g_mode == M_PAN;
			g_f[1] = Finger{ id, px, py, px, py };
			g_game_fingers = 2;
			g_mode = M_TWO;
			g_t0 = now;
			g_two_moved = false;
			g_two_last_mx = (g_f[0].x + px) / 2;
			g_two_last_my = (g_f[0].y + py) / 2;
			g_scroll_acc_x = g_scroll_acc_y = 0;
			// Two fingers that start on the map drive the camera (pinch + pan); a pan in progress joins in at once.
			g_two_cam = in_map_rect(g_two_last_mx, g_two_last_my) && (was_pan || g_pan_start_map);
			g_two_last_dist = hypotf(px - g_f[0].x, py - g_f[0].y);
			if (was_pan && g_two_cam)
				g_two_moved = true;
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
			if (dx * dx + dy * dy >= slop * slop && g_pan_start_map)
			{
				// Drag that started on the main map before the ring completed: pan the camera.
				g_mode = M_PAN;
				sample_reset();
				sample_add(e->tfinger.timestamp, f->x0, f->y0);
				sample_add(e->tfinger.timestamp, px, py);
				sk_camera_transform(f->x0, f->y0, px, py, 1.0f);   // include the distance travelled before the slop
				g_pan_last_x = px;
				g_pan_last_y = py;
			}
			else if (dx * dx + dy * dy >= slop * slop)
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
		case M_LONG:
		{
			// Moved after the ring: box-select from the press point (the game's own left-button drag frame).
			float dx = px - f->x0, dy = py - f->y0;
			if (g_pan_start_map && dx * dx + dy * dy >= slop * slop)
			{
				int lx, ly;
				flush_release();
				to_game_in_map(g, f->x0, f->y0, &lx, &ly);
				move_cursor(lx, ly);
				mouse.add_event(LEFT_BUTTON);
				to_game_in_map(g, px, py, &lx, &ly);
				move_cursor(lx, ly);
				g_drag_cam = true;
				g_mode = M_DRAG;
			}
			break;
		}
		case M_DRAG:
		{
			int lx, ly;
			if (g_drag_cam)
				to_game_in_map(g, px, py, &lx, &ly);
			else
				to_logical(g, px, py, &lx, &ly);
			move_cursor(lx, ly);
			break;
		}
		case M_PAN:
			sk_camera_transform(g_pan_last_x, g_pan_last_y, px, py, 1.0f);
			g_pan_last_x = px;
			g_pan_last_y = py;
			sample_add(e->tfinger.timestamp, px, py);
			break;
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
				float dist0 = hypotf(g_f[1].x0 - g_f[0].x0, g_f[1].y0 - g_f[0].y0);
				float dist = hypotf(g_f[1].x - g_f[0].x, g_f[1].y - g_f[0].y);
				float spread = dist - dist0;
				if (d0x * d0x + d0y * d0y >= lim || d1x * d1x + d1y * d1y >= lim || dm >= lim || (g_two_cam && spread * spread >= lim))
				{
					g_two_moved = true;
					g_two_last_mx = sx;   // include the distance travelled before crossing the slop
					g_two_last_my = sy;
					g_two_last_dist = dist0;
				}
			}
			if (g_two_moved && g_two_cam)
			{
				float dist = hypotf(g_f[1].x - g_f[0].x, g_f[1].y - g_f[0].y);
				float ratio = (g_two_last_dist > 1.0f && dist > 1.0f) ? dist / g_two_last_dist : 1.0f;
				sk_camera_transform(g_two_last_mx, g_two_last_my, mx, my, ratio);
				g_two_last_mx = mx;
				g_two_last_my = my;
				g_two_last_dist = dist;
			}
			else if (g_two_moved)
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
	case M_PENDING:   // tap: smart tap = right click if the game would order the selection there, else left click
	{
		int lx, ly;
		to_game(g, f->x0, f->y0, &lx, &ly);
		click_at(g, f->x0, f->y0, sk_game_tap_is_command(lx, ly) != 0);
		g_mode = M_IDLE;
		g_game_fingers = 0;
		break;
	}
	case M_DRAG:
	{
		int lx, ly;
		if (g_drag_cam)
			to_game_in_map(g, px, py, &lx, &ly);
		else
			to_logical(g, px, py, &lx, &ly);
		move_cursor(lx, ly);
		mouse.add_event(LEFT_BUTTON_RELEASE);
		mouse.reset_boundary();
		g_drag_cam = false;
		g_mode = M_IDLE;
		g_game_fingers = 0;
		break;
	}
	case M_PAN:
	{
		float vx, vy;
		if (sample_velocity(e->tfinger.timestamp, &vx, &vy) && sqrtf(vx * vx + vy * vy) >= g.out_h * FLING_MIN_SPEED_FRAC)
			sk_camera_fling(vx, vy);
		g_mode = M_IDLE;
		g_game_fingers = 0;
		g_pan_start_map = false;
		break;
	}
	case M_LONG:   // held, not moved: the opposite of what a tap would do at the press point
	{
		int lx, ly;
		to_game(g, f->x0, f->y0, &lx, &ly);
		bool tap_would_command = sk_game_tap_is_command(lx, ly) != 0;
		click_at(g, f->x0, f->y0, !tap_would_command, true);
		g_mode = M_IDLE;
		g_game_fingers = 0;
		break;
	}
	case M_TWO:
		if (!g_two_moved && (Sint32)(now - g_t0) < (Sint32)TWO_TAP_MAX_MS)
			click_at(g, (g_f[0].x0 + g_f[1].x0) / 2, (g_f[0].y0 + g_f[1].y0) / 2, true);
		g_scroll_acc_x = g_scroll_acc_y = 0;
		g_two_cam = false;
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
static const Uint8 FONT_C[7] = { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
static const Uint8 FONT_DIGIT[6][7] = {
	{ 0, 0, 0, 0, 0, 0, 0 },
	{ 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },   // 1
	{ 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },   // 2
	{ 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },   // 3
	{ 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },   // 4
	{ 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },   // 5
};

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

// Thick line as a stack of parallel 1 px lines (output px).
static void thick_line(SDL_Renderer *r, int x1, int y1, int x2, int y2, int th)
{
	for (int i = -th / 2; i <= th / 2; i++)
	{
		SDL_RenderDrawLine(r, x1 + i, y1, x2 + i, y2);
		SDL_RenderDrawLine(r, x1, y1 + i, x2, y2 + i);
	}
}

static void draw_glyph(SDL_Renderer *r, const Rect &b, const Item &it)
{
	Glyph gl = it.glyph;
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
	case G_C: draw_letter(r, b, FONT_C); break;
	case G_GRP: draw_letter(r, b, FONT_DIGIT[it.arg >= 1 && it.arg <= 5 ? it.arg : 0]); break;
	case G_DESEL:   // X
	{
		int th = b.w / 14 + 1;
		thick_line(r, b.x + m, b.y + m, b.x + b.w - m, b.y + b.h - m, th);
		thick_line(r, b.x + b.w - m, b.y + m, b.x + m, b.y + b.h - m, th);
		break;
	}
	case G_ZOOM:    // magnifier: ring + handle
	{
		float rad = b.w * 0.23f;
		float cx = b.x + b.w * 0.44f, cy = b.y + b.h * 0.44f;
		draw_ring(r, cx, cy, rad, b.w / 12.0f + 1.0f);
		thick_line(r, (int)(cx + rad * 0.75f), (int)(cy + rad * 0.75f), b.x + b.w - m + b.w / 16, b.y + b.h - m + b.h / 16, b.w / 12 + 1);
		break;
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
	Uint32 now = SDL_GetTicks();
	for (int i = 0; i < n; i++)
	{
		bool flash = g_item_flash == i && (Sint32)(now - g_item_flash_at) < (Sint32)ITEM_FLASH_MS;
		bool down = (g_mode == M_BUTTON && g_btn_finger_hit == i) || (ITEMS[i].glyph == G_C && sk_cheats_is_open()) || flash;
		set_col(r, down ? COL_BTN_DOWN : COL_BTN);
		fill(r, items[i].x, items[i].y, items[i].w, items[i].h);
		set_col(r, COL_EDGE);
		frame(r, items[i], edge);
		set_col(r, COL_GLYPH);
		draw_glyph(r, items[i], ITEMS[i]);
		if (g_mode == M_BUTTON && g_btn_finger_hit == i && ITEMS[i].glyph == G_GRP && !g_btn_done)
		{
			// hold-to-assign progress bar along the bottom edge
			float p = (float)(now - g_btn_t0) / (float)GROUP_HOLD_MS;
			if (p > 1) p = 1;
			set_col(r, COL_EDGE);
			fill(r, items[i].x + edge, items[i].y + items[i].h - 3 * edge, (int)((items[i].w - 2 * edge) * p), 2 * edge);
		}
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
	if (g_mode == M_LONG)
	{
		// armed: steady full ring (lift = click, move = box-select)
		SDL_SetRenderDrawColor(r, 255, 255, 190, 230);
		draw_ring(r, g_f[0].x0, g_f[0].y0, rmax, th);
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
		SDL_Log("touch: window=%dx%d output=%dx%d logical=%dx%d%s zoom_width=%d letterbox rect=(%.1f,%.1f %.1fx%.1f) scale=%.4f",
			ww, wh, g.out_w, g.out_h, g.lw, g.lh, g.wide ? " (wide)" : "", g.zoom_w, g.gx, g.gy, g.gw, g.gh, g.scale);
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
	SkCheatsView cv = cheats_view(g);
	sk_cheats_draw(r, &cv);
	draw_feedback(r, g);

	SDL_SetRenderDrawBlendMode(r, obm);
	SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
	SDL_RenderSetLogicalSize(r, lw, lh);
}

#endif // __ANDROID__
