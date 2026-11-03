#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include "st7735.h"

void ui_init(ST7735_Object_t *lcd);
void ui_render(void);
/* Call often from the main loop: advances rolling-digit animations. */
void ui_animate(void);

/* Settings menu (long press opens it; long press inside saves and closes). */
bool ui_settings_active(void);
void ui_open_settings(void);
void ui_close_settings(void);
void ui_settings_rotate(int8_t detents);
void ui_settings_press(void);

/* Fault popup or boot handle check; a click dismisses it. */
bool ui_popup_active(void);
void ui_popup_dismiss(void);

/* Any knob or button input; wakes a darkened panel. */
void ui_note_input(void);
/* True while the main (temperature) screen is the one on the panel. */
bool ui_main_screen_active(void);
/* Knob turn when no popup or menu is open: changes the set temperature only
 * on the main screen. */
void ui_main_rotate(int8_t detents);
/* Short click when no popup or menu is open (save set temp / heat on-off). */
void ui_main_press(void);

bool ui_display_flipped(void);
void ui_set_display_flipped(bool flipped);

#endif
