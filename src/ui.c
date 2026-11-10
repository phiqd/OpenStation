/* OpenStation C245/C210 user interface: landscape 160x128 ST7735.
 *
 * Screens: main (stats, big temperature, presets, live chart), sleep,
 * standby (heater off after sleep), settings, calibration wizard, about,
 * fault popup and boot handle check. All drawing goes through one RAM tile
 * that is sent to the panel in a single address window per field. */
#include "ui.h"
#include "app_config.h"
#include "board.h"
#include "control.h"
#include "encoder.h"
#include "fonts_aa.h"
#include "logo.h"
#include "measure.h"
#include "power.h"
#include "profiles.h"
#include "safety.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 160U
#define H 128U
#define TILE_H 32U
#define DEG "\xb0"

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8U) << 8) | (((g) & 0xFCU) << 3) | ((b) >> 3)))
#define C_BG        RGB(0, 0, 0)
#define C_PANEL     RGB(24, 26, 30)
#define C_PANEL_HI  RGB(36, 58, 92)
#define C_GRID      RGB(40, 42, 48)
#define C_TEXT      RGB(240, 240, 240)
#define C_MUTED     RGB(130, 132, 140)
#define C_DIM       RGB(80, 82, 88)
#define C_TEMP      RGB(255, 138, 31)
#define C_VOLT      RGB(80, 220, 110)
#define C_AMP       RGB(255, 210, 60)
#define C_WATT      RGB(240, 90, 220)
#define C_LINE_T    RGB(70, 160, 255)
#define C_LINE_P    RGB(70, 210, 90)
#define C_LINE_S    RGB(150, 150, 160)
#define C_RED       RGB(250, 70, 60)
#define C_GREEN     RGB(60, 200, 90)
#define C_GREEN_BG  RGB(20, 70, 34)
#define C_BLUE      RGB(60, 140, 255)
#define C_CYAN      RGB(80, 210, 230)
#define C_YELLOW    RGB(255, 220, 40)

typedef enum {
    SCR_NONE = 0, SCR_MAIN, SCR_SLEEP, SCR_SETTINGS,
    SCR_CAL_INTRO, SCR_CAL, SCR_ABOUT, SCR_FAULT, SCR_SPLASH, SCR_LOGO
} screen_t;

/* ------------------------------------------------------------------ */
/* Tile renderer                                                       */
/* ------------------------------------------------------------------ */
static ST7735_Object_t *s_lcd;
static uint8_t s_tile[W * TILE_H * 2U];
static uint16_t s_tw, s_th;

static void tile_begin(uint16_t w, uint16_t h, uint16_t bg)
{
    if (w > W) w = W;
    if (h > TILE_H) h = TILE_H;
    s_tw = w; s_th = h;
    uint8_t hi = (uint8_t)(bg >> 8), lo = (uint8_t)bg;
    for (uint32_t i = 0; i < (uint32_t)w * h; ++i) { s_tile[i * 2U] = hi; s_tile[i * 2U + 1U] = lo; }
}

static inline void tile_px(int16_t x, int16_t y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= (int16_t)s_tw || y >= (int16_t)s_th) return;
    uint32_t i = ((uint32_t)y * s_tw + (uint32_t)x) * 2U;
    s_tile[i] = (uint8_t)(c >> 8);
    s_tile[i + 1U] = (uint8_t)c;
}

static void tile_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c)
{
    for (int16_t j = y; j < y + h; ++j)
        for (int16_t i = x; i < x + w; ++i) tile_px(i, j, c);
}

static void tile_flush(uint16_t x, uint16_t y)
{
    if (s_lcd == NULL || x + s_tw > W || y + s_th > H) return;
    ST7735_FillRGBRect(s_lcd, x, y, s_tile, s_tw, s_th);
}

static uint16_t blend(uint16_t fg, uint16_t bg, uint8_t level, uint8_t max)
{
    if (level == 0U) return bg;
    if (level >= max) return fg;
    uint8_t inv = (uint8_t)(max - level);
    uint16_t r = (uint16_t)((((fg >> 11) & 31U) * level + ((bg >> 11) & 31U) * inv + max / 2U) / max);
    uint16_t g = (uint16_t)((((fg >> 5) & 63U) * level + ((bg >> 5) & 63U) * inv + max / 2U) / max);
    uint16_t b = (uint16_t)(((fg & 31U) * level + (bg & 31U) * inv + max / 2U) / max);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static int16_t glyph_index(const aa_font_t *f, char ch)
{
    for (uint8_t i = 0; i < f->count; ++i) if (f->chars[i] == ch) return i;
    return -1;
}

static uint16_t aa_width(const aa_font_t *f, const char *s)
{
    uint16_t w = 0;
    for (; *s; ++s) { int16_t g = glyph_index(f, *s); if (g >= 0) w += f->widths[g]; }
    return w;
}

static uint16_t aa_draw(const aa_font_t *f, int16_t x, int16_t y, const char *s,
                        uint16_t fg, uint16_t bg)
{
    enum { MAXLV = (1 << AA_FONT_BPP) - 1, PER_BYTE = 8 / AA_FONT_BPP };
    uint16_t lut[MAXLV + 1];
    for (uint8_t i = 0; i <= MAXLV; ++i) lut[i] = blend(fg, bg, i, MAXLV);
    int16_t start = x;
    for (; *s; ++s) {
        int16_t g = glyph_index(f, *s);
        if (g < 0) continue;
        uint8_t gw = f->widths[g];
        const uint8_t *d = &f->data[f->offsets[g]];
        uint32_t n = 0;
        for (uint8_t row = 0; row < f->height; ++row) {
            for (uint8_t col = 0; col < gw; ++col, ++n) {
                uint8_t lv = (uint8_t)((d[n / PER_BYTE] >> ((n % PER_BYTE) * AA_FONT_BPP)) & MAXLV);
                if (lv) tile_px((int16_t)(x + col), (int16_t)(y + row), lut[lv]);
            }
        }
        x = (int16_t)(x + gw);
    }
    return (uint16_t)(x - start);
}

/* Small labels (DejaVu Condensed Bold 10 px). */
static void px_draw(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg)
{
    aa_draw(&font_small, x, y, s, fg, bg);
}

typedef enum { AL_LEFT, AL_CENTER, AL_RIGHT } align_t;
typedef enum { F_SMALL, F_MID, F_BIG } font_id_t;

static uint16_t text_width(font_id_t f, const char *s)
{
    return aa_width(f == F_SMALL ? &font_small : (f == F_MID ? &font_mid : &font_big), s);
}
static uint8_t text_height(font_id_t f)
{
    return f == F_SMALL ? font_small.height : (f == F_MID ? font_mid.height : font_big.height);
}

static void tile_text(int16_t x, int16_t y, int16_t w, align_t al, font_id_t f,
                      const char *s, uint16_t fg, uint16_t bg)
{
    int16_t tw = (int16_t)text_width(f, s);
    if (al == AL_CENTER) x = (int16_t)(x + (w - tw) / 2);
    else if (al == AL_RIGHT) x = (int16_t)(x + w - tw);
    aa_draw(f == F_SMALL ? &font_small : (f == F_MID ? &font_mid : &font_big), x, y, s, fg, bg);
}

/* One self-contained text box: background, text, flush. */
static void box(uint16_t x, uint16_t y, uint16_t w, uint16_t h, align_t al, font_id_t f,
                const char *s, uint16_t fg, uint16_t bg)
{
    tile_begin(w, h, bg);
    int16_t ty = (int16_t)(((int16_t)h - (int16_t)text_height(f)) / 2);
    tile_text(al == AL_LEFT ? 2 : (al == AL_RIGHT ? -2 : 0), ty, (int16_t)w, al, f, s, fg, bg);
    tile_flush(x, y);
}

static void fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    if (s_lcd != NULL) ST7735_FillRect(s_lcd, x, y, w, h, c);
}

/* Cached fields: a box is only redrawn when its text or colours change. */
#define FIELD_COUNT 24U
typedef struct { char text[24]; uint16_t fg, bg; } field_cache_t;
static field_cache_t s_cache[FIELD_COUNT];

static void cache_clear(void) { memset(s_cache, 0, sizeof(s_cache)); }

static void field(uint8_t id, uint16_t x, uint16_t y, uint16_t w, uint16_t h, align_t al,
                  font_id_t f, const char *s, uint16_t fg, uint16_t bg)
{
    if (id >= FIELD_COUNT) return;
    field_cache_t *c = &s_cache[id];
    if (c->fg == fg && c->bg == bg && c->text[0] != '\0' &&
        strncmp(c->text, s, sizeof(c->text) - 1U) == 0) return;
    strncpy(c->text, s[0] ? s : " ", sizeof(c->text) - 1U);
    c->text[sizeof(c->text) - 1U] = '\0';
    c->fg = fg; c->bg = bg;
    box(x, y, w, h, al, f, s, fg, bg);
}

/* True (and remembered) when a custom-drawn field's key changed. */
static bool field_changed(uint8_t id, const char *key, uint16_t fg, uint16_t bg)
{
    if (id >= FIELD_COUNT) return false;
    field_cache_t *c = &s_cache[id];
    if (c->text[0] != '\0' && c->fg == fg && c->bg == bg &&
        strncmp(c->text, key, sizeof(c->text) - 1U) == 0) return false;
    strncpy(c->text, key[0] ? key : " ", sizeof(c->text) - 1U);
    c->text[sizeof(c->text) - 1U] = '\0';
    c->fg = fg; c->bg = bg;
    return true;
}

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
static screen_t s_screen = SCR_NONE;
static bool s_flipped;
static bool s_settings_open;
static bool s_about_open;
static bool s_splash;                 /* start-up logo screen */
static uint32_t s_splash_until_ms;
static bool s_logo_mode;              /* heater was turned off: show the logo */
static uint32_t s_logo_since_ms;
static bool s_was_heating;
#define SPLASH_MS        3500UL
#define LOGO_WAKE_MS     10000UL      /* knob input shows the main screen this long */
#define LOGO_HOT_C       60
static safety_fault_t s_popup_fault;
static bool s_fault_popup;
static bool s_cal_intro;
static uint32_t s_last_input_ms;

/* Chart history: one point every 500 ms, ~79 s window. */
#define HIST_N 158U
#define HIST_MS 500U
#define CHART_Y 61U
#define CHART_H 67U
static int16_t s_hist_temp[HIST_N];
static int16_t s_hist_set[HIST_N];
static uint8_t s_hist_pwr[HIST_N];
static uint8_t s_hist_count, s_hist_head;
static uint32_t s_hist_last_ms;
static bool s_chart_dirty = true;

/* Settings menu */
enum {
    S_TIP, S_BOOST_TEMP, S_BOOST_TIME, S_SLEEP_DELAY, S_SLEEP_TEMP, S_SLEEP_OFF,
    S_MAX_TEMP, S_ROOM, S_SUPPLY, S_ENC_STEP, S_ENC_DIR,
    S_BRIGHT, S_SOUND, S_READY_BEEP, S_TIP_CHECK, S_FLIP, S_CALIBRATE,
    S_RESET, S_ABOUT, S_EXIT, S_COUNT
};
#define MENU_ROWS 8U
#define MENU_ROW_H 14U
#define MENU_Y 16U
static uint8_t s_sel, s_scroll;
static bool s_editing;
static uint8_t s_pending_tip;
static bool s_reset_armed;
static const char *s_menu_note;

/* Calibration wizard */
static const int16_t s_cal_anchor[PROFILE_CAL_POINT_COUNT] = {100, 200, 300, 350, 400, 450};
static bool s_cal_active;
static uint8_t s_cal_point;
static int16_t s_cal_values[PROFILE_CAL_POINT_COUNT];
static int16_t s_cal_prev_target;
static uint32_t s_cal_stable_since;
static const char *s_cal_msg;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static void rollers_invalidate(void);

static void set_screen(screen_t s)
{
    if (s == s_screen) return;
    s_screen = s;
    cache_clear();
    rollers_invalidate();
    s_chart_dirty = true;
    fill(0, 0, W, H, C_BG);
}

static void force_redraw(void) { s_screen = SCR_NONE; }

static void fmt_temp(char *b, size_t n, int16_t t) { snprintf(b, n, "%d" DEG "C", t); }

static void top_bar(const char *middle, uint16_t middle_fg, const char *state,
                    uint16_t state_bg, uint16_t state_fg)
{
    const tip_profile_t *p = profile_active();
    uint16_t chip = profile_active_index() == TIP_C245 ? C_TEMP : C_CYAN;
    field(0, 0, 0, 34, 14, AL_CENTER, F_MID, p->name, C_BG, chip);
    field(1, 34, 0, 80, 14, AL_CENTER, F_SMALL, middle, middle_fg, C_PANEL);
    field(2, 114, 0, 46, 14, AL_CENTER, F_MID, state, state_fg, state_bg);
}

static void state_style(const control_status_t *c, bool tip, const char **text,
                        uint16_t *bg, uint16_t *fg)
{
    *fg = C_BG;
    if (safety_latched()) { *text = "FAULT"; *bg = C_RED; *fg = C_TEXT; return; }
    if (!tip) { *text = "NO TIP"; *bg = C_RED; *fg = C_TEXT; return; }
    switch (c->state) {
    case STATE_HEAT_UP: *text = "HEAT"; *bg = C_TEMP; break;
    case STATE_REGULATE: *text = "READY"; *bg = C_GREEN; break;
    case STATE_BOOST: *text = "BOOST"; *bg = C_YELLOW; break;
    case STATE_SLEEP: *text = "SLEEP"; *bg = C_BLUE; *fg = C_TEXT; break;
    default: *text = "OFF"; *bg = C_DIM; *fg = C_TEXT; break;
    }
}

/* ------------------------------------------------------------------ */
/* Chart                                                               */
/* ------------------------------------------------------------------ */
static void history_push(void)
{
    uint32_t now = board_millis();
    if (s_hist_count != 0U && now - s_hist_last_ms < HIST_MS) return;
    s_hist_last_ms = now;
    const measurement_t *m = measure_get();
    const control_status_t *c = control_get();
    s_hist_temp[s_hist_head] = m->tip_present ? m->temperature_c : 0;
    s_hist_set[s_hist_head] = c->heating_enabled ? c->active_setpoint_c : 0;
    uint16_t pct = (uint16_t)((power_get()->limited_permille + 5U) / 10U);
    s_hist_pwr[s_hist_head] = (uint8_t)(pct > 100U ? 100U : pct);
    s_hist_head = (uint8_t)((s_hist_head + 1U) % HIST_N);
    if (s_hist_count < HIST_N) ++s_hist_count;
    s_chart_dirty = true;
}

static uint8_t hist_at(uint8_t i) /* i = 0 oldest */
{
    return (uint8_t)((s_hist_head + HIST_N - s_hist_count + i) % HIST_N);
}

static void draw_chart(void)
{
    static uint8_t yt[HIST_N], yp[HIST_N], ys[HIST_N];
    int16_t lo = 1000, hi = -1000;
    for (uint8_t i = 0; i < s_hist_count; ++i) {
        uint8_t k = hist_at(i);
        if (s_hist_temp[k] < lo) lo = s_hist_temp[k];
        if (s_hist_temp[k] > hi) hi = s_hist_temp[k];
        if (s_hist_set[k] != 0) {
            if (s_hist_set[k] < lo) lo = s_hist_set[k];
            if (s_hist_set[k] > hi) hi = s_hist_set[k];
        }
    }
    if (s_hist_count == 0U) { lo = 0; hi = 400; }
    if (hi - lo < 40) { int16_t mid = (int16_t)((hi + lo) / 2); lo = (int16_t)(mid - 20); hi = (int16_t)(mid + 20); }
    int16_t pad = (int16_t)((hi - lo) / 10 + 2);
    lo = (int16_t)(((lo - pad) / 10) * 10); hi = (int16_t)(((hi + pad + 9) / 10) * 10);
    if (lo < 0) lo = 0;
    if (hi <= lo) hi = (int16_t)(lo + 40);

    const int16_t plot_h = (int16_t)(CHART_H - 2);
    uint8_t x0 = (uint8_t)(W - 1U - s_hist_count);
    for (uint8_t i = 0; i < s_hist_count; ++i) {
        uint8_t k = hist_at(i);
        int32_t t = s_hist_temp[k];
        if (t < lo) t = lo;
        if (t > hi) t = hi;
        yt[i] = (uint8_t)(1 + plot_h - 1 - ((t - lo) * (plot_h - 1)) / (hi - lo));
        yp[i] = (uint8_t)(1 + plot_h - 1 - ((int32_t)s_hist_pwr[k] * (plot_h - 1)) / 100);
        int32_t s = s_hist_set[k];
        ys[i] = (s == 0 || s < lo || s > hi) ? 0xFFU :
                (uint8_t)(1 + plot_h - 1 - ((s - lo) * (plot_h - 1)) / (hi - lo));
    }

    char lbl_hi[8], lbl_lo[8];
    snprintf(lbl_hi, sizeof(lbl_hi), "%d", hi);
    snprintf(lbl_lo, sizeof(lbl_lo), "%d", lo);
    for (uint16_t strip = 0; strip < CHART_H; strip += 28U) {
        uint16_t sh = (uint16_t)(CHART_H - strip);
        if (sh > 28U) sh = 28U;
        tile_begin(W, sh, C_BG);
        int16_t oy = (int16_t)strip;
        /* frame and grid */
        for (int16_t x = 0; x < (int16_t)W; ++x) {
            tile_px(x, (int16_t)(0 - oy), C_GRID);
            tile_px(x, (int16_t)(CHART_H - 1 - oy), C_GRID);
            if ((x & 3) == 0)
                for (uint8_t g = 1; g < 4U; ++g)
                    tile_px(x, (int16_t)(1 + (plot_h * g) / 4 - oy), C_GRID);
        }
        /* power (green) then setpoint (dashed) then temperature (blue, 2 px) */
        for (uint8_t i = 0; i < s_hist_count; ++i) {
            int16_t x = (int16_t)(x0 + i);
            uint8_t a = yp[i], b = i ? yp[i - 1U] : yp[i];
            for (int16_t y = (a < b ? a : b); y <= (a < b ? b : a); ++y) tile_px(x, (int16_t)(y - oy), C_LINE_P);
        }
        for (uint8_t i = 0; i < s_hist_count; ++i)
            if (ys[i] != 0xFFU && ((x0 + i) & 3) < 2) tile_px((int16_t)(x0 + i), (int16_t)(ys[i] - oy), C_LINE_S);
        for (uint8_t i = 0; i < s_hist_count; ++i) {
            int16_t x = (int16_t)(x0 + i);
            uint8_t a = yt[i], b = i ? yt[i - 1U] : yt[i];
            for (int16_t y = (a < b ? a : b); y <= (a < b ? b : a); ++y) {
                tile_px(x, (int16_t)(y - oy), C_LINE_T);
                tile_px(x, (int16_t)(y + 1 - oy), C_LINE_T);
            }
        }
        px_draw(2, (int16_t)(2 - oy), lbl_hi, C_LINE_T, C_BG);
        px_draw(2, (int16_t)(CHART_H - 12 - oy), lbl_lo, C_LINE_T, C_BG);
        tile_flush(0, (uint16_t)(CHART_Y + strip));
        if (sh < 28U) break;
    }
    s_chart_dirty = false;
}

/* ------------------------------------------------------------------ */
/* Main, sleep and standby screens                                     */
/* ------------------------------------------------------------------ */
static void draw_main_static(void)
{
    static const char *const labels[3] = {"Volt", "Amp", "Watt"};
    for (uint8_t i = 0; i < 3U; ++i) {
        tile_begin(30, 11, C_BG);
        px_draw(1, 0, labels[i], C_MUTED, C_BG);
        tile_flush((uint16_t)(2U + i * 31U), 16);
    }
}

/* ---- Rolling digits -----------------------------------------------------
 * Odometer-style number: when the value changes, only the digits that differ
 * slide (up when the value rises, down when it falls), eased over ROLL_MS.
 * ui_animate() redraws the active rollers at ~50 frames per second. */
#define ROLL_MS        180U
#define ROLL_FAST_MS   100U
#define ROLL_FRAME_MS  20U
typedef struct {
    uint16_t x, y, w, h;
    font_id_t font;
    char cur[8], prev[8];
    uint16_t fg, bg;
    int8_t dir;
    uint32_t start_ms;
    uint16_t dur_ms;
    bool active, valid;
} roller_t;
static roller_t s_roll_temp = {94, 16, 66, 28, F_BIG, "", "", 0, 0, 1, 0, ROLL_MS, false, false};
static roller_t s_roll_set = {118, 44, 42, 14, F_MID, "", "", 0, 0, 1, 0, ROLL_MS, false, false};
static uint32_t s_anim_last_ms;

static void rollers_invalidate(void)
{
    s_roll_temp.valid = false;
    s_roll_set.valid = false;
}

static const aa_font_t *font_of(font_id_t f)
{
    return f == F_BIG ? &font_big : (f == F_MID ? &font_mid : &font_small);
}

static uint16_t glyph_w(const aa_font_t *f, char ch)
{
    if (ch == ' ') return 0U;
    char t[2] = {ch, 0};
    return aa_width(f, t);
}

static void roller_draw(roller_t *r, uint32_t now)
{
    const aa_font_t *f = font_of(r->font);
    uint32_t el = now - r->start_ms;
    bool anim = r->active && el < r->dur_ms;
    if (!anim) r->active = false;
    int32_t p = anim ? (int32_t)((el * 1000UL) / r->dur_ms) : 1000;
    int32_t eased = 1000 - ((1000 - p) * (1000 - p)) / 1000;      /* ease-out */
    int16_t shift = (int16_t)((eased * (int32_t)r->h) / 1000);
    int16_t gy = (int16_t)(((int16_t)r->h - (int16_t)f->height) / 2);
    size_t nc = strlen(r->cur), np = strlen(r->prev);
    int16_t x = (int16_t)(r->w - 2U);
    tile_begin(r->w, r->h, r->bg);
    for (size_t k = 0; k < nc || k < np; ++k) {
        char cc = k < nc ? r->cur[nc - 1U - k] : ' ';
        char pc = k < np ? r->prev[np - 1U - k] : ' ';
        uint16_t cw = glyph_w(f, cc), pw = glyph_w(f, pc);
        x = (int16_t)(x - (cw > pw ? cw : pw));
        char sc[2] = {cc, 0}, sp[2] = {pc, 0};
        if (!anim || cc == pc) {
            if (cc != ' ') aa_draw(f, x, gy, sc, r->fg, r->bg);
        } else {
            if (pc != ' ') aa_draw(f, x, (int16_t)(gy - r->dir * shift), sp, r->fg, r->bg);
            if (cc != ' ') aa_draw(f, x, (int16_t)(gy + r->dir * ((int16_t)r->h - shift)), sc, r->fg, r->bg);
        }
    }
    tile_flush(r->x, r->y);
}

static void roller_set(roller_t *r, const char *text, uint16_t fg, uint16_t bg)
{
    uint32_t now = board_millis();
    bool colour = r->fg != fg || r->bg != bg;
    if (!r->valid) {
        strncpy(r->cur, text, sizeof(r->cur) - 1U); r->cur[sizeof(r->cur) - 1U] = '\0';
        strcpy(r->prev, r->cur);
        r->fg = fg; r->bg = bg; r->valid = true; r->active = false;
        roller_draw(r, now);
        return;
    }
    if (strncmp(text, r->cur, sizeof(r->cur) - 1U) != 0) {
        int old_v = atoi(r->cur), new_v = atoi(text);
        strcpy(r->prev, r->cur);                 /* a running roll snaps to its target */
        strncpy(r->cur, text, sizeof(r->cur) - 1U); r->cur[sizeof(r->cur) - 1U] = '\0';
        r->dir = (int8_t)(new_v >= old_v ? 1 : -1);
        /* Back-to-back changes roll faster so the digits still settle. */
        r->dur_ms = (uint16_t)(now - r->start_ms < 2U * ROLL_MS ? ROLL_FAST_MS : ROLL_MS);
        r->start_ms = now;
        r->active = true;
        r->fg = fg; r->bg = bg;
        roller_draw(r, now);
    } else if (colour) {
        r->fg = fg; r->bg = bg;
        roller_draw(r, now);
    }
}

/* The displayed tip temperature ignores a 1-degree flicker unless it holds,
 * so the digits do not roll back and forth while the tip is regulated, and
 * while it changes fast (heat-up, cooling) it updates at most every 250 ms so
 * each value stays readable between rolls. */
#define TEMP_FAST_UPDATE_MS 250U
static int16_t s_shown_temp = -1000;
static uint32_t s_temp_pending_ms;
static uint32_t s_temp_shown_ms;
static int16_t displayed_temp(int16_t t)
{
    uint32_t now = board_millis();
    int16_t d = (int16_t)(t - s_shown_temp);
    if (s_shown_temp == -1000 ||
        ((d >= 2 || d <= -2) && now - s_temp_shown_ms >= TEMP_FAST_UPDATE_MS)) {
        s_shown_temp = t; s_temp_pending_ms = 0; s_temp_shown_ms = now;
    } else if (d >= 2 || d <= -2) {
        /* wait for the next fast-update slot */
    } else if (d != 0) {
        if (s_temp_pending_ms == 0U) s_temp_pending_ms = now;
        else if (now - s_temp_pending_ms >= 400U) {
            s_shown_temp = t; s_temp_pending_ms = 0; s_temp_shown_ms = now;
        }
    } else s_temp_pending_ms = 0;
    return s_shown_temp;
}

static void render_main(void)
{
    char b[24];
    const measurement_t *m = measure_get();
    const control_status_t *c = control_get();
    const power_status_t *pw = power_get();
    const tip_profile_t *p = profile_active();

    const char *state; uint16_t sbg, sfg;
    state_style(c, m->tip_present, &state, &sbg, &sfg);
    uint16_t stand = control_stand_countdown_s();
    const char *mid = "";
    uint16_t mid_fg = C_MUTED;
    char chip[8];
    if (c->state == STATE_BOOST) {
        snprintf(b, sizeof(b), "Boost to %d" DEG "C", c->active_setpoint_c);
        mid = b; mid_fg = C_YELLOW;
        /* The countdown lives in the state chip once the boost target is reached. */
        if (c->boost_reached) {
            snprintf(chip, sizeof(chip), "%u s", control_boost_remaining_s());
            state = chip;
        }
    } else if (stand != 0U) {
        snprintf(b, sizeof(b), "Sleep in %u s", stand);
        mid = b; mid_fg = C_CYAN;
    } else if (!p->calibrated) {
        mid = "Not calibrated"; mid_fg = C_RED;
    } else if (!m->vin_valid && m->vin_mv > 0U) {
        mid = "Check the supply"; mid_fg = C_RED;
    }
    top_bar(mid, mid_fg, state, sbg, sfg);

    /* Electrical readings */
    snprintf(b, sizeof(b), "%lu.%lu", (unsigned long)(m->vin_mv / 1000UL),
             (unsigned long)((m->vin_mv / 100UL) % 10UL));
    field(3, 2, 26, 30, 14, AL_LEFT, F_MID, b, m->vin_valid ? C_VOLT : C_RED, C_BG);
    bool live = pw->pulse_ticks != 0U && pw->cycle_measurement_valid;
    if (live) snprintf(b, sizeof(b), "%u.%02u", pw->average_current_ma / 1000U,
                       (pw->average_current_ma / 10U) % 100U);
    else snprintf(b, sizeof(b), "0.00");
    field(4, 33, 26, 30, 14, AL_LEFT, F_MID, b, C_AMP, C_BG);
    uint32_t mw = live ? pw->estimated_power_mw : 0U;
    if (mw >= 100000UL) snprintf(b, sizeof(b), "%lu", (unsigned long)(mw / 1000UL));
    else snprintf(b, sizeof(b), "%lu.%lu", (unsigned long)(mw / 1000UL),
                  (unsigned long)((mw / 100UL) % 10UL));
    field(5, 64, 26, 30, 14, AL_LEFT, F_MID, b, C_WATT, C_BG);

    /* Big tip temperature (rolling digits) */
    if (m->tip_present) snprintf(b, sizeof(b), "%d", displayed_temp(m->temperature_c));
    else { snprintf(b, sizeof(b), "---"); s_shown_temp = -1000; }
    roller_set(&s_roll_temp, b, m->tip_present ? C_TEMP : C_RED, C_BG);

    /* Set temperature (red while boosting, rolling digits) */
    bool boost = c->state == STATE_BOOST;
    fmt_temp(b, sizeof(b), boost ? c->active_setpoint_c : c->target_c);
    roller_set(&s_roll_set, b, boost ? C_RED : C_TEXT, C_BG);
    bool pending = control_target_pending() && !boost;
    field(10, 96, 47, 22, 11, AL_LEFT, F_SMALL, "SET", pending ? C_YELLOW : C_MUTED, C_BG);

    /* Power bar with percentage, where the presets used to be */
    uint16_t duty = pw->limited_permille;
    uint16_t fillw = (uint16_t)((duty * 66U) / 1000U);
    uint16_t bar_c = boost ? C_YELLOW : C_TEMP;
    snprintf(b, sizeof(b), "%u", fillw);
    if (field_changed(8, b, bar_c, C_GRID)) {
        tile_begin(66, 7, C_GRID);
        tile_rect(0, 0, (int16_t)fillw, 7, bar_c);
        tile_flush(2, 49);
    }
    snprintf(b, sizeof(b), "%u%%", (unsigned)((duty + 5U) / 10U));
    field(9, 70, 45, 24, 14, AL_RIGHT, F_SMALL, b, C_MUTED, C_BG);

    history_push();
    if (s_chart_dirty) draw_chart();
}

static void render_sleep(void)
{
    char b[24];
    const measurement_t *m = measure_get();
    const control_status_t *c = control_get();
    top_bar("Sleeping", C_CYAN, "SLEEP", C_BLUE, C_TEXT);
    if (m->tip_present) snprintf(b, sizeof(b), "%d", m->temperature_c);
    else snprintf(b, sizeof(b), "---");
    field(3, 20, 20, 92, 28, AL_RIGHT, F_BIG, b, C_TEMP, C_BG);
    field(4, 112, 22, 30, 14, AL_LEFT, F_MID, DEG "C", C_MUTED, C_BG);
    snprintf(b, sizeof(b), "Holding %d" DEG "C", c->active_setpoint_c);
    field(5, 0, 52, W, 14, AL_CENTER, F_MID, b, C_MUTED, C_BG);
    field(6, 0, 72, W, 12, AL_CENTER, F_SMALL, "Heater turns off in", C_MUTED, C_BG);
    uint32_t left = control_sleep_remaining_s();
    snprintf(b, sizeof(b), "%02lu:%02lu", (unsigned long)(left / 60UL), (unsigned long)(left % 60UL));
    field(7, 0, 86, W, 28, AL_CENTER, F_BIG, b, C_TEXT, C_BG);
    field(8, 0, 116, W, 12, AL_CENTER, F_SMALL, "Lift the iron to wake", C_DIM, C_BG);
}

/* ------------------------------------------------------------------ */
/* Settings                                                            */
/* ------------------------------------------------------------------ */
static void menu_item_text(uint8_t item, char *label, size_t ln, char *value, size_t vn)
{
    const char *l = "";
    value[0] = '\0';
    switch (item) {
    case S_TIP:
        l = "Handle";
        snprintf(value, vn, "%s%s", profile_get(s_editing && s_sel == S_TIP ? s_pending_tip :
                 profile_active_index())->name,
                 profile_get(s_editing && s_sel == S_TIP ? s_pending_tip :
                 profile_active_index())->calibrated ? "" : " !");
        break;
    case S_BOOST_TEMP:
        l = "Boost temp";
        if (control_boost_delta_c() == 0U) snprintf(value, vn, "Off");
        else snprintf(value, vn, "+%u" DEG "C", control_boost_delta_c());
        break;
    case S_BOOST_TIME: l = "Boost time"; snprintf(value, vn, "%u s", control_boost_time_s()); break;
    case S_SLEEP_DELAY:
        l = "Sleep after";
        if (control_sleep_delay_s() == 0U) snprintf(value, vn, "Instant");
        else snprintf(value, vn, "%u s", control_sleep_delay_s());
        break;
    case S_SLEEP_TEMP: l = "Sleep temp"; fmt_temp(value, vn, control_sleep_temp_c()); break;
    case S_SLEEP_OFF: l = "Off after sleep"; snprintf(value, vn, "%u min", control_sleep_off_min()); break;
    case S_MAX_TEMP: l = "Max temp"; fmt_temp(value, vn, (int16_t)control_max_target_c()); break;
    case S_ROOM: l = "Room temp"; fmt_temp(value, vn, profile_ambient_c()); break;
    case S_SUPPLY: {
        /* Live reading; turn the knob until it matches a meter. */
        uint32_t mv = measure_get()->vin_mv + 5U;
        l = "Supply volts";
        snprintf(value, vn, "%lu.%02lu V", (unsigned long)(mv / 1000UL),
                 (unsigned long)((mv / 10UL) % 100UL));
        break;
    }
    case S_ENC_STEP: l = "Knob step"; snprintf(value, vn, "%u" DEG "C", control_encoder_step_c()); break;
    case S_ENC_DIR: l = "Knob direction"; snprintf(value, vn, input_encoder_reversed() ? "Reverse" : "Normal"); break;
    case S_BRIGHT: l = "Brightness"; snprintf(value, vn, "%u%%", board_backlight_get()); break;
    case S_SOUND: l = "Sound"; snprintf(value, vn, board_buzzer_enabled() ? "On" : "Off"); break;
    case S_READY_BEEP: l = "Ready beep"; snprintf(value, vn, board_setpoint_beep_get() ? "On" : "Off"); break;
    case S_TIP_CHECK: l = "Startup logo"; snprintf(value, vn, settings_tip_warning_enabled() ? "On" : "Off"); break;
    case S_FLIP: l = "Flip screen"; snprintf(value, vn, s_flipped ? "On" : "Off"); break;
    case S_CALIBRATE: l = "Calibrate tip"; snprintf(value, vn, ">"); break;
    case S_RESET: l = "Factory reset"; snprintf(value, vn, s_reset_armed ? "Click again" : ">"); break;
    case S_ABOUT: l = "About"; snprintf(value, vn, ">"); break;
    default: l = "Save and exit"; break;
    }
    snprintf(label, ln, "%s", l);
}

static void render_settings(void)
{
    char label[20], value[16], key[24];
    snprintf(key, sizeof(key), "%u/%u", (unsigned)(s_sel + 1U), (unsigned)S_COUNT);
    field(0, 0, 0, 110, 14, AL_LEFT, F_MID, s_menu_note ? s_menu_note : "Settings",
          s_menu_note ? C_YELLOW : C_TEXT, C_PANEL);
    field(1, 110, 0, 50, 14, AL_RIGHT, F_SMALL, key, C_MUTED, C_PANEL);
    for (uint8_t row = 0; row < MENU_ROWS; ++row) {
        uint8_t item = (uint8_t)(s_scroll + row);
        uint16_t y = (uint16_t)(MENU_Y + row * MENU_ROW_H);
        if (item >= S_COUNT) { field((uint8_t)(2U + row), 0, y, W, MENU_ROW_H, AL_LEFT, F_MID, " ", C_BG, C_BG); continue; }
        menu_item_text(item, label, sizeof(label), value, sizeof(value));
        bool sel = item == s_sel;
        uint16_t bg = sel ? (s_editing ? RGB(70, 44, 10) : C_PANEL_HI) : C_BG;
        uint16_t vfg = sel && s_editing ? C_TEMP : (item == S_EXIT ? C_GREEN : C_MUTED);
        if (item == S_TIP && value[strlen(value) - 1U] == '!') vfg = sel && s_editing ? C_TEMP : C_RED;
        snprintf(key, sizeof(key), "%.11s|%.11s", label, value);
        if (!field_changed((uint8_t)(2U + row), key, vfg, bg)) continue;
        tile_begin(W, MENU_ROW_H, bg);
        tile_text(6, 0, 100, AL_LEFT, F_MID, label, item == S_EXIT ? C_GREEN : C_TEXT, bg);
        tile_text(0, 0, (int16_t)(W - 6), AL_RIGHT, F_MID, value, vfg, bg);
        if (sel) tile_rect(0, 0, 3, MENU_ROW_H, s_editing ? C_TEMP : C_BLUE);
        tile_flush(0, y);
    }
}

static void menu_scroll_to_selection(void)
{
    if (s_sel < s_scroll) s_scroll = s_sel;
    if (s_sel >= s_scroll + MENU_ROWS) s_scroll = (uint8_t)(s_sel - MENU_ROWS + 1U);
}

/* ------------------------------------------------------------------ */
/* Calibration wizard                                                  */
/* ------------------------------------------------------------------ */
static void cal_set_target(void)
{
    control_set_target_c(profile_calibrate_temperature(s_cal_anchor[s_cal_point]));
}

static void cal_finish(bool keep_target)
{
    control_long_press();
    profile_set_calibration_mode(false);
    if (!keep_target) control_set_target_c(s_cal_prev_target);
    s_cal_active = false;
}

static void cal_start(void)
{
    const measurement_t *m = measure_get();
    if (!m->tip_present || !m->quiet_sample_valid) { s_cal_msg = "Connect the iron first"; return; }
    if (safety_latched()) { s_cal_msg = "Clear the fault first"; return; }
    if (control_get()->stand_active) { s_cal_msg = "Take the iron off the stand"; return; }
    profile_set_calibration_mode(true);
    memcpy(s_cal_values, profile_active()->temp_cal_c, sizeof(s_cal_values));
    s_cal_prev_target = control_get()->target_c;
    s_cal_point = 0U;
    s_cal_stable_since = 0U;
    s_cal_msg = NULL;
    s_cal_active = true;
    cal_set_target();
    control_short_press();
    if (!control_get()->heating_enabled) {
        cal_finish(false);
        s_cal_msg = "Heating did not start";
        return;
    }
    s_screen = SCR_NONE;
}

static void cal_press(void)
{
    const measurement_t *m = measure_get();
    const control_status_t *c = control_get();
    int16_t diff = (int16_t)(m->temperature_base_c - s_cal_anchor[s_cal_point]);
    if (diff < 0) diff = (int16_t)-diff;
    if (!m->tip_present || !c->heating_enabled || c->stand_active || safety_latched() ||
        diff > 25 || s_cal_stable_since == 0U || board_millis() - s_cal_stable_since < 2000U) {
        s_cal_msg = "Wait until it says Ready";
        return;
    }
    s_cal_msg = NULL;
    if (s_cal_point + 1U < PROFILE_CAL_POINT_COUNT) {
        ++s_cal_point;
        if (s_cal_values[s_cal_point] <= s_cal_values[s_cal_point - 1U])
            s_cal_values[s_cal_point] = (int16_t)(s_cal_values[s_cal_point - 1U] + 1);
        s_cal_stable_since = 0U;
        cal_set_target();
        return;
    }
    uint8_t tip = profile_active_index();
    cal_finish(false);
    if (!profile_set_calibration_values(tip, s_cal_values)) { s_menu_note = "Invalid values"; return; }
    profile_set_calibrated(tip, true);
    measure_profile_changed();
    s_menu_note = settings_save() ? "Calibration saved" : "Save failed";
    board_buzzer_pattern(2U);
}

static void cal_rotate(int8_t d)
{
    uint8_t i = s_cal_point;
    int32_t v = (int32_t)s_cal_values[i] + d;
    int16_t minimum = i == 0U ? 0 : (int16_t)(s_cal_values[i - 1U] + 1);
    if (v < minimum) v = minimum;
    if (v > 550) v = 550;
    s_cal_values[i] = (int16_t)v;
}

static void render_cal(void)
{
    char b[24];
    const measurement_t *m = measure_get();
    const control_status_t *c = control_get();
    int16_t diff = (int16_t)(m->temperature_base_c - s_cal_anchor[s_cal_point]);
    if (diff < 0) diff = (int16_t)-diff;
    bool ok = m->tip_present && m->quiet_sample_valid && c->heating_enabled &&
              !c->stand_active && !safety_latched() && diff <= 25 && m->temp_burst_span_raw <= 50U;
    if (ok) { if (s_cal_stable_since == 0U) s_cal_stable_since = board_millis(); }
    else s_cal_stable_since = 0U;
    bool ready = ok && board_millis() - s_cal_stable_since >= 2000U;

    snprintf(b, sizeof(b), "Calibrate %s", profile_active_name());
    field(0, 0, 0, 120, 14, AL_LEFT, F_MID, b, C_TEXT, C_PANEL);
    snprintf(b, sizeof(b), "%u/6", (unsigned)(s_cal_point + 1U));
    field(1, 120, 0, 40, 14, AL_RIGHT, F_MID, b, C_YELLOW, C_PANEL);
    snprintf(b, sizeof(b), "Step %d" DEG "C", s_cal_anchor[s_cal_point]);
    field(2, 0, 18, 80, 12, AL_LEFT, F_SMALL, b, C_MUTED, C_BG);
    field(3, 80, 18, 80, 12, AL_RIGHT, F_SMALL, "Station reads", C_MUTED, C_BG);
    snprintf(b, sizeof(b), "%d", m->temperature_base_c);
    field(4, 90, 28, 70, 28, AL_RIGHT, F_BIG, b, C_TEMP, C_BG);
    field(5, 0, 60, W, 12, AL_LEFT, F_SMALL, "Thermometer (turn knob)", C_MUTED, C_BG);
    snprintf(b, sizeof(b), "%d", s_cal_values[s_cal_point]);
    field(6, 0, 72, 70, 28, AL_RIGHT, F_BIG, b, C_CYAN, C_BG);
    field(7, 70, 74, 30, 14, AL_LEFT, F_MID, DEG "C", C_MUTED, C_BG);
    const char *status = s_cal_msg ? s_cal_msg : (ready ? "Ready - click to save" :
                         (ok ? "Settling..." : "Heating..."));
    field(8, 0, 104, W, 12, AL_CENTER, F_SMALL, status,
          s_cal_msg ? C_RED : (ready ? C_GREEN : C_YELLOW), C_BG);
    field(9, 0, 116, W, 12, AL_CENTER, F_SMALL, "Hold knob to abort", C_DIM, C_BG);
}

static void render_cal_intro(void)
{
    char b[24];
    snprintf(b, sizeof(b), "Calibrate %s", profile_active_name());
    field(0, 0, 0, W, 14, AL_LEFT, F_MID, b, C_TEXT, C_PANEL);
    field(1, 0, 20, W, 12, AL_LEFT, F_SMALL, " You need a tip thermometer.", C_YELLOW, C_BG);
    field(2, 0, 34, W, 12, AL_LEFT, F_SMALL, " Heats in 6 steps, 100-450" DEG "C.", C_TEXT, C_BG);
    field(3, 0, 46, W, 12, AL_LEFT, F_SMALL, " At each step, turn the knob", C_TEXT, C_BG);
    field(4, 0, 58, W, 12, AL_LEFT, F_SMALL, " to match the thermometer.", C_TEXT, C_BG);
    field(5, 0, 74, W, 12, AL_LEFT, F_SMALL, " The tip may overshoot.", C_RED, C_BG);
    field(6, 0, 86, W, 12, AL_LEFT, F_SMALL, " Hold the knob to stop.", C_TEXT, C_BG);
    field(7, 0, 104, W, 12, AL_CENTER, F_SMALL, s_cal_msg ? s_cal_msg : "Click to start",
          s_cal_msg ? C_RED : C_GREEN, C_BG);
}

/* ------------------------------------------------------------------ */
/* About, popups                                                       */
/* ------------------------------------------------------------------ */
static void render_about(void)
{
    char b[24];
    field(0, 0, 0, W, 14, AL_LEFT, F_MID, "About", C_TEXT, C_PANEL);
    field(1, 0, 20, W, 14, AL_LEFT, F_MID, STATION_FW_NAME, C_TEMP, C_BG);
    snprintf(b, sizeof(b), " Version %s", STATION_FW_VERSION);
    field(2, 0, 38, W, 12, AL_LEFT, F_SMALL, b, C_TEXT, C_BG);
    field(3, 0, 54, W, 12, AL_LEFT, F_SMALL, " PCB: toanthodien16", C_MUTED, C_BG);
    field(4, 0, 66, W, 12, AL_LEFT, F_SMALL, " Base firmware: imdolce", C_MUTED, C_BG);
    field(5, 0, 78, W, 12, AL_LEFT, F_SMALL, " Inspired by AxxSolder", C_MUTED, C_BG);
    field(6, 0, 96, W, 12, AL_LEFT, F_SMALL, " C245 data: original author", C_MUTED, C_BG);
    field(7, 0, 114, W, 12, AL_CENTER, F_SMALL, "Click to close", C_GREEN, C_BG);
}

static const char *fault_detail(safety_fault_t f)
{
    switch (f) {
    case FAULT_SENSOR_OPEN: return "Check iron and tip";
    case FAULT_SENSOR_SHORT: return "Temp signal too low";
    case FAULT_OVERTEMP: return "Tip above limit";
    case FAULT_OFF_CURRENT: return "Current while off";
    case FAULT_SUPPLY_RANGE: return "Supply not 9-28 V";
    case FAULT_ADC_STALE: return "ADC data stopped";
    case FAULT_PULSE_TIMEOUT: return "Heater pulse timeout";
    case FAULT_SENSOR_UNSTABLE: return "Temp signal dropped";
    default: return "Safety stop";
    }
}

static void render_fault(void)
{
    char b[48];
    const fault_snapshot_t *s = safety_snapshot();
    field(0, 0, 0, W, 14, AL_LEFT, F_MID, "Heater stopped", C_TEXT, C_RED);
    field(1, 0, 22, W, 14, AL_LEFT, F_MID, safety_fault_text(s_popup_fault), C_YELLOW, C_BG);
    field(2, 0, 40, W, 14, AL_LEFT, F_MID, fault_detail(s_popup_fault), C_TEXT, C_BG);
    snprintf(b, sizeof(b), " Supply %lu.%lu V   Tip %d" DEG "C", (unsigned long)(s->vin_mv / 1000UL),
             (unsigned long)((s->vin_mv / 100UL) % 10UL), s->temperature_c);
    field(3, 0, 62, W, 12, AL_LEFT, F_SMALL, b, C_CYAN, C_BG);
    snprintf(b, sizeof(b), " PA1 %u   PA6 %u   Duty %u%%", s->temp_raw, s->current_raw,
             s->duty_permille / 10U);
    field(4, 0, 74, W, 12, AL_LEFT, F_SMALL, b, C_MUTED, C_BG);
    field(5, 0, 92, W, 12, AL_LEFT, F_SMALL, s_popup_fault == FAULT_SENSOR_UNSTABLE ?
          " Power off and inspect the iron" : " Fix the cause, then click twice", C_TEXT, C_BG);
    field(6, 0, 114, W, 12, AL_CENTER, F_SMALL, "Click to close", C_GREEN, C_BG);
}

/* Draw a run-length coded logo, centred horizontally, top at y. */
static void draw_logo(const logo_t *L, uint16_t y)
{
    uint16_t x0 = (uint16_t)((W - L->width) / 2U);
    uint16_t row = 0, col = 0, strip_y = 0;
    uint16_t strip_h = L->height < 28U ? L->height : 28U;
    tile_begin(L->width, strip_h, C_BG);
    for (uint16_t i = 0; i < L->rle_len && row < L->height; ++i) {
        uint8_t run = (uint8_t)((L->rle[i] >> 5) + 1U);
        uint8_t idx = (uint8_t)(L->rle[i] & 31U);
        uint16_t colour = L->palette[idx];
        while (run-- && row < L->height) {
            if (idx != 0U) tile_px((int16_t)col, (int16_t)(row - strip_y), colour);
            if (++col == L->width) {
                col = 0;
                ++row;
                if (row - strip_y == strip_h || row == L->height) {
                    tile_flush(x0, (uint16_t)(y + strip_y));
                    strip_y = row;
                    uint16_t left = (uint16_t)(L->height - row);
                    strip_h = left < 28U ? left : (uint16_t)28U;
                    if (strip_h) tile_begin(L->width, strip_h, C_BG);
                }
            }
        }
    }
}

/* Start-up: logo, then the selected handle and whether a tip is seen. */
static void render_splash(bool fresh)
{
    char b[28];
    const measurement_t *m = measure_get();
    const tip_profile_t *p = profile_active();
    if (fresh) draw_logo(&logo_small, (uint16_t)((84U - logo_small.height) / 2U));
    uint16_t chip = profile_active_index() == TIP_C245 ? C_TEMP : C_CYAN;
    field(0, 50, 88, 60, 18, AL_CENTER, F_MID, p->name, C_BG, chip);
    if (!m->quiet_sample_valid) snprintf(b, sizeof(b), "Checking tip...");
    else if (!m->tip_present) snprintf(b, sizeof(b), "No tip connected");
    else if (!p->calibrated) snprintf(b, sizeof(b), "Tip ready - not calibrated");
    else snprintf(b, sizeof(b), "Tip ready  %d" DEG "C", m->temperature_c);
    uint16_t fg = !m->quiet_sample_valid ? C_MUTED :
                  (!m->tip_present || !p->calibrated ? C_RED : C_GREEN);
    field(1, 0, 110, W, 14, AL_CENTER, F_SMALL, b, fg, C_BG);
}

/* Heater off: only the logo (and a hot-tip reminder while it cools). */
static void render_logo(bool fresh)
{
    char b[24];
    const measurement_t *m = measure_get();
    if (fresh) draw_logo(&logo_large, (uint16_t)((104U - logo_large.height) / 2U));
    bool hot = m->tip_present && m->temperature_c >= LOGO_HOT_C;
    if (hot) snprintf(b, sizeof(b), "Tip hot  %d" DEG "C", m->temperature_c);
    else b[0] = '\0';
    field(0, 0, 112, W, 14, AL_CENTER, F_SMALL, b, C_TEMP, C_BG);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
void ui_init(ST7735_Object_t *lcd)
{
    s_lcd = lcd;
    ui_set_display_flipped(s_flipped);
    s_splash = settings_tip_warning_enabled();
    s_splash_until_ms = board_millis() + SPLASH_MS;
    s_screen = SCR_NONE;
}

bool ui_display_flipped(void) { return s_flipped; }
void ui_set_display_flipped(bool flipped)
{
    s_flipped = flipped;
    if (s_lcd != NULL)
        ST7735_SetOrientation(s_lcd, flipped ? ST7735_ORIENTATION_LANDSCAPE_ROT180 :
                                               ST7735_ORIENTATION_LANDSCAPE);
    force_redraw();
}

bool ui_settings_active(void) { return s_settings_open; }
void ui_animate(void)
{
    if (s_screen != SCR_MAIN || (!s_roll_temp.active && !s_roll_set.active)) return;
    uint32_t now = board_millis();
    if (now - s_anim_last_ms < ROLL_FRAME_MS) return;
    s_anim_last_ms = now;
    if (s_roll_temp.active) roller_draw(&s_roll_temp, now);
    if (s_roll_set.active) roller_draw(&s_roll_set, now);
}

bool ui_main_screen_active(void) { return s_screen == SCR_MAIN; }

/* Knob outside the menu: only the main screen changes the set temperature.
 * On the sleep screen it does nothing; on the logo screen the input (noted
 * by ui_note_input) just brings the main screen back. */
void ui_main_rotate(int8_t detents)
{
    if (s_screen == SCR_MAIN) control_rotate(detents);
}

/* Click outside the menu. After turning the knob, a click on the main screen
 * saves the new set temperature: while heating it only saves (the iron keeps
 * heating), while off it saves and starts heating. Never saved during boost.
 * Without a pending change the click turns the heater on or off. */
void ui_main_press(void)
{
    const control_status_t *c = control_get();
    if (s_screen == SCR_MAIN && c->state != STATE_BOOST && control_target_pending()) {
        bool heating = c->heating_enabled;
        control_commit_target();
        board_buzzer_pattern(settings_save() ? 2U : 3U);
        if (heating) return;
    }
    control_short_press();
}

void ui_note_input(void)
{
    s_last_input_ms = board_millis();
    if (s_last_input_ms == 0U) s_last_input_ms = 1U;
}

void ui_open_settings(void)
{
    s_splash = false;
    s_settings_open = true;
    s_about_open = false;
    s_editing = false;
    s_reset_armed = false;
    s_menu_note = NULL;
    s_cal_msg = NULL;
    s_sel = 0U; s_scroll = 0U;
    force_redraw();
}

void ui_close_settings(void)
{
    if (s_cal_active) cal_finish(false);
    profile_set_calibration_mode(false);
    if (s_editing && s_sel == S_TIP && s_pending_tip != profile_active_index())
        control_select_profile(s_pending_tip);
    s_editing = false;
    s_settings_open = false;
    s_about_open = false;
    s_cal_intro = false;
    s_cal_msg = NULL;
    if (settings_dirty() && !settings_save()) board_buzzer_pattern(3U);
    force_redraw();
}

bool ui_popup_active(void) { return s_fault_popup || s_splash; }
void ui_popup_dismiss(void)
{
    if (s_fault_popup) s_fault_popup = false;
    else s_splash = false;
    force_redraw();
}

static void menu_adjust(uint8_t item, int8_t d)
{
    int32_t v;
    switch (item) {
    case S_TIP: s_pending_tip = (uint8_t)((s_pending_tip + TIP_COUNT + (d > 0 ? 1 : -1)) % TIP_COUNT); break;
    case S_BOOST_TEMP:
        v = (int32_t)control_boost_delta_c() + d * 5;
        control_set_boost_delta_c((uint8_t)(v < 0 ? 0 : (v > 100 ? 100 : v)));
        break;
    case S_BOOST_TIME:
        v = (int32_t)control_boost_time_s() + d * 5;
        control_set_boost_time_s((uint8_t)(v < 10 ? 10 : (v > 120 ? 120 : v)));
        break;
    case S_SLEEP_DELAY:
        v = (int32_t)control_sleep_delay_s() + d;
        control_set_sleep_delay_s((uint8_t)(v < 0 ? 0 : (v > 60 ? 60 : v)));
        break;
    case S_SLEEP_TEMP: control_set_sleep_temp_c((int16_t)(control_sleep_temp_c() + d * 10)); break;
    case S_SLEEP_OFF:
        v = (int32_t)control_sleep_off_min() + d;
        control_set_sleep_off_min((uint8_t)(v < 1 ? 1 : (v > 60 ? 60 : v)));
        break;
    case S_MAX_TEMP:
        v = (int32_t)control_max_target_c() + d * 10;
        control_set_max_target_c((uint16_t)(v < 150 ? 150 : v));
        break;
    case S_SUPPLY:
        measure_set_vin_trim((int16_t)(measure_vin_trim() + d * 2));
        break;
    case S_ROOM:
        profile_set_ambient_c((int16_t)(profile_ambient_c() + d));
        measure_profile_changed();
        break;
    case S_ENC_STEP: {
        uint8_t s = control_encoder_step_c();
        int8_t idx = (int8_t)((s == 1U ? 0 : (s == 5U ? 1 : 2)) + (d > 0 ? 1 : -1));
        if (idx < 0) idx = 2;
        if (idx > 2) idx = 0;
        control_set_encoder_step_c(idx == 0 ? 1U : (idx == 1 ? 5U : 10U));
        break;
    }
    case S_BRIGHT:
        v = (int32_t)board_backlight_get() + d * 5;
        board_backlight_set((uint8_t)(v < 10 ? 10 : (v > 100 ? 100 : v)));
        break;
    default: break;
    }
}

void ui_settings_rotate(int8_t d)
{
    if (d == 0) return;
    if (s_cal_active) { cal_rotate(d); return; }
    if (s_about_open || s_screen == SCR_CAL_INTRO) return;
    s_menu_note = NULL;
    if (s_editing) { menu_adjust(s_sel, d); return; }
    s_reset_armed = false;
    int16_t sel = (int16_t)s_sel + d;
    while (sel < 0) sel += S_COUNT;
    while (sel >= (int16_t)S_COUNT) sel -= S_COUNT;
    s_sel = (uint8_t)sel;
    menu_scroll_to_selection();
}

void ui_settings_press(void)
{
    if (s_about_open) { s_about_open = false; force_redraw(); return; }
    if (s_cal_active) { cal_press(); return; }
    if (s_cal_intro) { cal_start(); if (s_cal_active) s_cal_intro = false; force_redraw(); return; }
    s_menu_note = NULL;
    switch (s_sel) {
    case S_TIP:
        if (!s_editing) { s_pending_tip = profile_active_index(); s_editing = true; }
        else {
            s_editing = false;
            if (s_pending_tip != profile_active_index()) {
                control_select_profile(s_pending_tip);
                s_menu_note = profile_active()->calibrated ? "Handle changed" : "Calibrate this tip";
            }
        }
        return;
    case S_ENC_DIR: input_set_encoder_reversed(!input_encoder_reversed()); return;
    case S_SOUND: board_buzzer_enable(!board_buzzer_enabled()); return;
    case S_READY_BEEP: board_setpoint_beep_enable(!board_setpoint_beep_get()); return;
    case S_TIP_CHECK: settings_set_tip_warning_enabled(!settings_tip_warning_enabled()); return;
    case S_FLIP: ui_set_display_flipped(!s_flipped); return;
    case S_CALIBRATE:
        control_long_press();
        s_cal_intro = true;
        s_cal_msg = NULL;
        force_redraw();
        return;
    case S_RESET:
        if (!s_reset_armed) { s_reset_armed = true; return; }
        s_reset_armed = false;
        settings_factory_defaults();
        measure_profile_changed();
        control_reconfigure();
        s_menu_note = settings_save() ? "Defaults restored" : "Save failed";
        return;
    case S_ABOUT: s_about_open = true; force_redraw(); return;
    case S_EXIT: ui_close_settings(); return;
    default: s_editing = !s_editing; return;
    }
}

void ui_render(void)
{
    const control_status_t *c = control_get();

    safety_fault_t fault = safety_fault();
    if (fault != FAULT_NONE && fault != s_popup_fault) {
        s_popup_fault = fault;
        s_fault_popup = true;
        if (s_cal_active) cal_finish(false);
        s_cal_intro = false;
    } else if (fault == FAULT_NONE) s_popup_fault = FAULT_NONE;

    uint32_t now = board_millis();
    if (s_splash && (int32_t)(now - s_splash_until_ms) >= 0) { s_splash = false; force_redraw(); }

    /* Turning the heater off (click or long press) switches to the logo.
     * Knob input shows the main screen again for a while; a click heats. */
    bool heating = c->heating_enabled;
    /* This includes the sleep timer running out: the heater stops and the
     * logo is shown with the backlight dimmed until the iron is lifted. */
    if (s_was_heating && !heating && fault == FAULT_NONE) {
        s_logo_mode = true;
        s_logo_since_ms = now;
    }
    if (heating || fault != FAULT_NONE) s_logo_mode = false;
    s_was_heating = heating;
    bool woken = s_last_input_ms != 0U && s_last_input_ms > s_logo_since_ms + 400U &&
                 now - s_last_input_ms < LOGO_WAKE_MS;

    screen_t want;
    if (s_fault_popup) want = SCR_FAULT;
    else if (s_splash) want = SCR_SPLASH;
    else if (s_settings_open) {
        want = s_about_open ? SCR_ABOUT : (s_cal_active ? SCR_CAL :
               (s_cal_intro ? SCR_CAL_INTRO : SCR_SETTINGS));
    } else if (c->state == STATE_SLEEP) want = SCR_SLEEP;
    else if (s_logo_mode && !woken) want = SCR_LOGO;
    else want = SCR_MAIN;

    if (want != SCR_SETTINGS && want != SCR_CAL && want != SCR_CAL_INTRO && want != SCR_ABOUT)
        s_cal_intro = false;
    bool fresh = want != s_screen;
    set_screen(want);
    switch (want) {
    case SCR_MAIN: if (fresh) draw_main_static(); render_main(); break;
    case SCR_SLEEP: history_push(); render_sleep(); break;
    case SCR_SETTINGS: history_push(); render_settings(); break;
    case SCR_CAL_INTRO: render_cal_intro(); break;
    case SCR_CAL: history_push(); render_cal(); break;
    case SCR_ABOUT: render_about(); break;
    case SCR_FAULT: render_fault(); break;
    case SCR_SPLASH: history_push(); render_splash(fresh); break;
    case SCR_LOGO: history_push(); render_logo(fresh); break;
    default: break;
    }
}
