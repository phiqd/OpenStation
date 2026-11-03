/* Host simulator: runs the real ui.c, control.c, safety.c and profiles.c
 * against mocked hardware and the two-node tip model in mock.c. It checks the
 * boost/sleep behaviour and writes screenshots (PPM) of every screen.
 *
 * Build and run:  sim/run.sh   (outputs to sim/out/) */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "board.h"
#include "control.h"
#include "encoder.h"
#include "measure.h"
#include "power.h"
#include "profiles.h"
#include "safety.h"
#include "settings.h"
#include "st7735.h"
#include "ui.h"
#include "mock.h"

static bool g_tip_warn = true;
bool settings_tip_warning_enabled(void) { return g_tip_warn; }
void settings_set_tip_warning_enabled(bool e) { g_tip_warn = e; }
static int g_saves;
bool settings_save(void) { ++g_saves; return true; }
bool settings_dirty(void) { return true; }
void settings_factory_defaults(void) {}

/* ---------------- LCD mock ---------------- */
static uint16_t g_fb[128][160];
static bool g_lcd_on = true;
int32_t ST7735_FillRGBRect(ST7735_Object_t *o, uint32_t x, uint32_t y, uint8_t *d, uint32_t w, uint32_t h)
{
    (void)o;
    for (uint32_t j = 0; j < h; ++j) for (uint32_t i = 0; i < w; ++i) {
        if (y + j < 128 && x + i < 160)
            g_fb[y + j][x + i] = (uint16_t)((d[(j * w + i) * 2] << 8) | d[(j * w + i) * 2 + 1]);
    }
    return 0;
}
int32_t ST7735_FillRect(ST7735_Object_t *o, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c)
{
    (void)o;
    for (uint32_t j = y; j < y + h && j < 128; ++j) for (uint32_t i = x; i < x + w && i < 160; ++i) g_fb[j][i] = (uint16_t)c;
    return 0;
}
int32_t ST7735_SetOrientation(ST7735_Object_t *o, uint32_t v) { (void)o; (void)v; return 0; }
int32_t ST7735_DisplayOn(ST7735_Object_t *o) { (void)o; g_lcd_on = true; return 0; }
int32_t ST7735_DisplayOff(ST7735_Object_t *o) { (void)o; g_lcd_on = false; return 0; }

static void shot(const char *name)
{
    char path[128];
    snprintf(path, sizeof(path), "sim/out/%s.ppm", name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6 160 128 255\n");
    for (int y = 0; y < 128; ++y) for (int x = 0; x < 160; ++x) {
        uint16_t c = g_lcd_on ? g_fb[y][x] : 0;
        uint8_t px[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63),
                         (uint8_t)((c & 31) * 255 / 31)};
        if (g_lcd_on && g_override) for (int k = 0; k < 3; ++k) px[k] = (uint8_t)(px[k] * g_override_pct / 100);
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

/* ---------------- main-loop emulation ---------------- */
static ST7735_Object_t g_lcd;
static void run_ms(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 50) {
        mock_cycle();
        control_run_cycle();
        const control_status_t *c = control_get();
        safety_supervise((uint8_t)c->state, c->heating_enabled);
        if ((g_ms % 100) == 0) ui_render();
        ui_animate();
    }
}
static void in_rotate(int8_t d) { ui_note_input(); if (ui_popup_active()) return; if (ui_settings_active()) ui_settings_rotate(d); else ui_main_rotate(d); }
static void in_click(void) { ui_note_input(); if (ui_popup_active()) ui_popup_dismiss(); else if (ui_settings_active()) ui_settings_press(); else ui_main_press(); }
static void in_double(void) { ui_note_input(); if (!ui_popup_active() && !ui_settings_active()) control_double_press(); }
static void in_long(void) { ui_note_input(); control_long_press(); if (ui_settings_active()) ui_close_settings(); else ui_open_settings(); }

static int g_fail;
static void check(bool ok, const char *fmt, ...)
{
    va_list a; va_start(a, fmt);
    printf("%s ", ok ? "PASS" : "FAIL"); vprintf(fmt, a); printf("\n");
    va_end(a);
    if (!ok) ++g_fail;
}
static const char *st(void) { return control_state_text(control_get()->state); }

int main(void)
{
    profiles_factory_reset();
    control_set_max_target_c(450);
    mock_reset(25.0);
    mock_cycle();
    safety_init();
    control_init();
    ui_init(&g_lcd);
    run_ms(1500);
    shot("01_splash");
    check(ui_popup_active(), "start-up logo shown");
    run_ms(2500);
    check(!ui_popup_active(), "start-up logo gone after 3.5 s");
    check(control_get()->state == STATE_IDLE && !control_get()->heating_enabled,
          "idle after boot, not heating (%s)", st());
    shot("02_main_off");

    /* Heater off -> logo only; knob shows main for 10 s; click heats */
    in_click(); run_ms(3000);
    check(control_get()->heating_enabled, "click heats from main");
    in_click(); run_ms(500);
    shot("02b_logo_hot");
    in_rotate(1); run_ms(500);
    check(control_get()->target_c == 300 && ui_main_screen_active(),
          "knob on the logo screen: main screen back, set temp unchanged (%d)", control_get()->target_c);
    in_rotate(1); run_ms(500);
    check(control_get()->target_c == 305, "knob on the main screen: 305 (%d)", control_get()->target_c);
    shot("02c_main_after_knob");
    run_ms(10500);
    shot("02d_logo_again");
    in_click(); run_ms(500);
    check(control_get()->heating_enabled && control_target_pending(),
          "click on the logo screen heats (305 not saved: logo is not the main screen)");
    in_click(); run_ms(500);
    check(control_get()->heating_enabled && !control_target_pending(),
          "click on the main screen while heating saves 305 and keeps heating");
    in_click(); run_ms(240000);
    check(!control_get()->heating_enabled, "click again (nothing pending) turns the heater off");
    shot("02e_logo_cool");
    g_tip_connected = false; run_ms(300);
    ui_init(&g_lcd); run_ms(500);
    shot("01b_splash_no_tip");
    g_tip_connected = true; run_ms(3500);
    in_rotate(-1); run_ms(300);           /* logo screen: wakes the main screen only */
    in_rotate(-1); run_ms(300);
    check(!control_get()->heating_enabled && control_target_pending(), "off: knob to 300, pending");
    in_click(); run_ms(300);
    check(control_get()->heating_enabled && !control_target_pending() && control_get()->target_c == 300,
          "off: click saves 300 and starts heating");
    in_click(); run_ms(300);
    check(!control_get()->heating_enabled, "click turns it off");

    /* Heat to 300 C */
    in_click();
    run_ms(12000);
    check(control_get()->state == STATE_REGULATE, "regulating at 300 (%s, %.0f C)", st(), g_t_heater);
    check(fabs(g_t_heater - 300.0) < 6.0, "tip within 6 C of 300 (%.1f)", g_t_heater);
    /* A solder joint pulls heat for 3 s, then the tip recovers */
    g_load_w = 35.0; run_ms(3000); g_load_w = 0.0; run_ms(5000);
    shot("03_main_ready");

    /* Boost: +50 C, timer counts only after reaching 350 C */
    in_double();
    run_ms(300);
    check(control_get()->state == STATE_BOOST && !control_get()->boost_reached,
          "boost heating, timer not started (%s)", st());
    check(control_boost_remaining_s() == 30, "shows full 30 s while heating (%u)", control_boost_remaining_s());
    uint32_t t0 = g_ms;
    while (!control_get()->boost_reached && g_ms - t0 < 20000) run_ms(50);
    double reach_s = (g_ms - t0) / 1000.0;
    check(control_get()->boost_reached, "boost target reached after %.1f s (tip %.0f C)", reach_s, g_t_heater);
    run_ms(10000);
    shot("04_main_boost");
    check(control_get()->state == STATE_BOOST && control_boost_remaining_s() <= 20 &&
          control_boost_remaining_s() >= 19, "10 s later: %u s left", control_boost_remaining_s());
    run_ms(20500);
    check(control_get()->state == STATE_REGULATE && control_get()->active_setpoint_c == 300,
          "back to 300 C after 30 s at boost (%s, set %d)", st(), control_get()->active_setpoint_c);

    /* Double click during boost cancels it */
    in_double(); run_ms(200); in_double(); run_ms(200);
    check(control_get()->state == STATE_REGULATE, "double click cancels boost (%s)", st());
    run_ms(8000);

    /* Stand: 5 s delay, sleep at 150 C, heater off after 20 min */
    g_stand_raw = 0;
    run_ms(2000);
    shot("05_sleep_countdown");
    int16_t set_before_sleep = control_get()->target_c;
    check(control_stand_countdown_s() > 0 && control_stand_countdown_s() <= 4,
          "stand countdown shown (%u s)", control_stand_countdown_s());
    run_ms(3500);
    check(control_get()->state == STATE_SLEEP, "asleep after 5 s on stand (%s)", st());
    run_ms(60000);
    shot("06_sleep");
    in_rotate(3); run_ms(300); in_rotate(-2); run_ms(300);
    check(control_get()->target_c == set_before_sleep && control_get()->state == STATE_SLEEP,
          "knob on the sleep screen changes nothing (%d)", control_get()->target_c);
    check(fabs(g_t_heater - 150.0) < 8.0, "sleep holds ~150 C (%.0f)", g_t_heater);
    check(control_sleep_remaining_s() > 1100 && control_sleep_remaining_s() <= 1140,
          "heater-off countdown %lu s", (unsigned long)control_sleep_remaining_s());
    g_stand_raw = 1; run_ms(400);
    check(control_get()->state == STATE_HEAT_UP || control_get()->state == STATE_REGULATE,
          "lifting the iron wakes it (%s)", st());
    run_ms(6000);
    g_stand_raw = 0; run_ms(6000);
    control_set_sleep_off_min(1);
    run_ms(61000);
    check(control_get()->sleep_timed_out && !control_get()->heating_enabled,
          "heater off after the sleep timer (%s)", st());
    run_ms(5000);
    shot("07_after_sleep_logo");
    check(g_override && g_override_pct < 50, "after sleep: logo with dimmed backlight");
    run_ms(70000);
    check(g_lcd_on, "panel stays on showing the logo");
    g_stand_raw = 1; run_ms(400);
    check(!g_override && !control_get()->heating_enabled, "lift: full brightness, still not heating");
    shot("07b_lifted_logo");
    in_click(); run_ms(400);
    check(control_get()->heating_enabled, "click heats again (%s)", st());
    control_set_sleep_off_min(20);
    run_ms(8000);

    /* Rolling digits: 300 -> 305 C on SET; capture the roll every 20 ms */
    control_set_target_c(300); run_ms(2000);
    in_rotate(2);
    for (int f = 0; f < 12; ++f) {
        if (f == 0) ui_render();
        char name[32]; snprintf(name, sizeof(name), "roll_%02d", f);
        shot(name);
        g_ms += 20; ui_animate();
    }
    check(control_get()->target_c == 310, "knob +2 = 310 (%d)", control_get()->target_c);
    in_rotate(1); run_ms(50);
    shot("15_roll_mid");
    run_ms(500);
    /* Unsaved knob change, then boost: a click during boost does not save,
     * it turns the heater off as before. */
    run_ms(4000);
    in_double(); run_ms(300);
    check(control_get()->state == STATE_BOOST && control_target_pending(), "boost with an unsaved 315");
    in_click(); run_ms(300);
    check(!control_get()->heating_enabled && control_target_pending(),
          "click during boost: heater off, nothing saved");
    /* Double click while off no longer selects presets */
    int16_t before = control_get()->target_c;
    in_double(); run_ms(300);
    check(control_get()->target_c == before && !control_get()->heating_enabled,
          "double click while off changes nothing");

    /* Settings */
    in_long(); run_ms(300);
    shot("08_settings");
    in_click(); in_rotate(1); run_ms(300);
    shot("09_settings_edit_handle");
    in_click(); run_ms(300);
    check(profile_active_index() == TIP_C210, "handle switched to C210");
    check(profile_max_temp_c() == 300, "uncalibrated C210 capped at 300");
    for (int i = 0; i < 15; ++i) in_rotate(1);
    run_ms(300);
    in_click(); run_ms(300);
    shot("10_cal_intro");
    in_click(); run_ms(4000);
    shot("11_cal_wizard");
    check(control_get()->heating_enabled && profile_calibration_mode(), "calibration heating");
    in_long(); run_ms(300);
    check(!profile_calibration_mode() && !control_get()->heating_enabled && !ui_settings_active(),
          "hold aborts calibration and leaves settings");
    shot("12_main_c210");
    in_long(); run_ms(200); in_click(); in_rotate(-1); in_click(); run_ms(200);
    check(profile_active_index() == TIP_C245, "back to C245");
    for (int i = 0; i < 17; ++i) in_rotate(1);
    in_click(); run_ms(300);
    shot("13_about");
    in_click(); in_long(); run_ms(2000);

    /* Fault: tip pulled while heating */
    in_click(); run_ms(3000);
    g_tip_connected = false; run_ms(500);
    shot("14_fault");
    check(safety_fault() == FAULT_SENSOR_OPEN, "tip removed while heating -> TIP OPEN");
    in_click(); g_tip_connected = true; run_ms(1000);
    in_click(); run_ms(500);
    check(!safety_latched() && !control_get()->heating_enabled, "fault acknowledged, heating flag clear");
    g_tip_connected = false; run_ms(500);
    check(!safety_latched(), "unplugging while idle after a fault: no false TIP OPEN");
    g_tip_connected = true; run_ms(1000);

    printf("\n%d check(s) failed, %d settings saves\n", g_fail, g_saves);
    return g_fail ? 1 : 0;
}
