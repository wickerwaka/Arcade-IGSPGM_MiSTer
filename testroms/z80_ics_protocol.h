#if !defined(Z80_ICS_PROTOCOL_H)
#define Z80_ICS_PROTOCOL_H 1

#if !defined(SYSTEM_H)
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned long u32;
typedef signed long s32;
#endif

#if defined(__GNUC__) && !defined(__SDCC)
#define Z80_ICS_PACKED __attribute__((packed))
#else
#define Z80_ICS_PACKED
#endif

#define Z80_ICS_SHARED_OFFSET 0x7000
#define Z80_ICS_MAGIC         0x1c5d
/* 0x1c51: original command set; 0x1c52: adds IRQ event log (0x32/0x33);
 * 0x1c53: adds the MDFourier timer-IRQ sequencer (0x40/0x41/0x42). */
#define Z80_ICS_DRIVER_MAGIC  0x1c53

#define Z80_ICS_STATUS_EMPTY  0x00
#define Z80_ICS_STATUS_READY  0x10
#define Z80_ICS_STATUS_BUSY   0x20
#define Z80_ICS_STATUS_DONE   0x30
#define Z80_ICS_STATUS_ERROR  0xe0

#define Z80_ICS_ERR_NONE      0x00
#define Z80_ICS_ERR_BAD_MAGIC 0x01
#define Z80_ICS_ERR_BAD_CMD   0x02
#define Z80_ICS_ERR_BAD_WIDTH 0x03
#define Z80_ICS_ERR_TIMEOUT   0x04

#define Z80_ICS_CMD_PING             0x01
#define Z80_ICS_CMD_READ_REG         0x10
#define Z80_ICS_CMD_WRITE_REG        0x11
#define Z80_ICS_CMD_READ_VOICE       0x20
#define Z80_ICS_CMD_WRITE_VOICE      0x21
#define Z80_ICS_CMD_GET_IRQ_COUNTS   0x30
#define Z80_ICS_CMD_RESET_IRQ_COUNTS 0x31
#define Z80_ICS_CMD_GET_IRQ_LOG      0x32
#define Z80_ICS_CMD_CLEAR_IRQ_LOG    0x33
/* result = raw ICS status port (0x8000) read */
#define Z80_ICS_CMD_READ_STATUS      0x34

/* MDFourier sequencer (driver magic >= 0x1c53).  The host writes a script of
 * Z80_ICS_MDF_MAX_ENTRIES entries into shared RAM at Z80_ICS_OFF_MDF_SCRIPT and
 * the entry count at Z80_ICS_OFF_MDF_COUNT, then issues these commands.  The
 * sequence is advanced entirely from the ICS timer-0 IRQ handler so it is
 * sample-locked and identical on hardware and in the simulator. */
/* value = (timer0 scale<<8) | timer0 preset; arms timer 0 and starts at entry 0 */
#define Z80_ICS_CMD_MDF_START        0x41
/* value = entry count; latch the script length already written to shared RAM */
#define Z80_ICS_CMD_MDF_LOAD         0x40
/* result = (running?0x8000:0) | (current entry index & 0x7fff) */
#define Z80_ICS_CMD_MDF_STATUS       0x42

/* One script step applied to voice 0 on its hold boundary.
 *   fc     : OscFC (reg 0x01) — sets the looped-sample repeat pitch
 *   pan    : Pan   (reg 0x0c, upper8)
 *   action : Z80_ICS_MDF_ACT_*  (key the oscillator on or off)
 *   ticks  : timer-0 IRQs to hold this step (1 tick == 1 MDFourier frame) */
#define Z80_ICS_MDF_ACT_OFF   0    /* osc_ctl = 0x0f -> output forced to silence */
#define Z80_ICS_MDF_ACT_ON    1    /* osc_ctl = 0x00 -> looped sample plays      */

#define Z80_ICS_MDF_ENTRY_SIZE 6   /* fc_hi fc_lo pan action ticks_hi ticks_lo */
#define Z80_ICS_MDF_MAX_ENTRIES 256

#define Z80_ICS_WIDTH_16       0
#define Z80_ICS_WIDTH_UPPER8   1
#define Z80_ICS_WIDTH_LOWER8   2

#define Z80_ICS_OFF_MAGIC      0
#define Z80_ICS_OFF_SEQ        2
#define Z80_ICS_OFF_CMD        3
#define Z80_ICS_OFF_STATUS     4
#define Z80_ICS_OFF_ERROR      5
#define Z80_ICS_OFF_VOICE      6
#define Z80_ICS_OFF_REG        7
#define Z80_ICS_OFF_WIDTH      8
#define Z80_ICS_OFF_RESERVED   9
#define Z80_ICS_OFF_VALUE      10
#define Z80_ICS_OFF_RESULT     12
#define Z80_ICS_OFF_TIMER0     14
#define Z80_ICS_OFF_TIMER1     18
#define Z80_ICS_OFF_OSC_IRQ    22
#define Z80_ICS_OFF_VOL_IRQ    26
#define Z80_ICS_OFF_SPURIOUS   30
#define Z80_ICS_OFF_VOICE_DATA 34

/* Packed in 68000-native big-endian byte order in the shared block.  The Z80
 * side explicitly converts each multi-byte field instead of casting this type. */
typedef struct Z80_ICS_PACKED
{
    u8  osc_conf;
    u16 osc_fc;
    u16 osc_start_hi;
    u8  osc_start_lo;
    u16 osc_end_hi;
    u8  osc_end_lo;
    u8  vol_incr;
    u8  vol_start;
    u8  vol_end;
    u16 vol_acc;
    u16 osc_acc_hi;
    u16 osc_acc_lo;
    u8  pan;
    u8  vol_ctrl;
    u8  osc_ctl;
    u8  osc_saddr;
    u8  vmode;
    u8  reserved;
} z80_ics_voice_t;

#define Z80_ICS_VOICE_SIZE 24

/* IRQ event log: ring of the raw bytes the Z80 IRQ handler observed, in
 * service order.  Entry = {seq, kind, a, b}:
 *   kind 0 TIMER:    a = 0x43 read value, b = status-port value
 *   kind 1 IRQV:     a = IRQV read value, b = status-port value
 *   kind 2 SPURIOUS: a = status-port value, b = 0
 */
#define Z80_ICS_OFF_LOG_COUNT (Z80_ICS_OFF_VOICE_DATA + Z80_ICS_VOICE_SIZE)
#define Z80_ICS_OFF_LOG_DATA  (Z80_ICS_OFF_LOG_COUNT + 2)
#define Z80_ICS_IRQ_LOG_MAX        32
#define Z80_ICS_IRQ_LOG_ENTRY_SIZE 4
#define Z80_ICS_IRQ_LOG_KIND_TIMER    0
#define Z80_ICS_IRQ_LOG_KIND_IRQV     1
#define Z80_ICS_IRQ_LOG_KIND_SPURIOUS 2

/* MDFourier script storage lives after the IRQ log.  The script bytes sit in
 * shared RAM and are read directly by the timer IRQ during playback (no copy);
 * the host fills them with z80_ics_mdf_load_chunk() and the count with
 * Z80_ICS_CMD_MDF_LOAD. */
#define Z80_ICS_OFF_MDF_COUNT \
    (Z80_ICS_OFF_LOG_DATA + Z80_ICS_IRQ_LOG_MAX * Z80_ICS_IRQ_LOG_ENTRY_SIZE)
#define Z80_ICS_OFF_MDF_SCRIPT (Z80_ICS_OFF_MDF_COUNT + 2)

#define Z80_ICS_SHARED_SIZE \
    (Z80_ICS_OFF_MDF_SCRIPT + Z80_ICS_MDF_MAX_ENTRIES * Z80_ICS_MDF_ENTRY_SIZE)

#endif
