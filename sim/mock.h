/* Shared hardware mocks and tip thermal model for the host simulator and
 * the PID benchmark. */
#ifndef SIM_MOCK_H
#define SIM_MOCK_H

#include <stdbool.h>
#include <stdint.h>

extern uint32_t g_ms;
extern uint8_t g_stand_raw, g_vib_raw;
extern bool g_tip_connected;
extern uint32_t g_vin_mv;      /* supply voltage */
extern double g_load_w;        /* heat drawn from the tip end by a joint */
extern double g_room_c;        /* real room temperature */
extern double g_noise_c;       /* ADC noise (1 sigma, degrees) */
extern double g_t_heater;      /* heater/thermocouple node (what the station reads) */
extern double g_t_tip;         /* working end of the tip */
extern bool g_override; extern uint8_t g_override_pct;
extern int g_beeps;

void mock_reset(double temperature_c);
/* One 50 ms heater cycle: integrates the model with the scheduled pulse,
 * then publishes a new quiet-window measurement. */
void mock_cycle(void);

#endif
