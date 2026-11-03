#include "debug.h"
#include "app_config.h"
#include "board.h"
#include "ch32x033_st7735.h"
#include "control.h"
#include "encoder.h"
#include "measure.h"
#include "power.h"
#include "safety.h"
#include "settings.h"
#include "ui.h"

static ST7735_Object_t s_lcd;

static void handle_inputs(void)
{
    int8_t rotation = input_take_rotation();
    button_event_t event = input_take_button_event();
    if (rotation == 0 && event == BUTTON_EVENT_NONE) return;
    ui_note_input();
    board_buzzer_pattern(1);

    if (rotation != 0 && !ui_popup_active()) {
        if (ui_settings_active()) ui_settings_rotate(rotation);
        else ui_main_rotate(rotation);
    }

    switch (event) {
    case BUTTON_EVENT_SHORT:
        if (ui_popup_active()) ui_popup_dismiss();
        else if (ui_settings_active()) ui_settings_press();
        else ui_main_press();
        break;
    case BUTTON_EVENT_DOUBLE:
        /* Boost while heating, preset cycling while off. */
        if (!ui_popup_active() && !ui_settings_active()) control_double_press();
        break;
    case BUTTON_EVENT_LONG:
        control_long_press();
        if (ui_settings_active()) ui_close_settings();
        else ui_open_settings();
        break;
    default:
        break;
    }
}

int main(void)
{
    uint32_t last_ui = 0;
    SystemCoreClockUpdate();
    Delay_Init();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);

    board_safe_boot();
    if (CH32_ST7735_Init(&s_lcd, ST7735_ORIENTATION_LANDSCAPE) != ST7735_OK) {
        while (1) { board_heater_gpio_off(); }
    }
    ST7735_ClearRawRAM(&s_lcd, COLOR_BLACK);

    board_init();
    input_init();
    power_init();
    settings_init();
    measure_init();
    safety_init();
    control_init();
    ui_init(&s_lcd);
    ui_render();
    board_buzzer_pattern(4);
    board_watchdog_start();

    while (1) {
        measure_process();
        handle_inputs();

        if (measure_take_new_quiet_sample()) control_run_cycle();
        if (power_take_cycle_event()) {
            const control_status_t *c = control_get();
            safety_supervise((uint8_t)c->state, c->heating_enabled);
        }

        uint32_t now = board_millis();
        if ((uint32_t)(now - last_ui) >= UI_REFRESH_MS) {
            last_ui = now;
            ui_render();
        }
        ui_animate();
        board_watchdog_feed();
    }
}
