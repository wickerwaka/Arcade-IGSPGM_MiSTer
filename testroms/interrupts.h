#if !defined( INTERRUPTS_H )
#define INTERRUPTS_H 1

#if !defined(__clang__)
#define INTERRUPT __attribute__((interrupt))
#else
#define INTERRUPT
#endif


INTERRUPT void bus_error_handler();
INTERRUPT void address_error_handler();
INTERRUPT void illegal_instruction_handler();
INTERRUPT void zero_divide_handler();
INTERRUPT void chk_handler();
INTERRUPT void trapv_handler();
INTERRUPT void priv_violation_handler();
INTERRUPT void trace_handler();
INTERRUPT void trap_1010_handler();
INTERRUPT void trap_1111_handler();
INTERRUPT void uninitialized_handler();
INTERRUPT void spurious_handler();
INTERRUPT void level1_handler();
INTERRUPT void level2_handler();
INTERRUPT void level3_handler();
INTERRUPT void level4_handler();
INTERRUPT void level5_handler();
INTERRUPT void level6_handler();
INTERRUPT void level7_handler();
INTERRUPT void trap0_handler();
INTERRUPT void trap1_handler();
INTERRUPT void trap2_handler();
INTERRUPT void trap3_handler();
INTERRUPT void trap4_handler();
INTERRUPT void trap5_handler();
INTERRUPT void trap6_handler();
INTERRUPT void trap7_handler();
INTERRUPT void trap8_handler();
INTERRUPT void trap9_handler();
INTERRUPT void trap10_handler();
INTERRUPT void trap11_handler();
INTERRUPT void trap12_handler();
INTERRUPT void trap13_handler();
INTERRUPT void trap14_handler();
INTERRUPT void trap15_handler();

static inline void enable_interrupts() { __asm__( "andi #0xf8ff, %sr" ); }
static inline void disable_interrupts() { __asm__( "ori #0x0700, %sr" ); }

#endif
