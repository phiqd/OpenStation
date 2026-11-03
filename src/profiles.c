#include "profiles.h"
#include "app_config.h"
#include <string.h>

/* C245: the original author's bench-calibrated values, copied unchanged
 * (3500 uV/C at the ADC, +25 C, Kp 8.0 Ki 2.0 Kd 0.5 in Q8, 24 V rated).
 *
 * C210: NOT bench calibrated. The original firmware's 8200 uV/C placeholder
 * would read about six times low and overheat the tip. AxxSolder's fits for
 * the same thermocouple types give T210 about 2.7x less signal per degree
 * than T245, so 1300 uV/C is a safer starting point. Power is limited to
 * 65 W (AxxSolder's T210 limit) and the tip stays capped at 300 C until the
 * calibration wizard has been run with an external thermometer. */
static const tip_profile_t s_factory[TIP_COUNT] = {
    [TIP_C245] = {
        .name = "C245", .nominal_resistance_mohm = 2500,
        .temp_uv_per_c = 3500, .temp_offset_c = 25,
        .kp_q8 = 2048, .ki_q8 = 512, .kd_q8 = 128, .max_integral_permille = 800,
        .temp_cal_c = {100, 200, 300, 350, 400, 450},
        .max_power_mw = 400000UL, .max_duty_permille = 1000, .rated_voltage_mv = 24000,
        .default_temp_c = 300, .maximum_temp_c = 500, .ff_k_milli = 170, .calibrated = 1
    },
    [TIP_C210] = {
        .name = "C210", .nominal_resistance_mohm = 2200,
        .temp_uv_per_c = 1300, .temp_offset_c = 25,
        .kp_q8 = 1024, .ki_q8 = 256, .kd_q8 = 64, .max_integral_permille = 600,
        .temp_cal_c = {100, 200, 300, 350, 400, 450},
        .max_power_mw = 65000UL, .max_duty_permille = 1000, .rated_voltage_mv = 16000,
        .default_temp_c = 280, .maximum_temp_c = 450, .ff_k_milli = 85, .calibrated = 0
    }
};

static tip_profile_t s_profiles[TIP_COUNT];
static uint8_t s_active = TIP_C245;
static bool s_initialized;
static bool s_calibration_mode;
static int16_t s_ambient_c = 25;   /* the original fixed +25 C offset */

static void ensure_initialized(void)
{
    if (s_initialized) return;
    memcpy(s_profiles, s_factory, sizeof(s_profiles));
    s_initialized = true;
}

uint8_t profile_count(void) { return TIP_COUNT; }
const tip_profile_t *profile_get(uint8_t i)
{
    ensure_initialized();
    return &s_profiles[i < TIP_COUNT ? i : 0];
}
const tip_profile_t *profile_active(void) { ensure_initialized(); return &s_profiles[s_active]; }
uint8_t profile_active_index(void) { return s_active; }
void profile_select(uint8_t i) { ensure_initialized(); if (i < TIP_COUNT) s_active = i; }
const char *profile_active_name(void) { return profile_active()->name; }

uint16_t profile_supply_min_mv(void) { return VIN_MIN_MV; }
uint16_t profile_supply_max_mv(void) { return VIN_MAX_MV; }
bool profile_supply_in_range(uint32_t vin_mv)
{
    return vin_mv >= VIN_MIN_MV && vin_mv <= VIN_MAX_MV;
}

int16_t profile_max_temp_c(void)
{
    const tip_profile_t *p = profile_active();
    if (!p->calibrated && !s_calibration_mode && p->maximum_temp_c > PROFILE_UNCALIBRATED_MAX_C)
        return PROFILE_UNCALIBRATED_MAX_C;
    return p->maximum_temp_c;
}
int16_t profile_ambient_c(void) { return s_ambient_c; }
void profile_set_ambient_c(int16_t v)
{
    if (v < 10) v = 10;
    if (v > 45) v = 45;
    s_ambient_c = v;
}
void profile_set_calibration_mode(bool active) { s_calibration_mode = active; }
bool profile_calibration_mode(void) { return s_calibration_mode; }

int16_t profile_calibrate_temperature(int16_t base)
{
    static const int16_t anchor[PROFILE_CAL_POINT_COUNT] = {100, 200, 300, 350, 400, 450};
    const tip_profile_t *p = profile_active();
    if (base <= anchor[0]) return (int16_t)(((int32_t)base * p->temp_cal_c[0]) / anchor[0]);
    for (uint8_t i = 1; i < PROFILE_CAL_POINT_COUNT; ++i) {
        if (base <= anchor[i]) {
            int32_t numerator = (int32_t)(base - anchor[i - 1U]) *
                                (p->temp_cal_c[i] - p->temp_cal_c[i - 1U]);
            return (int16_t)(p->temp_cal_c[i - 1U] + numerator /
                             (anchor[i] - anchor[i - 1U]));
        }
    }
    return (int16_t)(p->temp_cal_c[5] +
        ((int32_t)(base - anchor[5]) * (p->temp_cal_c[5] - p->temp_cal_c[4])) / 50);
}

bool profile_set_calibration_values(uint8_t index, const int16_t values[PROFILE_CAL_POINT_COUNT])
{
    ensure_initialized();
    if (index >= TIP_COUNT) return false;
    for (uint8_t i = 0; i < PROFILE_CAL_POINT_COUNT; ++i) {
        if (values[i] < 0 || values[i] > 550 ||
            (i != 0U && values[i] <= values[i - 1U])) return false;
    }
    memcpy(s_profiles[index].temp_cal_c, values, sizeof(s_profiles[index].temp_cal_c));
    return true;
}

void profile_set_calibrated(uint8_t index, bool calibrated)
{
    ensure_initialized();
    if (index >= TIP_COUNT) return;
    /* C245 keeps the author's characterization regardless. */
    s_profiles[index].calibrated = (calibrated || index == TIP_C245) ? 1U : 0U;
}

void profiles_factory_reset(void)
{
    memcpy(s_profiles, s_factory, sizeof(s_profiles));
    s_initialized = true;
    s_active = TIP_C245;
    s_ambient_c = 25;
}
