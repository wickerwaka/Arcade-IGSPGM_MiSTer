#include "serial_audio_capture.h"

#include <string.h>

#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#define SERIAL_AUDIO_CAPTURE_DMA_WORDS_PER_BLOCK 256u
#define SERIAL_AUDIO_CAPTURE_DMA_BLOCK_COUNT 2u

static serial_audio_capture_config_t capture_config;
static stereo_ring_buffer_t *capture_buffer;
static bool capture_running;

static PIO capture_pio = pio0;
static uint capture_sm;
static int capture_dma_chan = -1;
static uint8_t dma_active_block;
static volatile uint8_t dma_ready_mask;
static uint32_t dma_blocks[SERIAL_AUDIO_CAPTURE_DMA_BLOCK_COUNT][SERIAL_AUDIO_CAPTURE_DMA_WORDS_PER_BLOCK];

static bool parser_synced;
static bool parser_seen_transition;
static bool parser_current_lrclk;
static uint16_t parser_shift_reg;
static uint32_t parser_bit_count;
static int16_t pending_left_sample;
static int16_t pending_right_sample;
static bool pending_left_valid;
static bool pending_right_valid;

static volatile uint32_t dropped_dma_blocks;
static volatile uint32_t dropped_audio_frames;

static bool serial_audio_capture_config_valid(void) {
    return capture_buffer && capture_config.bits_per_sample == 16u &&
           capture_config.si_gpio == (capture_config.lrclk_gpio + 1u);
}

static inline bool lrclk_state_is_left(bool lrclk_high) {
    return capture_config.lrclk_low_is_left ? !lrclk_high : lrclk_high;
}

static inline int16_t sign_extend_sample(uint16_t sample) {
    return (int16_t)sample;
}

static void queue_audio_frame(int16_t left, int16_t right) {
    if (!capture_buffer) {
        return;
    }

    stereo_frame_t frame = {
        .left = left,
        .right = right,
    };

    if (stereo_ring_buffer_write(capture_buffer, &frame, 1) != 1) {
        dropped_audio_frames++;
    }
}

static void finalize_channel_sample(bool lrclk_high, uint16_t sample) {
    if (!parser_seen_transition) {
        parser_seen_transition = true;
        return;
    }

    if (lrclk_state_is_left(lrclk_high)) {
        pending_left_sample = sign_extend_sample(sample);
        pending_left_valid = true;
    } else {
        pending_right_sample = sign_extend_sample(sample);
        pending_right_valid = true;
    }

    if (pending_left_valid && pending_right_valid) {
        queue_audio_frame(pending_left_sample, pending_right_sample);
        pending_left_valid = false;
        pending_right_valid = false;
    }
}

static void process_packed_sample(uint8_t packed_sample) {
    bool lrclk_high = (packed_sample & 0x1u) != 0;
    bool serial_high = (packed_sample & 0x2u) != 0;

    if (!parser_synced) {
        parser_synced = true;
        parser_current_lrclk = lrclk_high;
        parser_shift_reg = (uint16_t)serial_high;
        parser_bit_count = 1;
        return;
    }

    if (lrclk_high != parser_current_lrclk) {
        finalize_channel_sample(parser_current_lrclk, parser_shift_reg);
        parser_current_lrclk = lrclk_high;
        parser_shift_reg = (uint16_t)serial_high;
        parser_bit_count = 1;
        return;
    }

    parser_shift_reg = (uint16_t)((parser_shift_reg << 1) | (serial_high ? 1u : 0u));
    parser_bit_count++;
}

static void process_dma_word(uint32_t word) {
    for (int shift = 30; shift >= 0; shift -= 2) {
        process_packed_sample((uint8_t)((word >> (uint32_t)shift) & 0x3u));
    }
}

static void process_dma_block(const uint32_t *block) {
    for (uint32_t i = 0; i < SERIAL_AUDIO_CAPTURE_DMA_WORDS_PER_BLOCK; ++i) {
        process_dma_word(block[i]);
    }
}

static void serial_audio_capture_dma_irq_handler(void) {
    if (capture_dma_chan < 0) {
        return;
    }

    uint32_t mask = 1u << (uint32_t)capture_dma_chan;
    if ((dma_hw->ints0 & mask) == 0) {
        return;
    }

    dma_hw->ints0 = mask;

    uint8_t completed_block = dma_active_block;
    dma_ready_mask |= (uint8_t)(1u << completed_block);

    dma_active_block ^= 1u;
    if ((dma_ready_mask & (1u << dma_active_block)) != 0) {
        dropped_dma_blocks++;
    }

    dma_channel_set_write_addr(capture_dma_chan, dma_blocks[dma_active_block], false);
    dma_channel_set_trans_count(capture_dma_chan, SERIAL_AUDIO_CAPTURE_DMA_WORDS_PER_BLOCK, true);
}

static uint16_t capture_program_instructions[8];
static const struct pio_program capture_program = {
    .instructions = capture_program_instructions,
    .length = 8,
    .origin = -1,
};

static void build_capture_program(uint clk_gpio) {
    capture_program_instructions[0] = pio_encode_wait_gpio(false, clk_gpio);
    capture_program_instructions[1] = pio_encode_wait_gpio(true, clk_gpio);
    capture_program_instructions[2] = pio_encode_in(pio_pins, 2);
    capture_program_instructions[3] = pio_encode_jmp(0);
    capture_program_instructions[4] = pio_encode_wait_gpio(true, clk_gpio);
    capture_program_instructions[5] = pio_encode_wait_gpio(false, clk_gpio);
    capture_program_instructions[6] = pio_encode_in(pio_pins, 2);
    capture_program_instructions[7] = pio_encode_jmp(4);
}

static void serial_audio_capture_start_dma(void) {
    dma_channel_config dma_config = dma_channel_get_default_config((uint)capture_dma_chan);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_config, false);
    channel_config_set_write_increment(&dma_config, true);
    channel_config_set_dreq(&dma_config, pio_get_dreq(capture_pio, capture_sm, false));

    dma_channel_configure(capture_dma_chan,
                          &dma_config,
                          dma_blocks[0],
                          &capture_pio->rxf[capture_sm],
                          SERIAL_AUDIO_CAPTURE_DMA_WORDS_PER_BLOCK,
                          false);

    dma_channel_acknowledge_irq0((uint)capture_dma_chan);
    dma_channel_set_irq0_enabled((uint)capture_dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, serial_audio_capture_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    dma_active_block = 0;
    dma_ready_mask = 0;
    dma_channel_start((uint)capture_dma_chan);
}

void serial_audio_capture_init(const serial_audio_capture_config_t *config, stereo_ring_buffer_t *target_buffer) {
    capture_config = *config;
    capture_buffer = target_buffer;
    capture_running = false;
    parser_synced = false;
    parser_seen_transition = false;
    parser_current_lrclk = false;
    parser_shift_reg = 0;
    parser_bit_count = 0;
    pending_left_sample = 0;
    pending_right_sample = 0;
    pending_left_valid = false;
    pending_right_valid = false;
    dropped_dma_blocks = 0;
    dropped_audio_frames = 0;
    capture_dma_chan = -1;

    if (!serial_audio_capture_config_valid()) {
        return;
    }

    gpio_init(capture_config.clk_gpio);
    gpio_set_dir(capture_config.clk_gpio, GPIO_IN);
    gpio_disable_pulls(capture_config.clk_gpio);
    gpio_init(capture_config.lrclk_gpio);
    gpio_set_dir(capture_config.lrclk_gpio, GPIO_IN);
    gpio_disable_pulls(capture_config.lrclk_gpio);
    gpio_init(capture_config.si_gpio);
    gpio_set_dir(capture_config.si_gpio, GPIO_IN);
    gpio_disable_pulls(capture_config.si_gpio);

}

void serial_audio_capture_enable(void) {
    if (capture_running || !serial_audio_capture_config_valid()) {
        return;
    }

    build_capture_program(capture_config.clk_gpio);

    capture_sm = pio_claim_unused_sm(capture_pio, true);
    uint capture_offset = pio_add_program(capture_pio, &capture_program);

    pio_gpio_init(capture_pio, capture_config.clk_gpio);
    pio_gpio_init(capture_pio, capture_config.lrclk_gpio);
    pio_gpio_init(capture_pio, capture_config.si_gpio);

    pio_sm_config sm_config = pio_get_default_sm_config();
    sm_config_set_in_pins(&sm_config, capture_config.lrclk_gpio);
    sm_config_set_in_shift(&sm_config, false, true, 32);
    sm_config_set_fifo_join(&sm_config, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&sm_config, 1.0f);

    uint entry_offset = capture_offset + (capture_config.sample_on_rising_edge ? 0u : 4u);
    sm_config_set_wrap(&sm_config, entry_offset, entry_offset + 3u);
    pio_sm_init(capture_pio, capture_sm, entry_offset, &sm_config);
    pio_sm_set_consecutive_pindirs(capture_pio, capture_sm, capture_config.lrclk_gpio, 2, false);
    pio_sm_set_enabled(capture_pio, capture_sm, true);

    capture_dma_chan = dma_claim_unused_channel(true);
    serial_audio_capture_start_dma();
    capture_running = true;
}

void serial_audio_capture_task(void) {
    while (dma_ready_mask != 0) {
        uint8_t ready_mask = dma_ready_mask;
        uint8_t block_index = (ready_mask & 0x1u) ? 0u : 1u;
        dma_ready_mask &= (uint8_t)~(1u << block_index);
        process_dma_block(dma_blocks[block_index]);
    }
}

bool serial_audio_capture_is_running(void) {
    return capture_running;
}

uint32_t serial_audio_capture_get_dropped_dma_blocks(void) {
    return dropped_dma_blocks;
}

uint32_t serial_audio_capture_get_dropped_audio_frames(void) {
    return dropped_audio_frames;
}
