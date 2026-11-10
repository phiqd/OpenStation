#include "control.h"
#include "app_config.h"
#include "board.h"
#include "measure.h"
#include "power.h"
#include "profiles.h"
#include "safety.h"

#define BOOST_REACH_TIMEOUT_MS 60000UL   /* start the timer anyway after 60 s */
#define BOOST_REACHED_BAND_C   3
#define INACTIVITY_OFF_MS      1800000UL /* 30 min without any interaction */
#define SLEEP_BACKLIGHT_OFF_PERCENT 25U

static control_status_t s_control;
static uint8_t s_tip_stable;
static uint8_t s_stand_stable;
static uint8_t s_vib_stable;
static uint8_t s_vib_high_stable;
static bool s_vib_armed;
static bool s_stand_beeped;
static bool s_last_stand_active;
static int16_t s_previous_temperature_c;
static uint32_t s_previous_sample_ms;
static bool s_derivative_valid;
static uint32_t s_last_activity_ms;

static uint8_t s_encoder_step_c = 5U;
static uint16_t s_max_target_c = STATION_MAX_TARGET_C;
static uint8_t s_sleep_delay_s = STATION_SLEEP_DELAY_S;
static int16_t s_sleep_temp_c = STATION_SLEEP_TEMP_C;
static uint8_t s_sleep_off_min = STATION_SLEEP_OFF_MIN;
static uint8_t s_boost_delta_c = 50U;
static uint8_t s_boost_time_s = 30U;
static uint16_t s_presets_c[3] = {300U, 350U, 400U};
/* Last set temperature per handle, restored at power-up (0 = tip default). */
static int16_t s_tip_target[TIP_COUNT];
static uint8_t s_preset_index;
static bool s_preset_selected;

#ifndef PID_LEGACY
static void pid_reset_learning(void);
#endif

static bool active_state(station_state_t s)
{
    return s == STATE_HEAT_UP || s == STATE_REGULATE || s == STATE_BOOST || s == STATE_SLEEP;
}

static int16_t limit_target(int32_t target)
{
    int16_t ceiling = profile_max_temp_c();
    if (ceiling > (int16_t)s_max_target_c && !profile_calibration_mode())
        ceiling = (int16_t)s_max_target_c;
    if (target > ceiling) target = ceiling;
    if (target < 100) target = 100;
    return (int16_t)target;
}

static int16_t boost_target_c(void)
{
    return limit_target((int32_t)s_control.target_c + s_boost_delta_c);
}

static int16_t sleep_setpoint_c(void)
{
    int16_t t = s_sleep_temp_c;
    if (t > s_control.target_c) t = s_control.target_c;
    return t;
}

static void clear_sleep_session(bool beep)
{
    bool was_sleeping = s_control.state == STATE_SLEEP || s_control.sleep_timed_out;
    s_control.sleep_since_ms = 0U;
    s_control.sleep_timed_out = false;
    s_control.stand_since_ms = 0U;
    board_backlight_override(false, 0U);
    if (beep && was_sleeping) board_buzzer_pattern(2U);
}

static void enter_state(station_state_t next)
{
    if (s_control.state == next) return;
    bool was_active = active_state(s_control.state);
    bool will_be_active = active_state(next);
    s_control.state = next;
    if (next != STATE_BOOST) s_control.boost_reached = false;
    s_control.state_since_ms = board_millis();
    if (next == STATE_SLEEP) {
        s_control.sleep_since_ms = s_control.state_since_ms;
        s_control.sleep_timed_out = false;
        if (!s_stand_beeped) board_buzzer_pattern(2U);
    }
    if (!will_be_active) {
        /* Heating is only ever enabled in an active state; this also clears
         * the stale flag a fault used to leave behind. */
        s_control.heating_enabled = false;
        s_control.integral_q8 = 0;
        s_control.requested_permille = 0;
        s_derivative_valid = false;
        power_request(0);
    } else if (!was_active) {
        s_derivative_valid = false;
    }
}

void control_init(void)
{
    s_control = (control_status_t){0};
    s_tip_stable = 0;
    s_stand_stable = 0;
    s_vib_stable = 0;
    s_vib_high_stable = 0;
    s_vib_armed = false;
    s_stand_beeped = false;
    s_last_stand_active = false;
    s_derivative_valid = false;
    s_preset_index = 0U;
    s_preset_selected = false;
    s_control.state = STATE_BOOT_SAFE;
    int16_t remembered = s_tip_target[profile_active_index()];
    s_control.target_c = limit_target(remembered >= 150 ? remembered :
                                      profile_active()->default_temp_c);
    s_control.active_setpoint_c = s_control.target_c;
    s_control.state_since_ms = board_millis();
    s_last_activity_ms = board_millis();
#ifndef PID_LEGACY
    pid_reset_learning();
#endif
    board_backlight_override(false, 0U);
    power_request(0);
}

void control_reconfigure(void)
{
    power_force_off();
    control_init();
}

void control_note_activity(void) { s_last_activity_ms = board_millis(); }

static void stop_heating(void)
{
    clear_sleep_session(false);
    enter_state(STATE_COOLING);
}

static void update_inputs(void)
{
    const measurement_t *m = measure_get();
    if (m->tip_present) { if (s_tip_stable < 4U) ++s_tip_stable; }
    else s_tip_stable = 0;

    if (board_stand_raw() == STATION_STAND_ACTIVE_LEVEL) { if (s_stand_stable < 4U) ++s_stand_stable; }
    else s_stand_stable = 0;
    s_control.stand_active = s_stand_stable >= 3U;
    if (s_control.stand_active != s_last_stand_active) {
        s_last_stand_active = s_control.stand_active;
        control_note_activity();
    }

    /* Stand-delay beeps: one when the countdown starts, one if cancelled. */
    if (active_state(s_control.state) && s_control.state != STATE_SLEEP &&
        s_control.stand_active) {
        if (!s_stand_beeped) { board_buzzer_pattern(2U); s_stand_beeped = true; }
    } else if (!s_control.stand_active || !active_state(s_control.state)) {
        if (s_stand_beeped && !s_control.stand_active && s_control.state != STATE_SLEEP)
            board_buzzer_pattern(2U);
        s_stand_beeped = false;
    }

    /* An unconnected PC3 is held low externally, like a triggered sensor.
     * Arm VIB only after a connected sensor has driven PC3 high. */
    if (board_vib_raw() != STATION_VIB_ACTIVE_LEVEL) {
        if (s_vib_high_stable < 3U) ++s_vib_high_stable;
        if (s_vib_high_stable >= 3U) s_vib_armed = true;
    } else s_vib_high_stable = 0U;
    if (s_vib_armed && board_vib_raw() == STATION_VIB_ACTIVE_LEVEL) {
        if (s_vib_stable < 3U) ++s_vib_stable;
    } else if (s_vib_stable > 0U) --s_vib_stable;
    bool vib = s_vib_stable >= 2U ? true : (s_vib_stable == 0U ? false : s_control.vib_active);
    if (vib != s_control.vib_active) control_note_activity();
    s_control.vib_active = vib;
}

static void run_sleep_logic(uint32_t now, bool sleep_requested)
{
    if (!active_state(s_control.state)) return;
    if (sleep_requested) {
        if (s_control.stand_since_ms == 0U) s_control.stand_since_ms = now;
        if (s_control.state != STATE_SLEEP &&
            (s_control.vib_active ||
             now - s_control.stand_since_ms >= (uint32_t)s_sleep_delay_s * 1000UL)) {
            power_request(0);
            s_control.integral_q8 = 0;
            s_control.requested_permille = 0;
            s_derivative_valid = false;
            s_control.active_setpoint_c = sleep_setpoint_c();
            enter_state(STATE_SLEEP);
        }
        if (s_control.state == STATE_SLEEP &&
            now - s_control.sleep_since_ms >= (uint32_t)s_sleep_off_min * 60000UL) {
            enter_state(STATE_COOLING);
            s_control.sleep_timed_out = true;
            board_backlight_override(true, SLEEP_BACKLIGHT_OFF_PERCENT);
            board_buzzer_pattern(2U);
        }
    } else {
        s_control.stand_since_ms = 0;
        if (s_control.state == STATE_SLEEP) {
            clear_sleep_session(true);
            s_control.active_setpoint_c = s_control.target_c;
            enter_state(STATE_HEAT_UP);
        }
    }
}

static void run_boost_logic(uint32_t now, int16_t temperature_c)
{
    if (s_control.state != STATE_BOOST) return;
    if (!s_control.boost_reached) {
        /* The boost timer counts from the moment the boost target is reached. */
        if (temperature_c >= s_control.active_setpoint_c - BOOST_REACHED_BAND_C ||
            now - s_control.boost_started_ms >= BOOST_REACH_TIMEOUT_MS) {
            s_control.boost_reached = true;
            s_control.boost_end_ms = now + (uint32_t)s_boost_time_s * 1000UL;
            board_buzzer_pattern(1U);
        }
    } else if ((int32_t)(now - s_control.boost_end_ms) >= 0) {
        s_control.active_setpoint_c = s_control.target_c;
        enter_state(STATE_REGULATE);
        board_buzzer_pattern(2U);
    }
}

#ifdef PID_LEGACY
/* The original OpenStation 0.1.0 controller, kept only so the host
 * benchmark (sim/pid_bench.c) can compare against it. Not built for the MCU. */
static void run_pid(uint32_t now)
{
    const measurement_t *m = measure_get();
    const tip_profile_t *p = profile_active();
    int32_t error = (int32_t)s_control.active_setpoint_c - m->temperature_c;
    int32_t p_term = ((int32_t)p->kp_q8 * error) >> 8;
    int32_t d_term = 0;
    uint32_t dt_ms = s_derivative_valid ? now - s_previous_sample_ms : 50U;
    if (s_derivative_valid && dt_ms >= 10U && dt_ms <= CONTROL_STALE_MS) {
        int32_t temperature_delta = (int32_t)m->temperature_c - s_previous_temperature_c;
        int64_t scaled = -((int64_t)p->kd_q8 * temperature_delta * 1000LL);
        d_term = (int32_t)(scaled / ((int64_t)256 * dt_ms));
        if (d_term > 300) d_term = 300;
        if (d_term < -300) d_term = -300;
    }
    s_previous_temperature_c = m->temperature_c;
    s_previous_sample_ms = now;
    s_derivative_valid = true;
    int32_t provisional = p_term + (s_control.integral_q8 >> 8) + d_term;
    bool limited_high = s_control.requested_permille >
                        (uint32_t)power_get()->limited_permille + 10U;
    if (dt_ms >= 10U && dt_ms <= CONTROL_STALE_MS &&
        !((error > 0 && (provisional >= 1000 || limited_high)) ||
          (error < 0 && provisional <= 0)))
        s_control.integral_q8 += (int32_t)(((int64_t)p->ki_q8 * error * dt_ms) / 1000LL);
    if (s_control.integral_q8 < 0) s_control.integral_q8 = 0;
    int32_t integral_limit_q8 = (int32_t)p->max_integral_permille << 8;
    if (s_control.integral_q8 > integral_limit_q8) s_control.integral_q8 = integral_limit_q8;
    int32_t request = p_term + (s_control.integral_q8 >> 8) + d_term;
    if (request < 0) request = 0;
    if (request > 1000) request = 1000;
    if (request > (int32_t)s_control.requested_permille + 100)
        request = s_control.requested_permille + 100;
    s_control.requested_permille = (uint16_t)request;
    power_request(s_control.requested_permille);
}
#else
/* ---- Temperature controller -------------------------------------------
 *
 * The controller works in "reference permille": 1000 = the heater's full
 * power at PID_REF_MV (24 V), the voltage the original C245 gains were tuned
 * at. The request is converted to a duty cycle with the measured supply
 * voltage (duty = u * Vref^2 / Vin^2), so the loop gain is the same from
 * 9 V to 28 V. At 24 V the P, I and D terms are exactly the original ones.
 *
 * On top of PID:
 *  - feedforward: the power needed to hold a temperature is roughly
 *    proportional to (setpoint - room). The slope is learned online whenever
 *    the tip sits stable at its setpoint, so the integral only trims errors;
 *  - the integral only acts within +/-PID_I_BAND_C of the setpoint, so it no
 *    longer winds up during heat-up (the cause of the original 10-20 C
 *    overshoot); the feedforward supplies the holding power instead;
 *  - the derivative (on measurement) is low-pass filtered against ADC noise;
 *  - anti-windup compares against the power that could actually be delivered.
 * A full-power "predictive brake" first approach was evaluated in
 * sim/pid_bench.c and rejected: the reading overshot and then dipped.
 */
#define PID_REF_MV          24000ULL
#define FF_LEARN_BAND_C     2
#define FF_LEARN_WINDOW_MS  4000UL
#define FF_K_MIN            40
#define FF_K_MAX            600
#ifndef PID_LOWV_GAIN_MAX
#define PID_LOWV_GAIN_MAX   3U
#endif
#ifndef PID_I_BAND_C
#define PID_I_BAND_C        15
#endif

static int32_t s_d_filtered;
static int32_t s_ff_k_milli;            /* u * 1000 per degree above room */
static uint8_t s_ff_profile = 0xFFU;
static uint32_t s_ff_window_start;
static int32_t s_ff_sum;
static uint16_t s_ff_count;
static uint16_t s_u_delivered;          /* reference permille actually scheduled */

static uint32_t duty_from_ref(uint32_t u, uint32_t vin_mv)
{
    if (vin_mv < 1000U) return 0U;
    uint64_t d = ((uint64_t)u * PID_REF_MV * PID_REF_MV) / ((uint64_t)vin_mv * vin_mv);
    return d > 1000U ? 1000U : (uint32_t)d;
}

static uint32_t ref_from_duty(uint32_t duty, uint32_t vin_mv)
{
    uint64_t u = ((uint64_t)duty * vin_mv * vin_mv) / (PID_REF_MV * PID_REF_MV);
    return u > 2000U ? 2000U : (uint32_t)u;
}

static int32_t feedforward(int32_t setpoint_c)
{
    int32_t rise = setpoint_c - profile_ambient_c();
    if (rise <= 0) return 0;
    int32_t ff = (s_ff_k_milli * rise) / 1000;
    return ff > 700 ? 700 : ff;
}

static void pid_reset_learning(void) { s_ff_profile = 0xFFU; }

static void ff_reset_window(uint32_t now) { s_ff_window_start = now; s_ff_sum = 0; s_ff_count = 0; }

/* Learn the holding power while the tip is steady at its setpoint. */
static void ff_learn(uint32_t now, int32_t error, int32_t request, bool saturated)
{
    if (saturated || error > FF_LEARN_BAND_C || error < -FF_LEARN_BAND_C ||
        s_control.state == STATE_HEAT_UP) { ff_reset_window(now); return; }
    s_ff_sum += request;
    ++s_ff_count;
    if (now - s_ff_window_start < FF_LEARN_WINDOW_MS || s_ff_count < 20U) return;
    int32_t rise = s_control.active_setpoint_c - profile_ambient_c();
    if (rise >= 60) {
        int32_t k_seen = (s_ff_sum / s_ff_count) * 1000 / rise;
        int32_t k_new = s_ff_k_milli + (k_seen - s_ff_k_milli) / 4;
        if (k_new < FF_K_MIN) k_new = FF_K_MIN;
        if (k_new > FF_K_MAX) k_new = FF_K_MAX;
        /* Bumpless: move what the integral was carrying into the feedforward. */
        int32_t before = feedforward(s_control.active_setpoint_c);
        s_ff_k_milli = k_new;
        int32_t after = feedforward(s_control.active_setpoint_c);
        s_control.integral_q8 -= (after - before) << 8;
    }
    ff_reset_window(now);
}

static void run_pid(uint32_t now)
{
    const measurement_t *m = measure_get();
    const tip_profile_t *p = profile_active();
    if (s_ff_profile != profile_active_index()) {
        s_ff_profile = profile_active_index();
        s_ff_k_milli = p->ff_k_milli;
    }
    int32_t error = (int32_t)s_control.active_setpoint_c - m->temperature_c;
    int32_t p_term = ((int32_t)p->kp_q8 * error) >> 8;
    /* On a low supply the heater's full power is only a fraction of the
     * 24 V reference, so the same P band would start throttling long before
     * the slower-heating tip needs it and the last degrees crawl. While the
     * tip is below the setpoint, steepen P by up to PID_LOWV_GAIN_MAX so full
     * power is held until closer in. Above the setpoint the gain is
     * unchanged, so holding behaviour and stability are as before. */
    if (error > 0 && m->vin_mv >= 1000U && m->vin_mv < PID_REF_MV) {
        uint32_t g_q8 = (uint32_t)((PID_REF_MV * PID_REF_MV * 256ULL) /
                                   ((uint64_t)m->vin_mv * m->vin_mv));
        if (g_q8 > PID_LOWV_GAIN_MAX * 256U) g_q8 = PID_LOWV_GAIN_MAX * 256U;
        p_term = (int32_t)(((int64_t)p_term * g_q8) >> 8);
    }
    uint32_t dt_ms = s_derivative_valid ? now - s_previous_sample_ms : 50U;
    bool dt_ok = dt_ms >= 10U && dt_ms <= CONTROL_STALE_MS;
    if (!s_derivative_valid) {
        s_d_filtered = 0;
        ff_reset_window(now);
    } else if (dt_ok) {
        int32_t temperature_delta = (int32_t)m->temperature_c - s_previous_temperature_c;
        int64_t scaled = -((int64_t)p->kd_q8 * temperature_delta * 1000LL);
        int32_t d_raw = (int32_t)(scaled / ((int64_t)256 * dt_ms));
        /* First-order low-pass, ~75 ms time constant at the 20 Hz loop. */
        s_d_filtered += ((d_raw - s_d_filtered) * 2) / 5;
    }
    int32_t d_term = s_d_filtered;
    if (d_term > 300) d_term = 300;
    if (d_term < -300) d_term = -300;
    s_previous_temperature_c = m->temperature_c;
    s_previous_sample_ms = now;
    s_derivative_valid = true;

    int32_t ff = feedforward(s_control.active_setpoint_c);

    int32_t provisional = ff + p_term + (s_control.integral_q8 >> 8) + d_term;
    bool limited_high = s_control.requested_permille > (uint32_t)s_u_delivered + 10U;
    /* Integrate only near the setpoint: far away, P and the feedforward do
     * the work, and an integral wound up during heat-up only causes
     * overshoot. The feedforward removes the old reason for integrating
     * everywhere (a steady-state error the integral could not reach). */
    bool in_band = error <= PID_I_BAND_C && error >= -PID_I_BAND_C;
    if (dt_ok && in_band && !((error > 0 && (provisional >= 1000 || limited_high)) ||
                              (error < 0 && provisional <= 0)))
        s_control.integral_q8 += (int32_t)(((int64_t)p->ki_q8 * error * dt_ms) / 1000LL);
    int32_t integral_max_q8 = (int32_t)p->max_integral_permille << 8;
    int32_t integral_min_q8 = -(ff << 8);
    if (s_control.integral_q8 > integral_max_q8) s_control.integral_q8 = integral_max_q8;
    if (s_control.integral_q8 < integral_min_q8) s_control.integral_q8 = integral_min_q8;

    int32_t request = ff + p_term + (s_control.integral_q8 >> 8) + d_term;
    if (request < 0) request = 0;
    if (request > 1000) request = 1000;
    if (request > (int32_t)s_control.requested_permille + 100)
        request = s_control.requested_permille + 100;
    s_control.requested_permille = (uint16_t)request;
    power_request((uint16_t)duty_from_ref((uint32_t)request, m->vin_mv));
    s_u_delivered = (uint16_t)ref_from_duty(power_get()->limited_permille, m->vin_mv);
    ff_learn(now, error, request, limited_high || request >= 1000);
}

#endif /* PID_LEGACY */

void control_run_cycle(void)
{
    const measurement_t *m = measure_get();
    uint32_t now = board_millis();

    if (safety_latched()) {
        clear_sleep_session(false);
        enter_state(STATE_FAULT_LATCHED);
        power_request(0);
        return;
    }

    update_inputs();
    bool sleep_requested = s_control.stand_active || s_control.vib_active;
    if (s_control.sleep_timed_out && !sleep_requested) clear_sleep_session(true);

    if (!m->tip_present) {
        if (s_control.sleep_timed_out) clear_sleep_session(false);
        if (active_state(s_control.state)) safety_trip(FAULT_SENSOR_OPEN, (uint8_t)s_control.state);
        else enter_state(STATE_NO_TIP);
        power_request(0);
        return;
    }
    if (s_control.state == STATE_BOOT_SAFE || s_control.state == STATE_NO_TIP) {
        if (s_tip_stable >= 3U) enter_state(STATE_TIP_IDENTIFY);
        return;
    }
    if (s_control.state == STATE_TIP_IDENTIFY) {
        if (now - s_control.state_since_ms >= 100U) enter_state(STATE_IDLE);
        return;
    }

    run_sleep_logic(now, sleep_requested);
    run_boost_logic(now, m->temperature_c);

    if (active_state(s_control.state) && s_control.state != STATE_SLEEP &&
        now - s_last_activity_ms >= INACTIVITY_OFF_MS) {
        stop_heating();
        board_buzzer_pattern(2U);
    }

    /* Validate the new quiet-window reading before it can increase duty. */
    safety_supervise((uint8_t)s_control.state, active_state(s_control.state));
    if (safety_latched()) {
        enter_state(STATE_FAULT_LATCHED);
        power_request(0);
        return;
    }

    if (active_state(s_control.state)) {
        run_pid(now);
        if (s_control.state == STATE_HEAT_UP &&
            m->temperature_c >= s_control.active_setpoint_c - 5) {
            enter_state(STATE_REGULATE);
            if (board_setpoint_beep_get()) board_buzzer_pattern(2);
        }
    } else {
        power_request(0);
        if (s_control.state == STATE_COOLING && m->temperature_c < 60 &&
            !s_control.sleep_timed_out) enter_state(STATE_IDLE);
    }
    safety_supervise((uint8_t)s_control.state, active_state(s_control.state));
}

void control_rotate(int8_t detents)
{
    if (detents == 0) return;
    control_note_activity();
    int32_t target = s_control.target_c + (int32_t)detents * s_encoder_step_c;
    if (target < 150) target = 150;
    int16_t limited = limit_target(target);
    if (limited != s_control.target_c) s_preset_selected = false;
    s_control.target_c = limited;
    if (s_control.state == STATE_BOOST) enter_state(STATE_REGULATE);
    if (s_control.state != STATE_SLEEP) s_control.active_setpoint_c = limited;
}

void control_set_target_c(int16_t target_c)
{
    s_control.target_c = limit_target(target_c);
    if (s_control.state == STATE_BOOST) s_control.active_setpoint_c = boost_target_c();
    else if (s_control.state == STATE_SLEEP) s_control.active_setpoint_c = sleep_setpoint_c();
    else s_control.active_setpoint_c = s_control.target_c;
    control_note_activity();
    s_preset_selected = false;
}

void control_select_profile(uint8_t index)
{
    if (index >= profile_count()) return;
    power_force_off();
    profile_select(index);
    clear_sleep_session(false);
    measure_profile_changed();
    s_derivative_valid = false;
    int16_t remembered = s_tip_target[index];
    s_control.target_c = limit_target(remembered >= 150 ? remembered :
                                      profile_active()->default_temp_c);
    s_control.active_setpoint_c = s_control.target_c;
    s_control.integral_q8 = 0;
    enter_state(measure_get()->tip_present ? STATE_TIP_IDENTIFY : STATE_NO_TIP);
    control_note_activity();
    s_preset_selected = false;
}

void control_short_press(void)
{
    control_note_activity();
    if (safety_latched()) { if (safety_acknowledge()) enter_state(STATE_IDLE); return; }
    if (active_state(s_control.state)) {
        stop_heating();
        return;
    }
    const measurement_t *m = measure_get();
    if ((s_control.state != STATE_IDLE && s_control.state != STATE_COOLING) ||
        !m->tip_present || !m->quiet_sample_valid) return;
    if (!m->vin_valid) {
        safety_trip(FAULT_SUPPLY_RANGE, (uint8_t)s_control.state);
        return;
    }
#if STATION_HEATER_OUTPUT_ENABLE
    clear_sleep_session(false);
    s_control.target_c = limit_target(s_control.target_c);
    if (s_control.vib_active ||
        (s_vib_armed && board_vib_raw() == STATION_VIB_ACTIVE_LEVEL)) {
        s_vib_stable = 2U;
        s_control.vib_active = true;
        s_control.active_setpoint_c = sleep_setpoint_c();
        s_control.stand_since_ms = board_millis();
        enter_state(STATE_SLEEP);
    } else {
        s_control.active_setpoint_c = s_control.target_c;
        enter_state(STATE_HEAT_UP);
    }
    s_control.heating_enabled = true;
#else
    board_buzzer_pattern(1);
#endif
}

void control_long_press(void)
{
    control_note_activity();
    if (active_state(s_control.state)) stop_heating();
    power_force_off();
}

void control_double_press(void)
{
    control_note_activity();
    if (s_control.state == STATE_HEAT_UP || s_control.state == STATE_REGULATE) {
        if (s_boost_delta_c == 0U) return;
        int16_t boost = boost_target_c();
        if (boost <= s_control.target_c) return;   /* already at the limit */
        s_control.active_setpoint_c = boost;
        s_control.boost_started_ms = board_millis();
        enter_state(STATE_BOOST);
        s_control.boost_reached = false;
    } else if (s_control.state == STATE_BOOST) {
        s_control.active_setpoint_c = s_control.target_c;
        enter_state(STATE_REGULATE);
    }
}

uint16_t control_boost_remaining_s(void)
{
    if (s_control.state != STATE_BOOST) return 0U;
    if (!s_control.boost_reached) return s_boost_time_s;
    int32_t left = (int32_t)(s_control.boost_end_ms - board_millis());
    return left <= 0 ? 0U : (uint16_t)((left + 999) / 1000);
}

uint16_t control_stand_countdown_s(void)
{
    if (!active_state(s_control.state) || s_control.state == STATE_SLEEP ||
        s_control.stand_since_ms == 0U) return 0U;
    uint32_t elapsed = board_millis() - s_control.stand_since_ms;
    uint32_t delay = (uint32_t)s_sleep_delay_s * 1000UL;
    return elapsed >= delay ? 0U : (uint16_t)((delay - elapsed + 999UL) / 1000UL);
}

uint32_t control_sleep_remaining_s(void)
{
    if (s_control.state != STATE_SLEEP) return 0U;
    uint32_t elapsed = board_millis() - s_control.sleep_since_ms;
    uint32_t total = (uint32_t)s_sleep_off_min * 60000UL;
    return elapsed >= total ? 0U : (total - elapsed + 999UL) / 1000UL;
}

uint16_t control_preset_c(uint8_t index) { return index < 3U ? s_presets_c[index] : 300U; }
int8_t control_selected_preset(void) { return s_preset_selected ? (int8_t)s_preset_index : -1; }
void control_set_preset_c(uint8_t index, uint16_t temperature_c)
{
    if (index >= 3U) return;
    if (temperature_c < 150U) temperature_c = 150U;
    if (temperature_c > STATION_MAX_TARGET_C) temperature_c = STATION_MAX_TARGET_C;
    s_presets_c[index] = temperature_c;
    if (s_preset_selected && index == s_preset_index &&
        s_control.target_c != (int16_t)temperature_c) s_preset_selected = false;
}

const control_status_t *control_get(void) { return &s_control; }

/* Saved set temperature for a handle (0 = use the tip default). Only an
 * explicit commit changes it; knob turns alone are not saved. */
int16_t control_tip_target(uint8_t tip)
{
    return tip < TIP_COUNT ? s_tip_target[tip] : 0;
}

static int16_t saved_target_c(void)
{
    int16_t t = s_tip_target[profile_active_index()];
    return limit_target(t >= 150 ? t : profile_active()->default_temp_c);
}

bool control_target_pending(void) { return s_control.target_c != saved_target_c(); }

void control_commit_target(void) { s_tip_target[profile_active_index()] = s_control.target_c; }
void control_set_tip_target(uint8_t tip, int16_t target_c)
{
    if (tip >= TIP_COUNT) return;
    s_tip_target[tip] = (target_c >= 150 && target_c <= STATION_MAX_TARGET_C) ? target_c : 0;
}
uint8_t control_encoder_step_c(void) { return s_encoder_step_c; }
void control_set_encoder_step_c(uint8_t value)
{
    s_encoder_step_c = value <= 1U ? 1U : (value <= 5U ? 5U : 10U);
}
uint16_t control_max_target_c(void) { return s_max_target_c; }
void control_set_max_target_c(uint16_t value)
{
    if (value < 150U) value = 150U;
    if (value > STATION_MAX_TARGET_C) value = STATION_MAX_TARGET_C;
    s_max_target_c = value;
    s_control.target_c = limit_target(s_control.target_c);
    if (s_control.active_setpoint_c > (int16_t)value) s_control.active_setpoint_c = (int16_t)value;
}
uint8_t control_sleep_delay_s(void) { return s_sleep_delay_s; }
void control_set_sleep_delay_s(uint8_t value) { s_sleep_delay_s = value > 60U ? 60U : value; }
int16_t control_sleep_temp_c(void) { return s_sleep_temp_c; }
void control_set_sleep_temp_c(int16_t value)
{
    if (value < 100) value = 100;
    if (value > 250) value = 250;
    s_sleep_temp_c = value;
    if (s_control.state == STATE_SLEEP) s_control.active_setpoint_c = sleep_setpoint_c();
}
uint8_t control_sleep_off_min(void) { return s_sleep_off_min; }
void control_set_sleep_off_min(uint8_t value)
{
    if (value < 1U) value = 1U;
    if (value > 60U) value = 60U;
    s_sleep_off_min = value;
}
uint8_t control_boost_delta_c(void) { return s_boost_delta_c; }
void control_set_boost_delta_c(uint8_t value)
{
    s_boost_delta_c = value > 100U ? 100U : (uint8_t)((value / 5U) * 5U);
    if (s_control.state == STATE_BOOST) {
        if (s_boost_delta_c == 0U) {
            s_control.active_setpoint_c = s_control.target_c;
            enter_state(STATE_REGULATE);
        } else s_control.active_setpoint_c = boost_target_c();
    }
}
uint8_t control_boost_time_s(void) { return s_boost_time_s; }
void control_set_boost_time_s(uint8_t value)
{
    if (value < 10U) value = 10U;
    if (value > 120U) value = 120U;
    s_boost_time_s = (uint8_t)((value / 5U) * 5U);
}

const char *control_state_text(station_state_t s)
{
    static const char *const names[] = {"BOOT", "NO TIP", "CHECK", "OFF", "HEAT",
                                        "READY", "BOOST", "SLEEP", "OFF", "FAULT"};
    return s <= STATE_FAULT_LATCHED ? names[s] : "?";
}
