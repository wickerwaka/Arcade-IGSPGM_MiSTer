#if !defined(GUI_H)
#define GUI_H 1

#include <stdbool.h>

#include "system.h"

void gui_begin(u16 x, u16 y);
bool gui_button(const char *text);
bool gui_toggle(const char *label, bool *value);
bool gui_bits16(const char *label, u16 *value);
bool gui_u16(const char *label, uint16_t *value);
bool gui_u8(const char *label, u8 *value, u8 min, u8 max);
void gui_end();

#endif
