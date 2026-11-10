/* Hardware mocks plus a two-node tip model.
 *
 * Node 1 is the heater with the thermocouple (what the station measures);
 * node 2 is the working end of the tip, where solder joints draw heat. The
 * C245 numbers give ~12 W to hold 350 C and ~3 s heat-up at 24 V, in line
 * with JBC C245 behaviour; C210 is lighter and faster. They are a model, not
 * a measurement of this board. */
#include "mock.h"
#include <math.h>
#include <stdlib.h>

#include "app_config.h"
#include "board.h"
#include "encoder.h"
#include "measure.h"
#include "power.h"
#include "profiles.h"
#include "safety.h"

static GPIO_TypeDef s_gpiob;
GPIO_TypeDef *GPIOB = &s_gpiob;
uint32_t g_ms;
uint8_t g_stand_raw = 1, g_vib_raw = 0;
bool g_tip_connected = true;
uint32_t g_vin_mv = 24000;
static int8_t g_vin_trim;
double g_load_w, g_room_c = 25.0, g_noise_c = 0.0;
double g_t_heater = 25.0, g_t_tip = 25.0;
bool g_override; uint8_t g_override_pct;
int g_beeps;

static uint8_t g_backlight = 80;
static bool g_buzzer = true, g_ready_beep = true, g_enc_rev;

uint32_t board_millis(void) { return g_ms; }
void board_backlight_set(uint8_t p) { g_backlight = p > 100 ? 100 : p; }
uint8_t board_backlight_get(void) { return g_backlight; }
void board_backlight_override(bool en, uint8_t p) { g_override = en; g_override_pct = p; }
void board_buzzer_pattern(uint8_t p) { if (p) ++g_beeps; }
void board_buzzer_enable(bool e) { g_buzzer = e; }
bool board_buzzer_enabled(void) { return g_buzzer; }
void board_setpoint_beep_enable(bool e) { g_ready_beep = e; }
bool board_setpoint_beep_get(void) { return g_ready_beep; }
uint8_t board_stand_raw(void) { return g_stand_raw; }
uint8_t board_vib_raw(void) { return g_vib_raw; }
bool input_encoder_reversed(void) { return g_enc_rev; }
void input_set_encoder_reversed(bool r) { g_enc_rev = r; }

static measurement_t g_m;
static power_status_t g_p;

const measurement_t *measure_get(void) { return &g_m; }
void measure_profile_changed(void) { g_m.quiet_sample_valid = true; }
void measure_requalify_quiet_sample(void) {}
int8_t measure_vin_trim(void) { return g_vin_trim; }
void measure_set_vin_trim(int16_t t) { g_vin_trim = (int8_t)(t < -100 ? -100 : (t > 100 ? 100 : t)); }
const power_status_t *power_get(void) { return &g_p; }
void power_force_off(void) { g_p.requested_permille = g_p.limited_permille = g_p.pulse_ticks = 0; }
void power_emergency_stop_isr(void) { power_force_off(); }

/* Same limits as src/power.c. */
void power_request(uint16_t duty)
{
    const tip_profile_t *p = profile_active();
    uint32_t limited = duty > 1000U ? 1000U : duty;
    if (limited > p->max_duty_permille) limited = p->max_duty_permille;
    if (!g_m.quiet_sample_valid || !g_m.vin_valid || !g_m.tip_present || safety_latched()) limited = 0;
    uint64_t vin2 = (uint64_t)g_m.vin_mv * g_m.vin_mv;
    if (vin2 == 0) limited = 0;
    else {
        uint32_t pd = (uint32_t)(((uint64_t)p->max_power_mw * 1000ULL * p->nominal_resistance_mohm) / vin2);
        uint32_t vd = (uint32_t)(((uint64_t)p->rated_voltage_mv * p->rated_voltage_mv * 1000ULL) / vin2);
        if (limited > pd) limited = pd;
        if (limited > vd) limited = vd;
    }
    g_p.requested_permille = duty;
    g_p.pulse_ticks = (uint16_t)((limited * HEATER_PERIOD_TICKS) / 1000UL);
    if (g_p.pulse_ticks > HEATER_MAX_ON_TICKS) g_p.pulse_ticks = HEATER_MAX_ON_TICKS;
    g_p.limited_permille = (uint16_t)((uint32_t)g_p.pulse_ticks * 1000UL / HEATER_PERIOD_TICKS);
}

typedef struct { double c_heater, c_tip, g_link, loss_heater, loss_tip; } model_t;
static const model_t s_models[TIP_COUNT] = {
    [TIP_C245] = {0.55, 1.20, 1.6, 0.008, 0.030},
    [TIP_C210] = {0.15, 0.35, 0.9, 0.004, 0.016},
};

static double gauss(void)
{
    double u1 = (rand() + 1.0) / (RAND_MAX + 2.0), u2 = (rand() + 1.0) / (RAND_MAX + 2.0);
    return sqrt(-2.0 * log(u1)) * cos(6.283185307 * u2);
}

static int16_t s_hist[3];
static uint8_t s_hist_n;

void mock_reset(double t)
{
    g_t_heater = g_t_tip = t;
    s_hist_n = 0;
    srand(1234);
}

void mock_cycle(void)
{
    const model_t *md = &s_models[profile_active_index()];
    double r = profile_active()->nominal_resistance_mohm / 1000.0;
    double v = g_vin_mv / 1000.0;
    double p_on = v * v / r;
    uint16_t on_ticks = g_tip_connected ? g_p.pulse_ticks : 0;
    for (int sub = 0; sub < 50; ++sub) {          /* 1 ms steps */
        double p_in = (sub * 100 < on_ticks) ? p_on : 0.0;
        double flow = (g_t_heater - g_t_tip) * md->g_link;
        double dh = p_in - flow - (g_t_heater - g_room_c) * md->loss_heater;
        double dt = flow - (g_t_tip - g_room_c) * md->loss_tip - g_load_w;
        g_t_heater += dh * 0.001 / md->c_heater;
        g_t_tip += dt * 0.001 / md->c_tip;
    }
    g_ms += 50;

    /* Station reading: thermocouple at the heater, read against the room
     * temperature the firmware assumes, with noise, ADC steps and the
     * firmware's three-sample median. */
    const tip_profile_t *p = profile_active();
    double step_c = (3300000.0 / 4095.0) / p->temp_uv_per_c;
    double seen = g_t_heater - g_room_c + profile_ambient_c() + gauss() * g_noise_c;
    int16_t q = (int16_t)lround(round(seen / step_c) * step_c);
    s_hist[s_hist_n % 3] = q; ++s_hist_n;
    int16_t a = s_hist[0], b = s_hist_n > 1 ? s_hist[1] : q, c = s_hist_n > 2 ? s_hist[2] : q;
    int16_t med = a > b ? (b > c ? b : (a > c ? c : a)) : (a > c ? a : (b > c ? c : b));

    g_m.tip_present = g_tip_connected;
    g_m.vin_mv = (uint32_t)((uint64_t)g_vin_mv * (uint32_t)(1000 + g_vin_trim) / 1000U);
    g_m.vin_valid = profile_supply_in_range(g_vin_mv);
    g_m.quiet_sample_valid = true;
    g_m.temperature_base_c = med;
    g_m.temperature_c = profile_calibrate_temperature(med);
    g_m.temp_raw = g_tip_connected ? 1200 : 4095;
    g_m.temp_burst_span_raw = 6;
    g_m.off_current_ma = 0; g_m.off_current_sample_valid = true;
    g_m.sample_time_ms = g_ms;
    ++g_m.sample_sequence;
    g_p.average_current_ma = (uint16_t)(v / r * 1000.0 * on_ticks / HEATER_PERIOD_TICKS);
    g_p.estimated_power_mw = (uint32_t)(v * g_p.average_current_ma);
    g_p.cycle_measurement_valid = on_ticks != 0;
}
