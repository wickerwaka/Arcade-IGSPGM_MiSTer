#if !defined(TILEMAP_H)
#define TILEMAP_H 1

#include <stdint.h>

void text_color(int color);
void text_cursor(int x, int y);
uint16_t text(const char *txt);
uint16_t text_at(int x, int y, const char *txt);
uint16_t textf(const char *fmt, ...);
uint16_t textf_at(int x, int y, const char *fmt, ...);


uint16_t text_get_x();
uint16_t text_get_y();
void text_clear(int x, int y, int w, int h);

void reset_screen();
void reset_screen_config();

#endif
