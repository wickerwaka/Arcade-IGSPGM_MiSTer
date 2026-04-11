#include "color.h"
#include "system.h"
#include "memory_map.h"
#include "util.h"

void set_fg_palette(uint16_t pal_index, uint16_t *colors)
{
    uint16_t offset = pal_index * 16;

    memcpy(PALRAM->fg + offset, colors, 32);
}

static void gradient(uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1, uint8_t g1, uint8_t b1,
                     uint16_t *colors, uint16_t count)
{
    int8_t rd = (r1 - r0) / count;
    int8_t gd = (g1 - g0) / count;
    int8_t bd = (b1 - b0) / count;
        
    for( int i = 0; i < count - 1; i++ )
    {
        colors[i] = RGB_ENCODE(r0, g0, b0);
        r0 += rd;
        g0 += gd;
        b0 += bd;
    }

    colors[count - 1] = RGB_ENCODE(r1, g1, b1);
}

static void set_text_palette(uint16_t index, uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1, uint8_t g1, uint8_t b1)
{
    uint16_t colors[16];
    for( int i = 0; i < 16; i++ ) colors[i] = 0;
    gradient(r0, g0, b0, r1, g1, b1, colors, 7);
    set_fg_palette(index, colors);
}

static u16 simple_spr[32] =
{ 
    0x0000, 0x1C20, 0x2C60, 0x40E1, 0x5966, 0x69C3, 0x7667, 0x7B0E,
    0x7FB5, 0x10E9, 0x318E, 0x4653, 0x52D8, 0x5F19, 0x6B5B, 0x77BD,
    0x04AF, 0x19B7, 0x3AFF, 0x1D2A, 0x35D0, 0x4A94, 0x5AF8, 0x675B,
    0x7FFE, 0x3540, 0x5E04, 0x6EA7, 0x7FEA, 0x58E2, 0x7D89, 0x0000
};

static u16 logo_pal[32] =
{
    0x0C63, 0x0864, 0x0CA6, 0x10E9, 0x152B, 0x1D6D, 0x21D0, 0x2A33,
    0x3AB6, 0x4719, 0x579C, 0x6BFF, 0x2081, 0x2CC1, 0x34E1, 0x4121,
    0x4981, 0x55E1, 0x5E21, 0x6A81, 0x7301, 0x7F80, 0x7FED, 0x7FF2,
    0x7FF6, 0x7FFF, 0x03BF, 0x03BF, 0x03BF, 0x03BF, 0x03BF, 0x0000
};

void set_default_palette()
{
    memset(PALRAM, 0, sizeof(*PALRAM));

    gradient(0, 255, 0, 255, 0, 255, PALRAM->sprites, 32);
    gradient(0, 255, 0, 255, 0, 255, PALRAM->bg, 32);

    memcpy(&PALRAM->sprites[32], simple_spr, 32 * 2);
    memcpy(&PALRAM->bg[32], logo_pal, 32 * 2);
    
    set_text_palette(0, 255, 255, 255, 255, 255, 255);
    set_text_palette(1, 128, 128, 192, 192, 192, 255);
    set_text_palette(2, 192, 128, 128, 255, 192, 192);
    set_text_palette(3, 128, 192, 128, 192, 255, 192);
    set_text_palette(4, 255,   0,   0, 255,   0,   0);
}

