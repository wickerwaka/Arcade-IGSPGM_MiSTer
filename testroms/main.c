#include <stdint.h>
#include <stdbool.h>

#include "interrupts.h"
#include "tilemap.h"
#include "input.h"
#include "system.h"
#include "page.h"
#include "memory_map.h"
#include "igs023.h"

#include "util.h"

#include "color.h"

__attribute__ ((section(".sprite_buffer"))) __attribute__((used))
IGS023Sprite SPRITE_BUFFER[256];

void level6_handler()
{
    if( !page_irq6() )
        igs023_ack_irq6();
}

void level4_handler()
{
    if( !page_irq4() )
        igs023_ack_irq4();
}

void illegal_instruction_handler()
{
    disable_interrupts();

    text_color(1);
    text_at(10,10,"ILLEGAL INSTRUCTION");

    while(true) {};
}

int main(int argc, char *argv[])
{
    igs023_init();

    memset(SPRITE_BUFFER, 0, 256 * 10);
    
    input_init();

    input_update();

    page_set_next_active();

    while(1)
    {
        input_update();

        if (input_pressed(START))
        {
            page_set_next_active();
        }

        page_update();
    }

    return 0;
}

