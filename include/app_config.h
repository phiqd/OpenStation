#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* PB7 high drives Q1, which pulls the high-side P-channel MOSFET gate low. */
#define STATION_HEATER_OUTPUT_ENABLE 1

/* Release version and separate hardware-debug build counter. */
#define STATION_FW_NAME "OpenStation C245/C210"
#define STATION_FW_VERSION "1.6.5"
#define STATION_FW_BUILD   1U
#define STATION_ADC_VREF_MV 3300UL
#define STATION_ADC_FULL_SCALE 4095UL
/* VIN divider ratio x10 (16.3), kept integer to avoid soft-float code. */
#define STATION_VIN_DIVIDER_X10 163UL
/* Original PA6 current-sense transfer coefficient. */
#define CURRENT_SENSE_UV_PER_MA 340UL
#define STATION_MAX_TARGET_C 500
#define STATION_OVERTEMP_C 515
#define STATION_STAND_ACTIVE_LEVEL 0U
#define STATION_VIB_ACTIVE_LEVEL 0U
#define STATION_SLEEP_DELAY_S 5U
#define STATION_SLEEP_TEMP_C 150
#define STATION_SLEEP_OFF_MIN 20U

/* TIM1 uses a 100 kHz base and a 50 ms heater cycle. Temperature is sampled
 * a fixed 5 ms after the actual heater pulse ends, rather than at a fixed
 * point near the end of the cycle. */
#define HEATER_TIMER_HZ 100000UL
#define HEATER_PERIOD_TICKS 5000U
#define HEATER_CURRENT_SAMPLE_TICKS 100U
#define HEATER_SENSOR_SETTLE_TICKS 500U
/* Keep at least 5 ms for thermocouple settling and 1 ms for ADC conversion. */
#define HEATER_MAX_ON_TICKS 4400U
#if (HEATER_MAX_ON_TICKS + HEATER_SENSOR_SETTLE_TICKS) >= HEATER_PERIOD_TICKS
#error "No ADC sampling window remains after heater settling"
#endif
#define TEMP_ADC_BURST_SAMPLES 19U

/* The power MOSFET is deliberately switched slowly to limit gate-transition
 * losses. This configuration is 20 Hz and must never be raised above 100 Hz. */
#define HEATER_MAX_SWITCH_HZ 100UL
#if (HEATER_TIMER_HZ / HEATER_PERIOD_TICKS) > HEATER_MAX_SWITCH_HZ
#error "Heater switching frequency exceeds the 100 Hz hardware limit"
#endif

#define BUZZER_CARRIER_ISR_HZ 8000UL
#define BUZZER_TONE_HZ        (BUZZER_CARRIER_ISR_HZ / 2UL)
#define BUZZER_SHORT_MS       25U
#define BUZZER_LONG_MS        400U

#define UI_REFRESH_MS 100U
#define CONTROL_STALE_MS 200U
#define TIP_OPEN_RAW 3900U
#define TIP_PRESENT_RAW 3800U
/* Accepted supply window for both handles. Duty is additionally limited by
 * each tip's rated voltage and power, so 28 V never exceeds those limits. */
#define VIN_MIN_MV 9000U
#define VIN_MAX_MV 28000U

#endif
