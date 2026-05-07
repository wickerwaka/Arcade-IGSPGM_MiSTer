#include "../system.h"
#include "../memory_map.h"
#include "../page.h"

#include "../util.h"
#include "../tilemap.h"
#include "../igs023.h"
#include "../color.h"

#define VRAM_WORDS (sizeof(IGS023VRAM) / sizeof(u16))
#define RESULT_MAGIC 0x5654 /* 'VT' */
#define NUM_PASSES 8

typedef struct
{
    u16 magic;
    u16 running;
    u16 done_once;
    u16 failed_once;
    u16 iteration_hi;
    u16 iteration_lo;
    u16 pass_index;
    u16 errors_hi;
    u16 errors_lo;
    u16 first_index_hi;
    u16 first_index_lo;
    u16 first_expected;
    u16 first_actual;
    u16 vram_words_hi;
    u16 vram_words_lo;
    u16 read_swap;
} TestStatus;

__attribute__((section(".test_status"))) __attribute__((used))
static volatile TestStatus test_status;

static u32 iteration;
static u32 total_errors;
static u32 first_index;
static u16 first_expected;
static u16 first_actual;
static u16 pass_index;
static u16 done_once;
static u16 failed_once;
static u16 read_swap;

static u16 pattern_for(u16 pass, u16 index)
{
    switch (pass & 7)
    {
        case 0: return 0x0000;
        case 1: return 0xffff;
        case 2: return 0xaaaa;
        case 3: return 0x5555;
        case 4: return index;
        case 5: return ~index;
        case 6: return (u16)((index * 0x1021u) ^ (index >> 3) ^ 0x5a5au);
        default: return (u16)(((index << 7) | (index >> 9)) ^ 0xc33cu);
    }
}

static void publish_result(u16 running)
{
    test_status.magic = RESULT_MAGIC;
    test_status.running = running;
    test_status.done_once = done_once;
    test_status.failed_once = failed_once;
    test_status.iteration_hi = (u16)(iteration >> 16);
    test_status.iteration_lo = (u16)iteration;
    test_status.pass_index = pass_index;
    test_status.errors_hi = (u16)(total_errors >> 16);
    test_status.errors_lo = (u16)total_errors;
    test_status.first_index_hi = (u16)(first_index >> 16);
    test_status.first_index_lo = (u16)first_index;
    test_status.first_expected = first_expected;
    test_status.first_actual = first_actual;
    test_status.vram_words_hi = (u16)(VRAM_WORDS >> 16);
    test_status.vram_words_lo = (u16)VRAM_WORDS;
    test_status.read_swap = read_swap;
}

static void note_error(u32 index, u16 expected, u16 actual)
{
    if (total_errors == 0)
    {
        first_index = index;
        first_expected = expected;
        first_actual = actual;
    }

    total_errors++;
    failed_once = 1;
}

static void run_one_pass()
{
    volatile u16 *vram = (volatile u16 *)VRAM;
    u16 pass = pass_index;

    publish_result(1);

    for (u32 i = 0; i < VRAM_WORDS; i++)
    {
        vram[i] = pattern_for(pass, (u16)i);
    }

    for (u32 i = 0; i < VRAM_WORDS; i++)
    {
        u16 expected = pattern_for(pass, (u16)i);
        u16 actual = vram[i];
        if (actual != expected)
        {
            note_error(i, expected, actual);
        }
    }

    pass_index++;
    if (pass_index == NUM_PASSES)
    {
        pass_index = 0;
        iteration++;
        done_once = 1;
    }

    publish_result(0);
}

static void draw_status()
{
    text_reset();
    IGS023_FG_X_SET(8);
    IGS023_FG_Y_SET(8);

    text_color(failed_once ? 2 : 1);
    text_cursor(2, 2);
    text("VRAM WRITE/READ TEST\n");

    text_color(1);
    textf("WORDS: %08X\n", (u32)VRAM_WORDS);
    textf("ITER : %08X\n", iteration);
    textf("PASS : %04X/%04X\n", pass_index, NUM_PASSES);
    textf("ERRS : %08X\n", total_errors);
    textf("RSWAP: %04X\n", read_swap);

    if (failed_once)
    {
        text_color(2);
        textf("FIRST IDX: %08X\n", first_index);
        textf("EXPECTED : %04X\n", first_expected);
        textf("ACTUAL   : %04X\n", first_actual);
    }
    else if (done_once)
    {
        text_color(1);
        text("STATUS: PASS\n");
    }
    else
    {
        text_color(3);
        text("STATUS: RUNNING\n");
    }
}

static void init()
{
    igs023_init();
    text_reset();
    set_default_palette();

    IGS023_BG_CTRL_SET(0xffff);
    IGS023_FG_X_SET(8);
    IGS023_FG_Y_SET(8);

    iteration = 0;
    total_errors = 0;
    first_index = 0;
    first_expected = 0;
    first_actual = 0;
    pass_index = 0;
    done_once = 0;
    failed_once = 0;

    volatile u16 *vram = (volatile u16 *)VRAM;
    vram[0] = 0x12a5;
    read_swap = (vram[0] == 0xa512) ? 1 : 0;

    publish_result(0);
    draw_status();
}

static void update()
{
    igs023_wait_vblank();
    if (!failed_once)
    {
        run_one_pass();
    }
    draw_status();
}

PAGE_REGISTER(vram_write_test, init, update, NULL);
