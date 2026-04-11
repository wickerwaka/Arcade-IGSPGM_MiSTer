#include "../system.h"
#include "../memory_map.h"
#include "../page.h"

#include "../util.h"
#include "../tilemap.h"
#include "../igs023.h"
#include "../gui.h"
#include "../color.h"

static void block_at(u8 x, u8 y, u8 color)
{
    IGS023Tile *tile = text_get_tile(x, y);
    tile->code = 0x20;
    tile->color = color;
}

static void sym_at(u16 code, u8 x, u8 y, u8 color)
{
    IGS023Tile *tile = text_get_tile(x, y);
    tile->code = code;
    tile->color = color;
}


static u16 attrib = 0;
static u16 fg_x, fg_y;

static void init()
{
    igs023_init();
    text_reset();
    set_default_palette();

    *IGS023_FG_X = fg_x = 8;
    *IGS023_FG_Y = fg_y = 8;

    block_at(0, 1, 4);
    block_at(1, 0, 4);
    block_at(1, 1, 1);

    block_at(56, 12, 1);
    block_at(1, 11, 4);
    block_at(1, 13, 4);
    block_at(1, 14, 4);
    block_at(56, 14, 1);

    sym_at(0x2be, 1, 15, 4);

    block_at(0, 28, 4);
    block_at(1, 29, 4);
    block_at(1, 28, 1);
    
    sym_at(0x371, 2, 28, 4);

    memset(VRAM->bg, 0, sizeof(VRAM->bg));
}

static void update()
{
    igs023_wait_vblank();
    
    text_cursor(3, 3);
    textf("VBL: %05X\n", igs023_get_vblank_count());

    gui_begin(3, 5);
    gui_bits16("CTRL", (u16 *)IGS023_CTRL);
    gui_bits16("UNK1", (u16 *)IGS023_UNK1);
    gui_bits16("ATTRIB", &attrib);
    gui_u16("X", &fg_x);
    gui_u16("Y", &fg_y);
    gui_end();

    *IGS023_FG_X = fg_x;
    *IGS023_FG_Y = fg_y;

    text_cursor(4, 12);
    text("FFFFFFFFFF");
    IGS023Tile *tile = text_get_tile(4, 12);
    for( int i = 0; i < 10; i++ )
    {
        tile[i].attrib = attrib;
    }
}

PAGE_REGISTER(fg_test, init, update, NULL);

