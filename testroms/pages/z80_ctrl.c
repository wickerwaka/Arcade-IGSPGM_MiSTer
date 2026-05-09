#include "../system.h"
#include "../memory_map.h"
#include "../page.h"

#include "../util.h"
#include "../tilemap.h"
#include "../igs023.h"
#include "../color.h"
#include "../gui.h"


volatile u16 *Z80_CTRL_0 = (volatile u16 *)0xc00000;
volatile u16 *Z80_CTRL_1 = (volatile u16 *)0xc00002;
volatile u16 *Z80_CTRL_2 = (volatile u16 *)0xc00004;
volatile u16 *Z80_CTRL_3 = (volatile u16 *)0xc00006;
volatile u16 *Z80_RESET = (volatile u16 *)0xc00008;
volatile u16 *Z80_CONTROL = (volatile u16 *)0xc0000a;
volatile u16 *Z80_CTRL_6 = (volatile u16 *)0xc0000c;
volatile u16 *Z80_CTRL_7 = (volatile u16 *)0xc0000e;

static uint16_t ctrl[8];
static bool force_reset = false;
static bool bus_owner = false;

static void init()
{
    igs023_init();
    text_reset();
    set_default_palette();

/*    ctrl[0] = 0; *Z80_CTRL_0 = 0;
    ctrl[1] = 0; *Z80_CTRL_1 = 0;
    ctrl[2] = 0; *Z80_CTRL_2 = 0;
    ctrl[3] = 0; *Z80_CTRL_3 = 0;
    ctrl[4] = 0; *Z80_CTRL_4 = 0;
    ctrl[5] = 0; *Z80_CTRL_5 = 0;
    ctrl[6] = 0; *Z80_CTRL_6 = 0;
    ctrl[7] = 0; *Z80_CTRL_7 = 0;*/
}

static void update()
{
    igs023_wait_vblank();
    gui_begin(3, 4);
    if (gui_toggle("RESET", &force_reset))
    {
        if (force_reset)
            *Z80_RESET = 0xa659;
        else
            *Z80_RESET = 0x5050;
    }

    if (gui_toggle("BUS", &bus_owner))
    {
        if (bus_owner)
            *Z80_CONTROL = 0x45d3;
        else
            *Z80_CONTROL = 0x0a0a;
    }

    //if (gui_bits16("CTRL 0", &ctrl[0])) *Z80_CTRL_0 = ctrl[0];
//    if (gui_bits16("RESET", &ctrl[4])) *Z80_CTRL_4 = ctrl[4];
//    if (gui_bits16("CONTROL", &ctrl[5])) *Z80_CTRL_5 = ctrl[5];
    //if (gui_bits16("CTRL 7", &ctrl[7])) *Z80_CTRL_7 = ctrl[7];
    gui_end();
}

PAGE_REGISTER(z80_ctrl, init, update, NULL);

