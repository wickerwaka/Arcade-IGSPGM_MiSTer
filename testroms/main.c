#include <stdint.h>
#include <stdbool.h>

#include "interrupts.h"
#include "tilemap.h"
#include "input.h"
#include "system.h"
#include "page.h"
#include "memory_map.h"
#include "util.h"

#include "color.h"

volatile uint32_t vblank_count = 0;
volatile uint32_t irq4_count = 0;

__attribute__ ((section(".sprite_buffer"))) __attribute__((used))
IGS023Sprite SPRITE_BUFFER[256];

void wait_vblank()
{
    uint32_t current = vblank_count;
    while( current == vblank_count )
    {
    }
}

void level6_handler()
{
    vblank_count++;

    *IGS023_CTRL &= ~IGS023_CTRL_IRQ6_EN;
    *IGS023_CTRL |= IGS023_CTRL_IRQ6_EN;
}

void level4_handler()
{
    irq4_count++;
    *IGS023_CTRL &= ~IGS023_CTRL_IRQ4_EN;
    *IGS023_CTRL |= IGS023_CTRL_IRQ4_EN;
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
    *IGS023_CTRL = IGS023_CTRL_IRQ6_EN | IGS023_CTRL_IRQ4_EN;

    *IGS023_CTRL |= IGS023_CTRL_UNK10;
    reset_screen();
    enable_interrupts();
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

