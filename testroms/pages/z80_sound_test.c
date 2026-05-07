#include "../system.h"
#include "../memory_map.h"
#include "../page.h"

#include "../util.h"
#include "../tilemap.h"
#include "../igs023.h"
#include "../color.h"
#include "../input.h"
#include "../pgm_bios_z80_data.h"

#define RESULT_MAGIC 0x5a53 /* 'ZS' */

#define Z80_LATCH1      (*(volatile u16 *)0x00c00002)
#define Z80_LATCH2      (*(volatile u16 *)0x00c00004)
#define Z80_RESET_REG   (*(volatile u16 *)0x00c00008)
#define Z80_CONTROL_REG (*(volatile u16 *)0x00c0000a)
#define Z80_LATCH3      (*(volatile u16 *)0x00c0000c)

#define Z80_RAM8        ((volatile u8 *)0x00c10000)
#define Z80_RAM16       ((volatile u16 *)0x00c10000)

#define Z80_CONTROL_BUS_68K 0x45d3
#define Z80_CONTROL_RUN     0x0a0a
#define Z80_RESET_ASSERT    0xa659
#define Z80_RESET_RELEASE   0x5050

#define Z80_MAILBOX_OFFSET       0x0006
#define Z80_WAVE_COUNT_OFFSET    0x0042
#define Z80_MIDI_COUNT_OFFSET    0x0052
#define Z80_WAVE_TABLE_OFFSET    0x5000

#define WAVE_ENTRY_SIZE 12
#define DEFAULT_WAVE_COUNT (sizeof(default_z80_wave_table_blob) / WAVE_ENTRY_SIZE)
#define AUTO_PLAY_FRAMES 90

typedef struct
{
    u16 magic;
    u16 initialized;
    u16 verify_errors;
    u16 commands_sent;
    u16 selected_wave;
    u16 latch1;
    u16 latch2;
    u16 latch3;
    u16 phase;
    u16 frame_hi;
    u16 frame_lo;
} TestStatus;

__attribute__((section(".test_status"))) __attribute__((used))
static volatile TestStatus test_status;

static u16 initialized;
static u16 verify_errors;
static u16 commands_sent;
static u16 selected_wave;
static u16 phase;
static u32 frame_count;
static u8 sequence_mod64;
static u8 latch_low_shadow;
static u8 latch3_high_shadow;

static void delay_short(u16 count)
{
    volatile u16 i;
    for (i = 0; i < count; i++)
    {
    }
}

static void z80_bus_take(void)
{
    Z80_CONTROL_REG = Z80_CONTROL_BUS_68K;
    delay_short(16);
}

static void z80_bus_release(void)
{
    Z80_CONTROL_REG = Z80_CONTROL_RUN;
    delay_short(16);
}

static void z80_write_byte(u16 offset, u8 value)
{
    Z80_RAM8[offset] = value;
}

static u8 z80_read_byte(u16 offset)
{
    return Z80_RAM8[offset];
}

static void z80_write_be_word(u16 offset, u16 value)
{
    Z80_RAM8[offset + 0] = (u8)(value >> 8);
    Z80_RAM8[offset + 1] = (u8)value;
}

static void z80_write_bytes(u16 offset, const u8 *src, u16 size)
{
    for (u16 i = 0; i < size; i++)
    {
        z80_write_byte(offset + i, src[i]);
    }
}

static void z80_fill_words(u16 value)
{
    for (u32 i = 0; i < 0x8000; i++)
    {
        Z80_RAM16[i] = value;
    }
}

static void publish_status(void)
{
    test_status.magic = RESULT_MAGIC;
    test_status.initialized = initialized;
    test_status.verify_errors = verify_errors;
    test_status.commands_sent = commands_sent;
    test_status.selected_wave = selected_wave;
    test_status.latch1 = Z80_LATCH1;
    test_status.latch2 = Z80_LATCH2;
    test_status.latch3 = Z80_LATCH3;
    test_status.phase = phase;
    test_status.frame_hi = (u16)(frame_count >> 16);
    test_status.frame_lo = (u16)frame_count;
}

static void upload_wave_table(void)
{
    /* The BIOS appends this table at wave index 1.  Keep index 0 empty and
       publish count = entries + 1 so command sound_id 1 starts the first BIOS
       default wave entry. */
    z80_write_be_word(Z80_WAVE_COUNT_OFFSET, (u16)(DEFAULT_WAVE_COUNT + 1));
    z80_write_be_word(Z80_MIDI_COUNT_OFFSET, 0);

    for (u16 i = 0; i < WAVE_ENTRY_SIZE; i++)
    {
        z80_write_byte(Z80_WAVE_TABLE_OFFSET + i, 0);
    }

    z80_write_bytes(Z80_WAVE_TABLE_OFFSET + WAVE_ENTRY_SIZE,
                    default_z80_wave_table_blob,
                    (u16)(DEFAULT_WAVE_COUNT * WAVE_ENTRY_SIZE));
}

static void z80_init_sound_driver(void)
{
    initialized = 0;
    verify_errors = 0;
    sequence_mod64 = 0;
    latch_low_shadow = 0;
    latch3_high_shadow = 0;
    phase = 1;
    publish_status();

    Z80_CONTROL_REG = Z80_CONTROL_BUS_68K;
    Z80_RESET_REG = Z80_RESET_ASSERT;
    delay_short(256);

    phase = 2;
    publish_status();
    z80_fill_words(0x7676);

    phase = 3;
    publish_status();
    z80_write_bytes(0, embedded_z80_sound_driver_blob,
                    (u16)sizeof(embedded_z80_sound_driver_blob));

    phase = 4;
    publish_status();
    for (u16 i = 0; i < sizeof(embedded_z80_sound_driver_blob); i++)
    {
        if (z80_read_byte(i) != embedded_z80_sound_driver_blob[i])
        {
            verify_errors++;
            if (verify_errors == 0xffff)
            {
                break;
            }
        }
    }

    upload_wave_table();

    delay_short(256);
    Z80_CONTROL_REG = Z80_CONTROL_RUN;
    Z80_RESET_REG = Z80_RESET_RELEASE;
    initialized = 1;
    phase = 5;
    publish_status();
}

static void z80_send_mailbox_command(u8 b0, u8 b1, u8 b2, u8 b3)
{
    /* Same visible path as BIOS PumpZ80CommandQueue(): take Z80 RAM, write the
       4-byte mailbox at Z80 0006, release RAM, then pulse sound latch 1 with
       token 1 to trigger the Z80 NMI receive path. */
    Z80_LATCH3 = (u16)((latch_low_shadow & 0x0f) | 0x00f0);

    z80_bus_take();
    latch3_high_shadow = 0;
    Z80_LATCH3 = (u16)(latch_low_shadow & 0x0f);

    z80_write_byte(Z80_MAILBOX_OFFSET + 0, b0);
    z80_write_byte(Z80_MAILBOX_OFFSET + 1, b1);
    z80_write_byte(Z80_MAILBOX_OFFSET + 2, b2);
    z80_write_byte(Z80_MAILBOX_OFFSET + 3, b3);

    z80_bus_release();

    latch_low_shadow = 1;
    Z80_LATCH1 = (u16)((latch3_high_shadow & 0xf0) | 1);
    commands_sent++;
    phase = 6;
    publish_status();
}

static void play_wave(u16 wave_index, u8 owner_key)
{
    sequence_mod64 = (u8)((sequence_mod64 + 1) & 0x3f);

    u16 packed_sound_id = (u16)(((wave_index & 0x03ff) << 8) |
                                ((sequence_mod64 & 0x3f) << 2) |
                                ((wave_index >> 8) & 0x03));

    z80_send_mailbox_command(0x00,
                             owner_key,
                             (u8)(packed_sound_id >> 8),
                             (u8)packed_sound_id);
}

static void draw_status(void)
{
    text_reset();
    IGS023_FG_X_SET(8);
    IGS023_FG_Y_SET(8);

    text_color(1);
    text_cursor(2, 2);
    text("Z80 SOUND TEST\n");
    textf("INIT   : %04X\n", initialized);
    textf("VERIFY : %04X\n", verify_errors);
    textf("CMD    : %04X\n", commands_sent);
    textf("WAVE   : %04X/%04X\n", selected_wave, (u16)DEFAULT_WAVE_COUNT);
    textf("PHASE  : %04X\n", phase);
    textf("L1/L2  : %04X %04X\n", Z80_LATCH1, Z80_LATCH2);
    textf("L3     : %04X\n", Z80_LATCH3);
    text("\nBTN1 PLAY  BTN2 NEXT\n");
    text("BTN3 REINIT\n");

    if (verify_errors)
    {
        text_color(2);
        text("Z80 RAM VERIFY FAILED\n");
    }
    else if (initialized)
    {
        text_color(1);
        text("READY\n");
    }
    else
    {
        text_color(3);
        text("INITIALIZING\n");
    }
}

static void init(void)
{
    igs023_init();
    text_reset();
    set_default_palette();
    input_init();

    IGS023_BG_CTRL_SET(0xffff);
    IGS023_FG_X_SET(8);
    IGS023_FG_Y_SET(8);

    initialized = 0;
    verify_errors = 0;
    commands_sent = 0;
    selected_wave = 1;
    phase = 0;
    frame_count = 0;
    sequence_mod64 = 0;
    latch_low_shadow = 0;
    latch3_high_shadow = 0;

    publish_status();
    draw_status();
    z80_init_sound_driver();
}

static void update(void)
{
    igs023_wait_vblank();
    input_update();
    frame_count++;

    if (initialized && !verify_errors)
    {
        if (frame_count == AUTO_PLAY_FRAMES || input_pressed(BTN1))
        {
            play_wave(selected_wave, 0x40);
        }

        if (input_pressed(BTN2))
        {
            selected_wave++;
            if (selected_wave > DEFAULT_WAVE_COUNT)
            {
                selected_wave = 1;
            }
        }
    }

    if (input_pressed(BTN3))
    {
        z80_init_sound_driver();
    }

    publish_status();
    draw_status();
}

PAGE_REGISTER(z80_sound_test, init, update, NULL);
