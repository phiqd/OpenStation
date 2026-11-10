/* PID benchmark: the same scenario with the original controller
 * (-DPID_LEGACY) and the new one, at several supply voltages, on the C245
 * two-node model from mock.c.
 *
 * Scenario: heat 25 -> 350 C, hold, a 40 W solder joint for 2 s, then
 * 350 -> 400 -> 350 setpoint steps. Run with sim/pid_bench.sh. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "control.h"
#include "measure.h"
#include "mock.h"
#include "power.h"
#include "profiles.h"
#include "safety.h"

#ifdef PID_LEGACY
#define NAME "old"
#else
#define NAME "new"
#endif

typedef struct {
    double t_reach, tip_reach, tip_over, overshoot, settle, ripple, droop_tip, droop_meas, recover,
           step_over, step_under, hold_err, avg_power;
} result_t;

static FILE *s_trace;
static double s_t0;

static void cycle(void)
{
    mock_cycle();
    control_run_cycle();
    const control_status_t *c = control_get();
    safety_supervise((uint8_t)c->state, c->heating_enabled);
    if (s_trace)
        fprintf(s_trace, "%.2f,%d,%.2f,%.2f,%d,%u\n", g_ms / 1000.0 - s_t0, measure_get()->temperature_c,
                g_t_heater, g_t_tip, c->active_setpoint_c, power_get()->limited_permille);
}

static result_t run(uint32_t vin_mv, uint8_t tip, const char *trace_path)
{
    result_t r = {0};
    g_ms = 0; g_vin_mv = vin_mv; g_load_w = 0; g_noise_c = 0.3;
    profiles_factory_reset();
    profile_select(tip);
    profile_set_calibrated(tip, true);
    control_set_max_target_c(450);
    mock_reset(25.0);
    mock_cycle();
    safety_init();
    control_init();
    for (int i = 0; i < 20; ++i) cycle();
    control_set_target_c(350);
    s_trace = trace_path ? fopen(trace_path, "w") : NULL;
    if (s_trace) fprintf(s_trace, "t,measured,heater,tip,setpoint,duty\n");
    s_t0 = g_ms / 1000.0;
    control_short_press();

    double t = 0, maxm = 0, settle_at = -1, max_tip = 0;
    r.t_reach = -1; r.tip_reach = -1;
    for (; t < 25.0; t += 0.05) {         /* heat-up and settle */
        cycle();
        int16_t m = measure_get()->temperature_c;
        if (r.t_reach < 0 && m >= 345) r.t_reach = t;
        if (r.tip_reach < 0 && g_t_tip >= 340) r.tip_reach = t;
        if (g_t_tip > max_tip) max_tip = g_t_tip;
        if (m > maxm) maxm = m;
        if (fabs(m - 350.0) > 2.0) settle_at = -1;
        else if (settle_at < 0) settle_at = t;
    }
    r.overshoot = maxm - 350.0;
    r.tip_over = max_tip - g_t_tip;   /* tip peak above its final value */
    r.settle = settle_at;
    double lo = 1e9, hi = -1e9, sum_err = 0, sum_p = 0; int n = 0;
    for (; t < 35.0; t += 0.05) {         /* steady hold */
        cycle();
        double m = measure_get()->temperature_c;
        if (m < lo) lo = m;
        if (m > hi) hi = m;
        sum_err += fabs(measure_get()->temperature_c - 350.0);
        sum_p += power_get()->limited_permille;
        ++n;
    }
    r.ripple = hi - lo;
    r.hold_err = sum_err / n;
    double v = vin_mv / 1000.0;
    r.avg_power = (sum_p / n / 1000.0) * v * v / (profile_active()->nominal_resistance_mohm / 1000.0);
    double tip_before = g_t_tip, min_tip = 1e9, min_meas = 1e9;
    g_load_w = 40.0;                      /* a large joint for 2 s */
    for (double e = 0; e < 2.0; e += 0.05, t += 0.05) {
        cycle();
        if (g_t_tip < min_tip) min_tip = g_t_tip;
        if (measure_get()->temperature_c < min_meas) min_meas = measure_get()->temperature_c;
    }
    g_load_w = 0.0;
    /* Recovered = the measured temperature is back within 2 C of the
     * setpoint and the tip end within 2 C of where it was before the joint. */
    double ok_since = -1; r.recover = -1;
    for (double e = 0; e < 10.0; e += 0.05, t += 0.05) {
        cycle();
        if (g_t_tip < min_tip) min_tip = g_t_tip;
        bool ok = fabs(measure_get()->temperature_c - 350.0) <= 2.0 && fabs(g_t_tip - tip_before) <= 2.0;
        if (ok) { if (ok_since < 0) ok_since = e; }
        else ok_since = -1;
    }
    r.recover = ok_since;
    r.droop_tip = tip_before - min_tip;
    r.droop_meas = 350.0 - min_meas;
    control_set_target_c(400);
    double mx = 0;
    for (double e = 0; e < 12.0; e += 0.05, t += 0.05) { cycle(); if (measure_get()->temperature_c > mx) mx = measure_get()->temperature_c; }
    r.step_over = mx - 400.0;
    control_set_target_c(350);
    double mn = 1e9;
    for (double e = 0; e < 12.0; e += 0.05, t += 0.05) { cycle(); if (measure_get()->temperature_c < mn) mn = measure_get()->temperature_c; }
    r.step_under = 350.0 - mn;
    if (s_trace) fclose(s_trace);
    s_trace = NULL;
    return r;
}

int main(void)
{
    const uint32_t volts[] = {9000, 12000, 15000, 19000, 24000, 28000};
    printf("pid,tip,vin,reach_s,overshoot_c,tip340_s,tip_peak_c,settle_s,ripple_c,hold_err_c,hold_w,"
           "joint_droop_tip_c,joint_droop_meas_c,recover_s,step400_over_c,step350_under_c\n");
    for (uint8_t tip = 0; tip < TIP_COUNT; ++tip) {
        for (unsigned i = 0; i < sizeof(volts) / sizeof(volts[0]); ++i) {
            char path[96];
            snprintf(path, sizeof(path), "sim/out/trace_%s_%s_%u.csv", NAME, tip == TIP_C245 ? "c245" : "c210",
                     (unsigned)(volts[i] / 1000));
            result_t r = run(volts[i], tip, path);
            printf("%s,%s,%u,%.2f,%.1f,%.2f,%.1f,%.2f,%.1f,%.2f,%.1f,%.1f,%.1f,%.2f,%.1f,%.1f\n", NAME,
                   tip == TIP_C245 ? "C245" : "C210", (unsigned)(volts[i] / 1000), r.t_reach, r.overshoot, r.tip_reach, r.tip_over,
                   r.settle, r.ripple, r.hold_err, r.avg_power, r.droop_tip, r.droop_meas, r.recover,
                   r.step_over, r.step_under);
        }
    }
    return 0;
}
