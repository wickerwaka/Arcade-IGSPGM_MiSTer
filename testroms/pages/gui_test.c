#include "../system.h"
#include "../memory_map.h"
#include "../page.h"

#include "../util.h"
#include "../tilemap.h"
#include "../gui.h"

bool toggle1 = false;
uint16_t frame_count;
static void init()
{
    frame_count = 0;

    reset_screen();
}

static void update()
{
    wait_vblank();
    
    text_cursor(1, 1);
    textf("VBL: %05X %05X\n", vblank_count, vblank_count - frame_count);

    gui_begin(3, 3);

    gui_button("BUTTON 1");
    gui_bits16("CTRL", (u16 *)IGS023_CTRL);
    gui_toggle("TOGGLE 1", &toggle1);
    if( toggle1 )
        gui_button("BUTTON 3");
    gui_button("BUTTON 4");
    
    text_cursor(1, 16);
    textf("SCANLINE: %03X", *IGS023_SCANLINE);

    frame_count++;
}

PAGE_REGISTER(gui_test, init, update, NULL);

