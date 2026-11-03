#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>

void settings_init(void);
/* Writes flash only when something differs from the stored record. */
bool settings_save(void);
bool settings_dirty(void);
void settings_factory_defaults(void);
bool settings_loaded_from_flash(void);
bool settings_tip_warning_enabled(void);
void settings_set_tip_warning_enabled(bool enabled);

#endif
