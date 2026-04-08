#if !defined (SYSTEM_H)
#define SYSTEM_H 1

#include <stdint.h>

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;

extern volatile uint32_t vblank_count;
extern volatile uint32_t irq4_count;

void wait_vblank();

#endif
