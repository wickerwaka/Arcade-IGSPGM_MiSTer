#if !defined(COLOR_H)
#define COLOR_H 1

#include "system.h"

#define RGB_ENCODE(r,g,b) ((((r) & 0xF8) << 7) | (((g) & 0xF8) << 2) | (((b) & 0xF8) >> 3))

void set_palettes(uint16_t pal_index, uint16_t pal_count, uint16_t *colors);

void set_gradient2(uint16_t pal_index, uint8_t r0, uint8_t g0, uint8_t b0,
                                       uint8_t r1, uint8_t g1, uint8_t b1);

void set_gradient3(uint16_t pal_index, uint8_t r0, uint8_t g0, uint8_t b0,
                                       uint8_t r1, uint8_t g1, uint8_t b1,
                                       uint8_t r2, uint8_t g2, uint8_t b2);

void set_default_palette();

#endif
