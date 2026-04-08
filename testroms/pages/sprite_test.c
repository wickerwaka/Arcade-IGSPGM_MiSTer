#include "../system.h"
#include "../memory_map.h"
#include "../page.h"

#include "../util.h"
#include "../tilemap.h"

#include "../gui.h"

static uint16_t frame_count = 0;
static uint16_t zoom[2];

static void init()
{
    reset_screen();
    for( int i = 0; i < 64; i++ )
        IGS023_ZOOM[i] = 0x1000;
 
    frame_count = 0;
}

static void update()
{
    *IGS023_CTRL |= IGS023_CTRL_DMA;
    for( int i = 0; i < 32; i++ )
        IGS023_ZOOM[i] = 0x5555;

    wait_vblank();

    for( int i = 0; i < 32; i++ )
        IGS023_ZOOM[i] = 0x5555;

    text_color(1);
    text_cursor(3, 2);
    textf("VBL: %05X  FRAME: %05X\n", vblank_count, frame_count);

    IGS023Sprite *spr = SPRITE_BUFFER;

    spr->color = 0;
    spr->height = 89;
    spr->width = 10;
    spr->prio = 0;
    spr->xflip = 0;
    spr->yflip = 0;
    spr->xscale_mode = (vblank_count >> 7) & 0x1;
    spr->xscale_table = (vblank_count >> 3) & 0xf;
    spr->yscale_mode = (vblank_count >> 7) & 0x1;
    spr->yscale_table = (vblank_count >> 3) & 0xf;
    spr->address_lo = 0;
    spr->address_hi = 0;
    spr->xpos = 100;
    spr->ypos = 100;
    spr->unk1 = 0;
    spr->unk2 = 0;
    spr->unk3 = 0;

    spr++;
    spr->unk2 = 0;
    spr->width = 0;
    spr->height = 0;
   
    *IGS023_CTRL |= IGS023_CTRL_DMA;

    gui_begin(3, 4);
    gui_bits16("ZOOM 0", &zoom[0]);
    gui_bits16("ZOOM 1", &zoom[1]);
    gui_end();
}

PAGE_REGISTER(sprite_test, init, update, NULL);

