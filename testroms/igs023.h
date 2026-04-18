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

void igs023_ack_irq6();
void igs023_ack_irq4();


#define IGS023_SHADOW(ty, name, addr) \
    extern ty __shadow_##name; \
    static inline void IGS023_##name##_SET(ty v) { __shadow_##name  = v; *(volatile ty *)addr = __shadow_##name; } \
    static inline ty IGS023_##name##_OR(ty v)  { __shadow_##name |= v; *(volatile ty *)addr = __shadow_##name; return __shadow_##name; } \
    static inline ty IGS023_##name##_XOR(ty v) { __shadow_##name ^= v; *(volatile ty *)addr = __shadow_##name; return __shadow_##name; } \
    static inline ty IGS023_##name##_AND(ty v) { __shadow_##name &= v; *(volatile ty *)addr = __shadow_##name; return __shadow_##name; } \
    static inline ty IGS023_##name##_GET()     { return __shadow_##name; } \
    static inline ty IGS023_##name##_RAW()     { return *(ty *)addr; }

IGS023_SHADOW(u16, BG_Y, 0xb02000);
IGS023_SHADOW(u16, BG_X, 0xb03000);
IGS023_SHADOW(u16, BG_CTRL, 0xb04000);
IGS023_SHADOW(u16, FG_Y, 0xb05000);
IGS023_SHADOW(u16, FG_X, 0xb06000);
IGS023_SHADOW(u16, SCANLINE, 0xb07000);
IGS023_SHADOW(u16, UNK8, 0xb08000);
IGS023_SHADOW(u16, UNK9, 0xb09000);
IGS023_SHADOW(u16, UNKA, 0xb0a000);
IGS023_SHADOW(u16, UNKB, 0xb0b000);
IGS023_SHADOW(u16, UNKC, 0xb0c000);
IGS023_SHADOW(u16, UNKD, 0xb0d000);
IGS023_SHADOW(u16, CTRL, 0xb0e000);
IGS023_SHADOW(u16, UNKF, 0xb0f000);

#endif
