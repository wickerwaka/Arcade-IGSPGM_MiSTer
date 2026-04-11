#include "igs023.h"
#include "interrupts.h"

static volatile u32 irq6_count = 0;
static volatile u32 irq4_count = 0;

void igs023_init()
{
    disable_interrupts();

    *IGS023_CTRL = IGS023_CTRL_DMA;
    *IGS023_UNK1 = 0x610; // ???
    SPRITE_BUFFER[0].width = 0;
    SPRITE_BUFFER[0].height = 0;
    igs023_enable_irq6(true);
    igs023_enable_irq4(true);
    enable_interrupts();
}

void igs023_enable_irq4(bool en)
{
    if (en)
        *IGS023_CTRL |= IGS023_CTRL_IRQ4_EN;
    else
        *IGS023_CTRL &= ~IGS023_CTRL_IRQ4_EN;
}

void igs023_enable_irq6(bool en)
{
    if (en)
        *IGS023_CTRL |= IGS023_CTRL_IRQ6_EN;
    else
        *IGS023_CTRL &= ~IGS023_CTRL_IRQ6_EN;
}

u32 igs023_wait_vblank()
{
    if (*IGS023_CTRL & IGS023_CTRL_IRQ6_EN)
    {
        u32 cur = irq6_count;
        while(cur == irq6_count) {}
    }
    return irq6_count;
}

u32 igs023_get_vblank_count()
{
    return irq6_count;
}

u32 igs023_get_irq4_count()
{
    return irq4_count;
}


void igs023_ack_irq6()
{
    irq6_count++;
    *IGS023_CTRL &= ~IGS023_CTRL_IRQ6_EN;
    *IGS023_CTRL |= IGS023_CTRL_IRQ6_EN;
}

void igs023_ack_irq4()
{
    irq4_count++;
    *IGS023_CTRL &= ~IGS023_CTRL_IRQ4_EN;
    *IGS023_CTRL |= IGS023_CTRL_IRQ4_EN;
}




