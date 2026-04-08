#if !defined(GUI_H)
#define GUI_H 1

#include <stdbool.h>

#include "system.h"

void gui_begin(u16 x, u16 y);
bool gui_button(const char *text);
bool gui_toggle(const char *label, bool *value);
bool gui_bits16(const char *label, u16 *value);
void gui_end();

#endif
