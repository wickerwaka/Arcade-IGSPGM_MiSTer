#include "../system.h"
#include "../memory_map.h"
#include "../page.h"

#include "../util.h"
#include "../tilemap.h"
#include "../igs023.h"
#include "../color.h"
#include "../gui.h"

static u32 frame_count;
static u32 vblank_checkpoint;
static bool en_sprite_dma;
static bool en_irq4;
static bool en_bus_master;
static u8 sx = 0x10;

typedef enum
{
    TS_DONE,
    TS_START,
    TS_RUN
} TestState;

static volatile TestState test_state;
static volatile u16 test_active;
static volatile u16 test_count;

static void vblank_handler()
{
    switch(test_state)
    {
        case TS_DONE:
            break;

        case TS_START:
            test_state = TS_RUN;
            test_active = 0xffff;
            break;

        case TS_RUN:
            test_count--;
            if (test_count == 0)
            {
                test_active = 0x0000;
                test_state = TS_DONE;
            }
            break;
    }

    igs023_ack_irq6();
}

static void init()
{
    igs023_init();
    text_reset();
    set_default_palette();

    memset(VRAM->bg, 0, sizeof(VRAM->bg));

    frame_count = 0;
    vblank_checkpoint = igs023_get_vblank_count();
    en_sprite_dma = false;
    en_irq4 = false;
    en_bus_master = false;

    IGS023_CTRL_AND(~(IGS023_CTRL_BUS_MASTER | IGS023_CTRL_DMA | IGS023_CTRL_IRQ4_EN));
    
    memset(SPRITE_BUFFER, 0xffff, sizeof(SPRITE_BUFFER));
}

void start_test(u16 count)
{
    test_count = count;
    test_state = TS_START;
    test_active = 0x0000;

    while(test_active == 0) {};
}

static void update()
{
    igs023_wait_vblank();

    text_color(2);
    text_cursor(3, 2);
    textf("COUNT: %08X VBL: %08X\n", frame_count, igs023_get_vblank_count() - vblank_checkpoint);

    frame_count++;

    gui_begin(3, 4);
    if (gui_button("RESET COUNT"))
    {
        frame_count = 0;
        vblank_checkpoint = igs023_get_vblank_count();
    }
    
    if (gui_u8("SX", &sx, 0, 0x1f)) IGS023_BG_CTRL_SET(( IGS023_BG_CTRL_GET() & 0xffe0 ) | ( sx & 0x1f ));
    
    if (gui_toggle("IRQ4", &en_irq4))
    {
        if (en_irq4)
        {
            IGS023_CTRL_OR(IGS023_CTRL_IRQ4_EN);
        }
        else
        {
            IGS023_CTRL_AND(~IGS023_CTRL_IRQ4_EN);
        }
    }

    if (gui_toggle("SPRITE DMA", &en_sprite_dma))
    {
        if (en_sprite_dma)
        {
            IGS023_CTRL_OR(IGS023_CTRL_DMA);
        }
        else
        {
            IGS023_CTRL_AND(~IGS023_CTRL_DMA);
        }
    }

    if (gui_toggle("68K BUS MASTER", &en_bus_master))
    {
        if (en_bus_master)
        {
            IGS023_CTRL_OR(IGS023_CTRL_BUS_MASTER);
        }
        else
        {
            IGS023_CTRL_AND(~IGS023_CTRL_BUS_MASTER);
        }
    }

    if (gui_button("TEST COUNT"))
    {
        u32 simple_inc = 0;
        start_test(60);
        while(test_active)
        {
            simple_inc++;
        }

        text_cursor(20, 18);
        // PGM  = 0x9158a
        // FPGA = 0x91590
        textf("TEST COUNTS: %08u", simple_inc);
    }

    if (gui_button("TEST VRAM WRITE SM"))
    {
        u32 simple_inc = 0;
        volatile u16 *ptr = (volatile u16 *)VRAM->bg;
        start_test(60);
        while(test_active)
        {
            *ptr = 0xffff;
            simple_inc++;
        }

        text_cursor(20, 19);
        // Normal
        // PGM = 0x42480 - 0x424E0
        // FPGA = 0x04DD0
        // Bus Master
        // PGM = 0x583ED
        textf("VRAM WRITES: %08u", simple_inc);
    }

    if (gui_button("TEST VRAM WRITE LG"))
    {
        u32 simple_inc = 0;
        volatile u16 *ptr = (volatile u16 *)VRAM->bg;
        start_test(60);
        while(test_active)
        {
            memset(ptr, 0xff, 16);
            simple_inc++;
        }

        text_cursor(20, 19);
        // Normal
        // PGM = 0x42480 - 0x424E0
        // FPGA = 0x04DD0
        // Bus Master
        // PGM = 0x583ED
        textf("VRAM WRITES: %08u", simple_inc);
    }


    gui_end();
}

PAGE_REGISTER_IRQ(video_timing, init, update, NULL, NULL, vblank_handler);
 
