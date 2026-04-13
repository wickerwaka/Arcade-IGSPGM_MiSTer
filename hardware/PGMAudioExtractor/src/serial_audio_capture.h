#ifndef SERIAL_AUDIO_CAPTURE_H
#define SERIAL_AUDIO_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "ring_buffer.h"

typedef struct {
    uint32_t clk_gpio;
    uint32_t lrclk_gpio;
    uint32_t si_gpio;
    bool lrclk_low_is_left;
    bool sample_on_rising_edge;
    uint8_t bits_per_sample;
} serial_audio_capture_config_t;

void serial_audio_capture_init(const serial_audio_capture_config_t *config, stereo_ring_buffer_t *target_buffer);
void serial_audio_capture_enable(void);
void serial_audio_capture_task(void);
bool serial_audio_capture_is_running(void);
uint32_t serial_audio_capture_get_processed_dma_blocks(void);
uint32_t serial_audio_capture_get_ready_mask(void);
uint32_t serial_audio_capture_get_dropped_dma_blocks(void);
uint32_t serial_audio_capture_get_dropped_audio_frames(void);
uint32_t serial_audio_capture_get_channel_word_count(void);
uint32_t serial_audio_capture_get_stereo_frame_count(void);
uint32_t serial_audio_capture_get_nonzero_sample_count(void);
int16_t serial_audio_capture_get_last_left_sample(void);
int16_t serial_audio_capture_get_last_right_sample(void);

#endif
