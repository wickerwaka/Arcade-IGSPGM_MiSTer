#if !defined(MEMORY_MAP_H)
#define MEMORY_MAP_H 1

#include "system.h"

static volatile s16 *IGS023_SPRITES = (volatile s16 *)0xb00000;
static volatile s16 *IGS023_ZOOM = (volatile s16 *)0xb01000;
static volatile s16 *IGS023_BG_Y = (volatile s16 *)0xb02000;
static volatile s16 *IGS023_BG_X = (volatile s16 *)0xb03000;
static volatile s16 *IGS023_UNK1 = (volatile s16 *)0xb04000;
static volatile s16 *IGS023_FG_Y = (volatile s16 *)0xb05000;
static volatile s16 *IGS023_FG_X = (volatile s16 *)0xb06000;
static volatile s16 *IGS023_SCANLINE = (volatile s16 *)0xb07000;
static volatile s16 *IGS023_UNK2 = (volatile s16 *)0xb08000;
static volatile s16 *IGS023_UNK3 = (volatile s16 *)0xb09000;
static volatile s16 *IGS023_UNK4 = (volatile s16 *)0xb0a000;
static volatile s16 *IGS023_UNK5 = (volatile s16 *)0xb0b000;
static volatile s16 *IGS023_UNK6 = (volatile s16 *)0xb0c000; // Reading from this causes sync issues?
static volatile s16 *IGS023_UNK7 = (volatile s16 *)0xb0d000; // Reading from this can also cause sync issues?
static volatile s16 *IGS023_CTRL = (volatile s16 *)0xb0e000;
static volatile s16 *IGS023_UNK8 = (volatile s16 *)0xb0f000;

#define IGS023_CTRL_DMA 0x0001
#define IGS023_CTRL_UNK1 0x0002
#define IGS023_CTRL_IRQ4_EN 0x0004
#define IGS023_CTRL_IRQ6_EN 0x0008
#define IGS023_CTRL_UNK2 0x0010
#define IGS023_CTRL_UNK3 0x0020
#define IGS023_CTRL_UNK4 0x0040
#define IGS023_CTRL_UNK5 0x0080
#define IGS023_CTRL_UNK6 0x0100
#define IGS023_CTRL_UNK7 0x0200
#define IGS023_CTRL_UNK8 0x0400
#define IGS023_CTRL_DISABLE_FG 0x0800
#define IGS023_CTRL_DISABLE_BG 0x1000
#define IGS023_CTRL_DISABLE_HIGH_PRIO 0x2000
#define IGS023_CTRL_UNK9 0x4000
#define IGS023_CTRL_UNK10 0x8000

typedef struct
{
    u16 code;
    u16 unk2 : 8;
    u16 flipy : 1;
    u16 flipx : 1;
    u16 color : 5;
    u16 unk1 : 1;
} IGS023Tile;

typedef struct
{
    u16 xscale_mode : 1;
    u16 xscale_table : 4;
    s16 xpos : 11;

    u16 yscale_mode : 1;
    u16 yscale_table : 4;
    u16 unk3 : 1;
    s16 ypos : 10;

    u16 unk1 : 1;
    u16 yflip : 1;
    u16 xflip : 1;
    u16 color : 5;
    u16 prio : 1;
    u16 address_hi : 7;

    u16 address_lo : 16;

    u16 unk2 : 1;
    u16 width : 6;
    u16 height : 9;
} IGS023Sprite;

_Static_assert(sizeof(IGS023Sprite) == 10,     "IGS023Sprite size mismatch");


typedef struct
{
    IGS023Tile bg[64 * 64];
    IGS023Tile fg[64 * 32];
    u16 unused1[0x800];
    s16 bg_scroll[0x400];
    u16 unused2[0x400];
} IGS023VRAM;

typedef struct
{
    u16 sprites[32 * 32];
    u16 bg[32 * 32];
    u16 fg[16 * 32];
    u16 unused[48 * 32];
} IGS023PALRAM;

_Static_assert(sizeof(IGS023VRAM)   == 0x8000, "IGS023VRAM size mismatch");
_Static_assert(sizeof(IGS023PALRAM) == 0x2000, "IGS023VRAM size mismatch");

static IGS023VRAM *VRAM = (IGS023VRAM *)0x900000;
static IGS023PALRAM *PALRAM = (IGS023PALRAM *)0xa00000;

extern IGS023Sprite SPRITE_BUFFER[256];

#endif
