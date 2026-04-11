#if !defined(IGS023_H)
#define IGS023_H 1

#include "memory_map.h"
#include <stdbool.h>

void igs023_init();
void igs023_enable_irq6(bool en);
void igs023_enable_irq4(bool en);

u32 igs023_wait_vblank();
u32 igs023_get_vblank_count();
u32 igs023_get_irq4_count();

static inline u16 igs023_get_scanline()
{
    return *IGS023_SCANLINE;
}

void igs023_ack_irq6();
void igs023_ack_irq4();

#endif
