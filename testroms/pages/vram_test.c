#include "../system.h"
#include "../memory_map.h"
#include "../page.h"

#include "../util.h"
#include "../tilemap.h"
#include "../igs023.h"
#include "../gui.h"
#include "../color.h"

static u16 attrib = 0;

static void init()
{
    igs023_init();
    text_reset();
    set_default_palette();

    IGS023_FG_X_SET(15);
    IGS023_FG_Y_SET(8);

    memset(VRAM->bg, 0, sizeof(VRAM->bg));
}

static void vram_update()
{
    igs023_wait_vblank();

    //while(IGS023_SCANLINE_RAW() != 0) {};
    while(IGS023_SCANLINE_RAW() != 50) memset((volatile u16 *)&VRAM->fg[0].code, 0xffff, 16);
    while(IGS023_SCANLINE_RAW() < 100) memset((volatile u16 *)&VRAM->bg[0].code, 0xffff, 16);
}

PAGE_REGISTER(vram_test, init, vram_update, NULL);

