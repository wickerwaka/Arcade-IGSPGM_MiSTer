#include "../system.h"
#include "../memory_map.h"
#include "../page.h"

#include "../util.h"
#include "../tilemap.h"

static uint16_t frame_count = 0;

static void init()
{
    reset_screen();

    frame_count = 0;
}

static void update()
{
    wait_vblank();

    text_color(1);
    text_cursor(3, 2);
    textf("VBL: %05X  IRQ: %05X FRAME: %05X\n", vblank_count, irq4_count, frame_count);


    text_cursor(3, 13);

    text("XXXXXXXXXXXXXXXXXXXXXXXXXXXX\n");
    text("XXXXXXXXXXXXXXXXXXXXXXXXXXXX\n");
    text("XXXXXXXXXXXXXXXXXXXXXXXXXXXX\n");
    text("XXXXXXXXXXXXXXXXXXXXXXXXXXXX\n");
    text("XXXXXXXXXXXXXXXXXXXXXXXXXXXX\n");

    int x = 0;
    while(*IGS023_SCANLINE < 100) {}
    while(*IGS023_SCANLINE < 120)
    {
        //VRAM->bg[x].code = 0xffff;
        PALRAM->fg[x] = 0xffff;
        PALRAM->fg[x+1] = 0xffff;
        PALRAM->fg[x+2] = 0xffff;
        PALRAM->fg[x+3] = 0xffff;
        PALRAM->fg[x+4] = 0xffff;
        PALRAM->fg[x+5] = 0xffff;
        PALRAM->fg[x+6] = 0xffff;
        PALRAM->fg[x+7] = 0xffff;
        x = x ? 0 : 1;
    } 
    frame_count++;
}

PAGE_REGISTER(system_basics, init, update, NULL);

