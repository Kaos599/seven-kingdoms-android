/*
 * Seven Kingdoms Android port: in-game cheats panel (see sk_cheats.h, docs/CHEATS.md).
 *
 * Injection point: SDL_KEYDOWN/SDL_KEYUP pushed into the SDL queue, so the game's own
 * Vga::handle_messages -> Mouse::add_key_event -> Sys::process_key -> Sys::detect_cheat_key path
 * runs unchanged. The game reads Shift from SDL_GetModState() (Mouse::update_skey_state), and a
 * pushed event does not update SDL's modifier state, so we set it by hand around each injection.
 *
 * Master toggle: ON injects Shift+1 x3, Shift+2 x3, Shift+3 x3 (Sys::process_key / cheat_str), so the
 * game itself sets nation->cheat_enabled_flag. The game has no key to turn cheat mode off again, so
 * OFF only locks this panel's cheat buttons (the game flag stays set for the rest of that game).
 */
#ifdef __ANDROID__

#include <string.h>
#include <SDL.h>
#include "sk_cheats.h"
#include "sk_text.h"

// ---------------------------------------------------------------------------------------------
// Tunables (sizes are fractions of the screen height unless noted)
// ---------------------------------------------------------------------------------------------
static const float  PANEL_MIN_MARGIN_FRAC = 0.36f;  // left letterbox margin at least this wide => panel lives in it
static const float  PANEL_OVERLAY_W_FRAC  = 0.85f;  // overlay panel width (fraction of height), ...
static const float  PANEL_OVERLAY_MAX_W   = 0.75f;  // ... capped to this fraction of the screen width
static const float  PANEL_PAD_FRAC        = 0.015f; // panel inner padding
static const float  PANEL_GAP_FRAC        = 0.012f; // gap between buttons
static const float  STATUS_ROW_RATIO      = 0.45f;  // status line height relative to a button row
static const int    LABEL_MAX_CHARS       = 10;     // longest label line, for the shared text scale

static const Uint32 KEY_GAP_MS            = 70;     // spacing between injected key presses
static const Uint32 FLASH_MS              = 450;    // visual confirmation after a one-shot cheat

static const int    CHEAT_COUNT           = 12;
static const int    COLS                  = 2;

static const Uint8 COL_PANEL[4]      = { 10, 12, 22, 225 };
static const Uint8 COL_PANEL_EDGE[4] = { 235, 225, 170, 200 };
static const Uint8 COL_BTN[4]        = { 34, 40, 66, 240 };
static const Uint8 COL_BTN_ON[4]     = { 40, 120, 62, 245 };
static const Uint8 COL_BTN_OFF[4]    = { 30, 30, 34, 200 };
static const Uint8 COL_BTN_DOWN[4]   = { 210, 175, 70, 240 };
static const Uint8 COL_BTN_FLASH[4]  = { 240, 200, 80, 245 };
static const Uint8 COL_EDGE[4]       = { 235, 225, 170, 210 };
static const Uint8 COL_EDGE_OFF[4]   = { 90, 90, 96, 160 };
static const Uint8 COL_TEXT[4]       = { 245, 240, 215, 255 };
static const Uint8 COL_TEXT_OFF[4]   = { 125, 125, 130, 230 };
static const Uint8 COL_TEXT_DARK[4]  = { 20, 20, 30, 255 };

// ---------------------------------------------------------------------------------------------
// Cheat table: one entry per key in Sys::detect_cheat_key (OSYS.cpp:1860-1996)
// ---------------------------------------------------------------------------------------------
enum Kind { K_ONESHOT, K_UNDIE, K_FAST, K_UNIT };   // K_UNIT: applied by sk_cheats_unit_cheat to all selected units

struct CheatDef
{
	const char *line1;
	const char *line2;     // for toggles this is the stem; " ON"/" OFF" is appended
	SDL_Keycode key;
	Kind kind;
	int unit_which;        // K_UNIT only: argument of sk_cheats_unit_cheat
};

static const CheatDef CHEATS[CHEAT_COUNT] = {
	{ "CASH",     "+1000",      SDLK_c,            K_ONESHOT, 0 },  // 'c'  add_cheat(1000)
	{ "FOOD",     "+1000",      SDLK_BACKSLASH,    K_ONESHOT, 0 },  // '\'  add_food(1000)
	{ "ALL TECH", "AND GODS",   SDLK_n,            K_ONESHOT, 0 },  // 'n'
	{ "REVEAL",   "MAP",        SDLK_SLASH,        K_ONESHOT, 0 },  // '/'
	{ "TOWN",     "POP +10",    SDLK_SEMICOLON,    K_ONESHOT, 0 },  // ';'  needs a selected town
	{ "IMMORTAL", "KING",       SDLK_u,            K_UNDIE, 0 },    // 'u'  toggle
	{ "PRAYER",   "MAX",        SDLK_EQUALS,       K_ONESHOT, 0 },  // '='  needs a selected Seat of Power base
	{ "FIRM",     "FULL HP",    SDLK_MINUS,        K_ONESHOT, 0 },  // '-'  needs a selected firm
	{ "FAST",     "BUILD",      SDLK_z,            K_FAST, 0 },     // 'z'  toggle
	{ "COMBAT",   "+20",        SDLK_LEFTBRACKET,  K_UNIT, 0 },     // '['  every selected own unit (OSYS.cpp detect_cheat_key)
	{ "SKILL",    "+20",        SDLK_RIGHTBRACKET, K_UNIT, 1 },     // ']'  every selected own unit
	{ "SPY",      "SKILL +20",  SDLK_QUOTE,        K_UNIT, 2 },     // '\'' every selected own spy
};

// ---------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------
struct QKey { SDL_Keycode sym; bool shift; };
enum { QCAP = 16 };

static bool   g_open = false;
static bool   g_armed = false;      // panel's cheat buttons unlocked (the "CHEATS: ON" state)
static bool   g_enabling = false;   // enable sequence sent / being sent, waiting for the game flag
static SkCheatsGameState g_st = { 0, 0, 0, 0, 0 };

static QKey   g_q[QCAP];
static int    g_qn = 0;             // queued keys (front = g_q[0])
static Uint32 g_last_inject = 0;
static bool   g_mod_dirty = false;  // SDL modifier state was changed by us and needs restoring

static int    g_press_idx = -1;     // -1 none, -2 dead area, -100 cancelled, 0 master, 1.. cheats
static SDL_FingerID g_press_id = 0;
static bool   g_pressing = false;

static int    g_flash_idx = -1;
static int    g_flash_count = -1;   // K_UNIT: units changed by the last press
static Uint32 g_flash_at = 0;

// ---------------------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------------------
struct Rect { int x, y, w, h; };

struct Layout
{
	Rect panel, master, status;
	Rect btn[CHEAT_COUNT];
	int  text_scale;        // shared scale for cheat button labels
	bool in_margin;
};

static void compute_layout(const SkCheatsView &v, Layout *L)
{
	int margin = (int)v.game_x;
	L->in_margin = !v.wide && margin >= (int)(v.out_h * PANEL_MIN_MARGIN_FRAC);
	if (v.wide)   // left part of the map view, below the top bar, clear of the right block (set by sk_touch)
		L->panel = Rect{ (int)v.wide_x, (int)v.wide_y, (int)v.wide_w, (int)v.wide_h };
	else if (L->in_margin)
		L->panel = Rect{ 0, 0, margin, v.out_h };
	else
	{
		int w = (int)(v.out_h * PANEL_OVERLAY_W_FRAC);
		int cap = (int)(v.out_w * PANEL_OVERLAY_MAX_W);
		if (w > cap) w = cap;
		L->panel = Rect{ 0, v.strip_h, w, v.out_h - v.strip_h };
	}

	int pad = (int)(v.out_h * PANEL_PAD_FRAC) + 1;
	int gap = (int)(v.out_h * PANEL_GAP_FRAC) + 1;
	int ix = L->panel.x + pad, iy = L->panel.y + pad;
	int iw = L->panel.w - 2 * pad, ih = L->panel.h - 2 * pad;
	int rows = CHEAT_COUNT / COLS;
	int items = rows + 2;   // master + status + rows
	float units = (float)rows + 1.0f + STATUS_ROW_RATIO;
	int rowh = (int)((ih - (items - 1) * gap) / units);
	int statush = (int)(rowh * STATUS_ROW_RATIO);
	int colw = (iw - (COLS - 1) * gap) / COLS;

	L->master = Rect{ ix, iy, iw, rowh };
	L->status = Rect{ ix, iy + rowh + gap, iw, statush };
	int y0 = L->status.y + statush + gap;
	for (int i = 0; i < CHEAT_COUNT; i++)
	{
		int c = i % COLS, r = i / COLS;
		L->btn[i] = Rect{ ix + c * (colw + gap), y0 + r * (rowh + gap), colw, rowh };
	}

	// One text scale for all cheat labels: widest label must fit the width, two lines the height.
	int by_w = (colw - 2 * pad) / (6 * LABEL_MAX_CHARS - 1);
	int by_h = (int)(rowh * 0.78f) / 16;
	int s = by_w < by_h ? by_w : by_h;
	L->text_scale = s < 1 ? 1 : s;
}

static bool in_rect(const Rect &r, float x, float y)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// Which control is at (x,y)? -1 outside the panel, -2 panel but no control, 0 master, 1.. cheat i-1.
static int panel_hit(const Layout &L, float x, float y)
{
	if (!in_rect(L.panel, x, y))
		return -1;
	if (in_rect(L.master, x, y))
		return 0;
	for (int i = 0; i < CHEAT_COUNT; i++)
		if (in_rect(L.btn[i], x, y))
			return i + 1;
	return -2;
}

// ---------------------------------------------------------------------------------------------
// Key injection
// ---------------------------------------------------------------------------------------------
static void send_key(SDL_Window *window, SDL_Keycode sym, Uint16 mod)
{
	SDL_Event e;
	SDL_zero(e);
	e.type = SDL_KEYDOWN;
	e.key.state = SDL_PRESSED;
	e.key.windowID = window ? SDL_GetWindowID(window) : 0;
	e.key.keysym.sym = sym;
	e.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
	e.key.keysym.mod = mod;
	SDL_PushEvent(&e);
	e.type = SDL_KEYUP;
	e.key.state = SDL_RELEASED;
	SDL_PushEvent(&e);
}

static void enqueue(SDL_Keycode sym, bool shift)
{
	if (g_qn < QCAP)
		g_q[g_qn++] = QKey{ sym, shift };
}

static void refresh_state(void)
{
	sk_cheats_get_game_state(&g_st);
}

static bool buttons_enabled(void)
{
	return g_st.can_cheat && g_armed && g_st.cheat_on && g_qn == 0;
}

// ---------------------------------------------------------------------------------------------
// Public API: open/close, update
// ---------------------------------------------------------------------------------------------
void sk_cheats_toggle(void) { g_open = !g_open; g_pressing = false; }
void sk_cheats_close(void)  { g_open = false; g_pressing = false; }
int  sk_cheats_is_open(void) { return g_open ? 1 : 0; }

void sk_cheats_reset_touch(void) { g_pressing = false; }

void sk_cheats_update(SDL_Window *window)
{
	sk_text_update();   // per-frame hook for the text-field keyboard (sk_text.cpp); sk_touch.cpp is not ours to edit
	refresh_state();
	Uint32 now = SDL_GetTicks();

	if (!g_st.in_game)
	{
		// Back in the menus: forget everything, never inject outside a game.
		g_armed = false;
		g_enabling = false;
		g_qn = 0;
	}
	else if (g_armed && !g_st.cheat_on && g_st.can_cheat)
		g_armed = false;   // a new/loaded game without cheat mode

	if (g_enabling && g_qn == 0 && g_st.cheat_on)
	{
		g_enabling = false;
		g_armed = true;
	}

	if (g_qn > 0 && g_st.can_cheat && (Sint32)(now - g_last_inject) >= (Sint32)KEY_GAP_MS)
	{
		QKey k = g_q[0];
		memmove(&g_q[0], &g_q[1], sizeof(QKey) * (g_qn - 1));
		g_qn--;
		Uint16 mod = k.shift ? (Uint16)KMOD_LSHIFT : (Uint16)KMOD_NONE;
		SDL_SetModState((SDL_Keymod)mod);
		send_key(window, k.sym, mod);
		g_mod_dirty = true;
		g_last_inject = now;
	}
	else if (g_qn == 0 && g_mod_dirty && (Sint32)(now - g_last_inject) >= (Sint32)KEY_GAP_MS)
	{
		// Restore the modifier state and let the game's Mouse refresh its copy (SDL_KEYUP does that).
		SDL_SetModState(KMOD_NONE);
		SDL_Event e;
		SDL_zero(e);
		e.type = SDL_KEYUP;
		e.key.state = SDL_RELEASED;
		e.key.windowID = window ? SDL_GetWindowID(window) : 0;
		e.key.keysym.sym = SDLK_LSHIFT;
		e.key.keysym.scancode = SDL_SCANCODE_LSHIFT;
		e.key.keysym.mod = KMOD_NONE;
		SDL_PushEvent(&e);
		g_mod_dirty = false;
	}
}

// ---------------------------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------------------------
static void activate(int idx)
{
	if (idx == 0)   // master toggle
	{
		if (!g_st.can_cheat)
			return;
		if (g_enabling)
		{
			g_enabling = false;   // cancel a stuck enable attempt
			g_qn = 0;
		}
		else if (g_armed)
			g_armed = false;      // OFF: lock the buttons (the game cannot clear its own flag)
		else if (g_st.cheat_on)
			g_armed = true;       // the game already has cheat mode on (e.g. enabled earlier this game)
		else
		{
			g_enabling = true;    // ON: the game's own key sequence !!!@@@###
			static const SDL_Keycode SEQ[3] = { SDLK_1, SDLK_2, SDLK_3 };
			for (int k = 0; k < 3; k++)          // Shift+1 x3, Shift+2 x3, Shift+3 x3
				for (int n = 0; n < 3; n++)
					enqueue(SEQ[k], true);
		}
		return;
	}
	int i = idx - 1;
	if (!buttons_enabled())
		return;
	if (CHEATS[i].kind == K_UNIT)
	{
		g_flash_count = sk_cheats_unit_cheat(CHEATS[i].unit_which);
		g_flash_idx = idx;
		g_flash_at = SDL_GetTicks();
		return;
	}
	enqueue(CHEATS[i].key, false);
	if (CHEATS[i].kind == K_ONESHOT)
	{
		g_flash_idx = idx;
		g_flash_at = SDL_GetTicks();
	}
}

// ---------------------------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------------------------
int sk_cheats_touch(const SkCheatsView *v, int type, SDL_FingerID id, float px, float py)
{
	if (!g_open)
		return 0;
	Layout L;
	compute_layout(*v, &L);
	int hit = panel_hit(L, px, py);

	if (type == 0)
	{
		if (hit == -1)
			return 0;       // outside the panel: the rest of the touch layer decides
		if (!g_pressing)
		{
			g_pressing = true;
			g_press_id = id;
			g_press_idx = hit;
		}
		return 1;           // never reaches the game
	}
	if (!g_pressing || id != g_press_id)
		return 0;
	if (type == 1)
	{
		if (hit != g_press_idx)
			g_press_idx = -100;
		return 1;
	}
	// finger up
	g_pressing = false;
	if (g_press_idx >= 0 && hit == g_press_idx)
	{
		refresh_state();
		activate(g_press_idx);
	}
	return 1;
}

// ---------------------------------------------------------------------------------------------
// Drawing: 5x7 uppercase font
// ---------------------------------------------------------------------------------------------
static const Uint8 FONT_BLANK[7] = { 0, 0, 0, 0, 0, 0, 0 };
static const Uint8 FONT_LET[26][7] = {
	{ 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, // A
	{ 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, // B
	{ 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, // C
	{ 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E }, // D
	{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, // E
	{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 }, // F
	{ 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F }, // G
	{ 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, // H
	{ 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, // I
	{ 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C }, // J
	{ 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, // K
	{ 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }, // L
	{ 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, // M
	{ 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 }, // N
	{ 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, // O
	{ 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }, // P
	{ 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, // Q
	{ 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }, // R
	{ 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, // S
	{ 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, // T
	{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, // U
	{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }, // V
	{ 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A }, // W
	{ 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }, // X
	{ 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }, // Y
	{ 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F }, // Z
};
static const Uint8 FONT_DIG[10][7] = {
	{ 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, // 0
	{ 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, // 1
	{ 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, // 2
	{ 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E }, // 3
	{ 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, // 4
	{ 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E }, // 5
	{ 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, // 6
	{ 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, // 7
	{ 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, // 8
	{ 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }, // 9
};
static const Uint8 FONT_PLUS[7]   = { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 };
static const Uint8 FONT_MINUS[7]  = { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
static const Uint8 FONT_COLON[7]  = { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 };
static const Uint8 FONT_DOT[7]    = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C };
static const Uint8 FONT_SLASH[7]  = { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 };
static const Uint8 FONT_BANG[7]   = { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 };
static const Uint8 FONT_QUEST[7]  = { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 };

static const Uint8 *glyph_rows(char c)
{
	if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
	if (c >= 'A' && c <= 'Z') return FONT_LET[c - 'A'];
	if (c >= '0' && c <= '9') return FONT_DIG[c - '0'];
	switch (c)
	{
	case '+': return FONT_PLUS;
	case '-': return FONT_MINUS;
	case ':': return FONT_COLON;
	case '.': return FONT_DOT;
	case '/': return FONT_SLASH;
	case '!': return FONT_BANG;
	case '?': return FONT_QUEST;
	default:  return FONT_BLANK;
	}
}

static int text_width(const char *s, int sc)
{
	int n = (int)strlen(s);
	return n > 0 ? n * 6 * sc - sc : 0;
}

static void set_col(SDL_Renderer *r, const Uint8 *c)
{
	SDL_SetRenderDrawColor(r, c[0], c[1], c[2], c[3]);
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

static void draw_text(SDL_Renderer *r, int x, int y, int sc, const char *s)
{
	for (; *s; s++, x += 6 * sc)
	{
		const Uint8 *rows = glyph_rows(*s);
		for (int j = 0; j < 7; j++)
			for (int i = 0; i < 5; i++)
				if (rows[j] & (0x10 >> i))
					fill(r, x + i * sc, y + j * sc, sc, sc);
	}
}

static void draw_text_centered(SDL_Renderer *r, const Rect &b, int y, int sc, const char *s)
{
	draw_text(r, b.x + (b.w - text_width(s, sc)) / 2, y, sc, s);
}

// One or two centred lines in a button.
static void draw_label(SDL_Renderer *r, const Rect &b, int sc, const char *l1, const char *l2)
{
	if (!l2 || !*l2)
	{
		draw_text_centered(r, b, b.y + (b.h - 7 * sc) / 2, sc, l1);
		return;
	}
	int total = 16 * sc;   // 7 + 2 gap + 7
	int y = b.y + (b.h - total) / 2;
	draw_text_centered(r, b, y, sc, l1);
	draw_text_centered(r, b, y + 9 * sc, sc, l2);
}

static int fit_scale(int maxw, int maxh, const char *s)
{
	int n = (int)strlen(s);
	int by_w = n > 0 ? maxw / (6 * n - 1) : maxh / 7;
	int by_h = maxh / 7;
	int sc = by_w < by_h ? by_w : by_h;
	return sc < 1 ? 1 : sc;
}

static const char *status_text(void)
{
	if (!g_st.in_game)   return "START A GAME FIRST";
	if (g_enabling)      return g_qn ? "ENABLING..." : "TAP THE GAME MESSAGE";
	if (!g_st.can_cheat) return "CLOSE THE GAME MENU";
	if (g_armed)         return "READY";
	if (g_st.cheat_on)   return "OFF: BUTTONS LOCKED";
	return "TAP TO ENABLE CHEATS";
}

void sk_cheats_draw(SDL_Renderer *r, const SkCheatsView *v)
{
	if (!g_open)
		return;
	refresh_state();
	Layout L;
	compute_layout(*v, &L);
	Uint32 now = SDL_GetTicks();
	int edge = v->out_h / 300 + 1;

	// Panel background.
	set_col(r, COL_PANEL);
	fill(r, L.panel.x, L.panel.y, L.panel.w, L.panel.h);
	set_col(r, COL_PANEL_EDGE);
	frame(r, L.panel, edge);

	// Master toggle.
	{
		bool usable = g_st.can_cheat;
		bool down = g_pressing && g_press_idx == 0;
		const Uint8 *bg = down ? COL_BTN_DOWN : (!usable ? COL_BTN_OFF : (g_armed ? COL_BTN_ON : COL_BTN));
		set_col(r, bg);
		fill(r, L.master.x, L.master.y, L.master.w, L.master.h);
		set_col(r, usable ? COL_EDGE : COL_EDGE_OFF);
		frame(r, L.master, edge);
		const char *t = g_enabling ? "CHEATS: ..." : (g_armed ? "CHEATS: ON" : "CHEATS: OFF");
		int sc = fit_scale(L.master.w - 4 * edge - 16, (int)(L.master.h * 0.5f), t);
		set_col(r, down ? COL_TEXT_DARK : (usable ? COL_TEXT : COL_TEXT_OFF));
		draw_label(r, L.master, sc, t, NULL);
	}

	// Status line.
	{
		const char *t = status_text();
		int sc = fit_scale(L.status.w - 8, (int)(L.status.h * 0.7f), t);
		set_col(r, COL_TEXT);
		draw_text_centered(r, L.status, L.status.y + (L.status.h - 7 * sc) / 2, sc, t);
	}

	// Cheat buttons.
	bool enabled = buttons_enabled();
	for (int i = 0; i < CHEAT_COUNT; i++)
	{
		const CheatDef &c = CHEATS[i];
		int idx = i + 1;
		bool down = enabled && g_pressing && g_press_idx == idx;
		bool flash = g_flash_idx == idx && (Sint32)(now - g_flash_at) < (Sint32)FLASH_MS;
		bool on = (c.kind == K_UNDIE && g_st.king_undie) || (c.kind == K_FAST && g_st.fast_build);

		const Uint8 *bg = !enabled ? COL_BTN_OFF : (down ? COL_BTN_DOWN : (flash ? COL_BTN_FLASH : (on ? COL_BTN_ON : COL_BTN)));
		set_col(r, bg);
		fill(r, L.btn[i].x, L.btn[i].y, L.btn[i].w, L.btn[i].h);
		set_col(r, enabled ? COL_EDGE : COL_EDGE_OFF);
		frame(r, L.btn[i], edge);

		char l2[24];
		if (c.kind == K_UNIT && flash)
		{
			if (g_flash_count > 0)
				SDL_snprintf(l2, sizeof(l2), "%d %s", g_flash_count, g_flash_count == 1 ? "UNIT" : "UNITS");
			else
				SDL_snprintf(l2, sizeof(l2), "NO UNIT");
		}
		else if (c.kind == K_ONESHOT || c.kind == K_UNIT)
			SDL_snprintf(l2, sizeof(l2), "%s", flash ? "SENT" : c.line2);
		else
			SDL_snprintf(l2, sizeof(l2), "%s %s", c.line2, on ? "ON" : "OFF");

		set_col(r, !enabled ? COL_TEXT_OFF : ((down || flash) ? COL_TEXT_DARK : COL_TEXT));
		draw_label(r, L.btn[i], L.text_scale, c.line1, l2);
	}
}

#endif // __ANDROID__
