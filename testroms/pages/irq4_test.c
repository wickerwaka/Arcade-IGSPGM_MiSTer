#include "../system.h"
#include "../memory_map.h"
#include "../page.h"

#include "../util.h"
#include "../tilemap.h"
#include "../igs023.h"
#include "../gui.h"
#include "../color.h"

bool toggle = false;
bool sync = false;
static void irq4_handler()
{
    igs023_ack_irq4();
    if (toggle)
    {
        PALRAM->fg[8 * 16] = 0xffff;
        toggle = false;
        if (sync) igs023_enable_irq4(false);
    }
    else
    {
        PALRAM->fg[8 * 16] = 0x0000;
        toggle = true;
    }
}

static void init()
{
    igs023_init();
    text_reset();
    set_default_palette();
    igs023_enable_irq4(true);
 

    for( u16 i = 0; i < 32 * 64; i++ )
    {
        VRAM->fg[i].code = 0x14;
        VRAM->fg[i].color = 8;
    }
}
    

static void update()
{
    igs023_wait_vblank();
    
    text_cursor(3, 3);
    textf("VBL: %05X\n", igs023_get_vblank_count());

    gui_begin(3, 5);
    gui_toggle("SYNC", &sync);
    gui_end();

    if (sync)
    {
        igs023_enable_irq4(false);
        PALRAM->fg[8 * 16] = 0x0000;
        toggle = false;
        while(IGS023_SCANLINE_GET() != 100) {}
        igs023_enable_irq4(true);
    }
    else
    {
        igs023_enable_irq4(true);
    }

}

PAGE_REGISTER_IRQ(irq4_test, init, update, NULL, irq4_handler, NULL);

