#ifndef PROFILES_H
#define PROFILES_H

#include <stdbool.h>
#include <stdint.h>

#define PROFILE_CAL_POINT_COUNT 6U

/* Only the two handles this station is used with. */
typedef enum { TIP_C245 = 0, TIP_C210, TIP_COUNT } tip_family_t;

/* A tip that has not been calibrated against an external thermometer is
 * capped at this temperature (outside the calibration wizard). */
#define PROFILE_UNCALIBRATED_MAX_C 300

typedef struct {
    char name[8];
    uint16_t nominal_resistance_mohm;
    uint16_t temp_uv_per_c;
    int16_t temp_offset_c;
    uint16_t kp_q8, ki_q8, kd_q8;
    uint16_t max_integral_permille;
    int16_t temp_cal_c[PROFILE_CAL_POINT_COUNT];
    uint32_t max_power_mw;
    uint16_t max_duty_permille;
    uint16_t rated_voltage_mv;
    int16_t default_temp_c;
    int16_t maximum_temp_c;
    int16_t ff_k_milli;      /* initial holding-power slope (learned at run time) */
    uint8_t calibrated;
} tip_profile_t;

uint8_t profile_count(void);
const tip_profile_t *profile_get(uint8_t index);
const tip_profile_t *profile_active(void);
uint8_t profile_active_index(void);
void profile_select(uint8_t index);
const char *profile_active_name(void);

uint16_t profile_supply_min_mv(void);
uint16_t profile_supply_max_mv(void);
bool profile_supply_in_range(uint32_t vin_mv);

/* Highest target the station may regulate to with the active tip. */
int16_t profile_max_temp_c(void);

/* Room (cold-junction) temperature added to the thermocouple reading. */
int16_t profile_ambient_c(void);
void profile_set_ambient_c(int16_t value);
/* The calibration wizard may exceed the uncalibrated cap. */
void profile_set_calibration_mode(bool active);
bool profile_calibration_mode(void);

int16_t profile_calibrate_temperature(int16_t base_temperature_c);
bool profile_set_calibration_values(uint8_t index, const int16_t values[PROFILE_CAL_POINT_COUNT]);
void profile_set_calibrated(uint8_t index, bool calibrated);
void profiles_factory_reset(void);

#endif
