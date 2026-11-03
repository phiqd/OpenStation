#ifndef CONTROL_H
#define CONTROL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    STATE_BOOT_SAFE = 0, STATE_NO_TIP, STATE_TIP_IDENTIFY, STATE_IDLE,
    STATE_HEAT_UP, STATE_REGULATE, STATE_BOOST, STATE_SLEEP,
    STATE_COOLING, STATE_FAULT_LATCHED
} station_state_t;

typedef struct {
    station_state_t state;
    int16_t target_c, active_setpoint_c;
    int32_t integral_q8;
    uint16_t requested_permille;
    uint32_t state_since_ms, stand_since_ms, sleep_since_ms;
    uint32_t boost_started_ms, boost_end_ms;
    bool boost_reached;
    bool stand_active, vib_active, heating_enabled, sleep_timed_out;
} control_status_t;

void control_init(void);
void control_reconfigure(void);
void control_run_cycle(void);
void control_rotate(int8_t detents);
void control_select_profile(uint8_t index);
void control_short_press(void);
void control_long_press(void);
void control_double_press(void);
void control_note_activity(void);
void control_set_target_c(int16_t target_c);
const control_status_t *control_get(void);
/* Set temperature saved per handle (flash; restored at boot). A knob change
 * is "pending" until control_commit_target() (a click on the main screen). */
int16_t control_tip_target(uint8_t tip);
void control_set_tip_target(uint8_t tip, int16_t target_c);
bool control_target_pending(void);
void control_commit_target(void);
const char *control_state_text(station_state_t state);

/* Seconds left on the boost timer; while still heating up to the boost
 * target this is the full configured time. */
uint16_t control_boost_remaining_s(void);
/* Seconds until the stand delay puts the iron to sleep (0 if not pending). */
uint16_t control_stand_countdown_s(void);
/* Seconds until the heater turns off after sleeping (0 if not sleeping). */
uint32_t control_sleep_remaining_s(void);

uint16_t control_preset_c(uint8_t index);
int8_t control_selected_preset(void);
void control_set_preset_c(uint8_t index, uint16_t temperature_c);
uint8_t control_encoder_step_c(void);
void control_set_encoder_step_c(uint8_t value);
uint16_t control_max_target_c(void);
void control_set_max_target_c(uint16_t value);
uint8_t control_sleep_delay_s(void);
void control_set_sleep_delay_s(uint8_t value);
int16_t control_sleep_temp_c(void);
void control_set_sleep_temp_c(int16_t value);
uint8_t control_sleep_off_min(void);
void control_set_sleep_off_min(uint8_t value);
uint8_t control_boost_delta_c(void);
void control_set_boost_delta_c(uint8_t value);
uint8_t control_boost_time_s(void);
void control_set_boost_time_s(uint8_t value);

#endif
