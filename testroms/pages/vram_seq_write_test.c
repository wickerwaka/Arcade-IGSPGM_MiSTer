#include "../system.h"
#include "../memory_map.h"
#include "../page.h"

#include "../util.h"
#include "../tilemap.h"
#include "../igs023.h"
#include "../color.h"

#define VRAM_WORDS (sizeof(IGS023VRAM) / sizeof(u16))
#define RESULT_MAGIC 0x5653 /* 'VS' */
#define WRITE_PASSES_PER_BATCH 4

typedef struct
{
    u16 magic;
    u16 running;
    u16 done_once;
    u16 failed_once;
    u16 batch_hi;
    u16 batch_lo;
    u16 pass_hi;
    u16 pass_lo;
    u16 phase;
    u16 errors_hi;
    u16 errors_lo;
    u16 first_index_hi;
    u16 first_index_lo;
    u16 first_expected;
    u16 first_actual;
    u16 vram_words_hi;
    u16 vram_words_lo;
    u16 writes_m_hi;
    u16 writes_m_lo;
    u16 writes_l_hi;
    u16 writes_l_lo;
} TestStatus;

__attribute__((section(".test_status"))) __attribute__((used))
static volatile TestStatus test_status;

static u32 batch_count;
static u32 pass_count;
static u32 total_errors;
static u32 first_index;
static u16 first_expected;
static u16 first_actual;
static u16 failed_once;
static u16 done_once;
static u16 phase;
static u32 total_write_words_hi;
static u32 total_write_words_lo;

static u16 pattern_for(u32 pass, u16 index)
{
    u16 seed = (u16)((pass * 0x31d5u) ^ (pass >> 5) ^ 0x4b39u);
    u16 mixed = (u16)((index * 0x1021u) ^ (index >> 3) ^ seed);
    return (u16)((mixed << (pass & 7)) | (mixed >> ((8 - pass) & 7)));
}

static void add_write_words(u32 words)
{
    u32 old = total_write_words_lo;
    total_write_words_lo += words;
    if (total_write_words_lo < old)
    {
        total_write_words_hi++;
    }
}

static void publish_result(u16 running)
{
    test_status.magic = RESULT_MAGIC;
    test_status.running = running;
    test_status.done_once = done_once;
    test_status.failed_once = failed_once;
    test_status.batch_hi = (u16)(batch_count >> 16);
    test_status.batch_lo = (u16)batch_count;
    test_status.pass_hi = (u16)(pass_count >> 16);
    test_status.pass_lo = (u16)pass_count;
    test_status.phase = phase;
    test_status.errors_hi = (u16)(total_errors >> 16);
    test_status.errors_lo = (u16)total_errors;
    test_status.first_index_hi = (u16)(first_index >> 16);
    test_status.first_index_lo = (u16)first_index;
    test_status.first_expected = first_expected;
    test_status.first_actual = first_actual;
    test_status.vram_words_hi = (u16)(VRAM_WORDS >> 16);
    test_status.vram_words_lo = (u16)VRAM_WORDS;
    test_status.writes_m_hi = (u16)(total_write_words_hi >> 16);
    test_status.writes_m_lo = (u16)total_write_words_hi;
    test_status.writes_l_hi = (u16)(total_write_words_lo >> 16);
    test_status.writes_l_lo = (u16)total_write_words_lo;
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

static void write_sequential_pass(u32 pass)
{
    volatile u16 *vram = (volatile u16 *)VRAM;

    for (u32 i = 0; i < VRAM_WORDS; i += 16)
    {
        vram[i + 0]  = pattern_for(pass, (u16)(i + 0));
        vram[i + 1]  = pattern_for(pass, (u16)(i + 1));
        vram[i + 2]  = pattern_for(pass, (u16)(i + 2));
        vram[i + 3]  = pattern_for(pass, (u16)(i + 3));
        vram[i + 4]  = pattern_for(pass, (u16)(i + 4));
        vram[i + 5]  = pattern_for(pass, (u16)(i + 5));
        vram[i + 6]  = pattern_for(pass, (u16)(i + 6));
        vram[i + 7]  = pattern_for(pass, (u16)(i + 7));
        vram[i + 8]  = pattern_for(pass, (u16)(i + 8));
        vram[i + 9]  = pattern_for(pass, (u16)(i + 9));
        vram[i + 10] = pattern_for(pass, (u16)(i + 10));
        vram[i + 11] = pattern_for(pass, (u16)(i + 11));
        vram[i + 12] = pattern_for(pass, (u16)(i + 12));
        vram[i + 13] = pattern_for(pass, (u16)(i + 13));
        vram[i + 14] = pattern_for(pass, (u16)(i + 14));
        vram[i + 15] = pattern_for(pass, (u16)(i + 15));
    }

    add_write_words(VRAM_WORDS);
}

static void verify_pass(u32 pass)
{
    volatile u16 *vram = (volatile u16 *)VRAM;

    for (u32 i = 0; i < VRAM_WORDS; i++)
    {
        u16 expected = pattern_for(pass, (u16)i);
        u16 actual = vram[i];
        if (actual != expected)
        {
            note_error(i, expected, actual);
        }
    }
}

static void run_batch()
{
    u32 verify_pass_index = pass_count;

    phase = 1;
    publish_result(1);

    for (u16 i = 0; i < WRITE_PASSES_PER_BATCH; i++)
    {
        write_sequential_pass(pass_count);
        verify_pass_index = pass_count;
        pass_count++;
        publish_result(1);
    }

    phase = 2;
    publish_result(1);
    verify_pass(verify_pass_index);

    batch_count++;
    done_once = 1;
    phase = 0;
    publish_result(0);
}

static void draw_status()
{
    text_reset();
    IGS023_FG_X_SET(8);
    IGS023_FG_Y_SET(8);

    text_color(failed_once ? 2 : 1);
    text_cursor(2, 2);
    text("VRAM SEQUENTIAL WRITE TEST\n");

    text_color(1);
    textf("WORDS : %08X\n", (u32)VRAM_WORDS);
    textf("BATCH : %08X\n", batch_count);
    textf("PASS  : %08X\n", pass_count);
    textf("WRHI  : %08X\n", total_write_words_hi);
    textf("WRLO  : %08X\n", total_write_words_lo);
    textf("ERRS  : %08X\n", total_errors);

    if (failed_once)
    {
        text_color(2);
        textf("FIRST : %08X\n", first_index);
        textf("EXP   : %04X\n", first_expected);
        textf("ACT   : %04X\n", first_actual);
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

    batch_count = 0;
    pass_count = 0;
    total_errors = 0;
    first_index = 0;
    first_expected = 0;
    first_actual = 0;
    failed_once = 0;
    done_once = 0;
    phase = 0;
    total_write_words_hi = 0;
    total_write_words_lo = 0;

    publish_result(0);
    draw_status();
}

static void update()
{
    igs023_wait_vblank();
    if (!failed_once)
    {
        run_batch();
    }
    draw_status();
}

PAGE_REGISTER(vram_seq_write_test, init, update, NULL);
