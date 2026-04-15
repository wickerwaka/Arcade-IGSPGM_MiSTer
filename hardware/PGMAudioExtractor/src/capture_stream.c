#include "capture_stream.h"

#include <string.h>

#include "tusb.h"

#define CAPTURE_STREAM_PACKET_QUEUE_LEN 32u
#define CAPTURE_STREAM_MAX_AUDIO_FRAMES 128u
#define CAPTURE_STREAM_MAX_PAYLOAD_BYTES (CAPTURE_STREAM_MAX_AUDIO_FRAMES * sizeof(stereo_frame_t))
#define CAPTURE_STREAM_MAX_PACKET_BYTES (sizeof(pgm_capture_packet_header_t) + CAPTURE_STREAM_MAX_PAYLOAD_BYTES)

typedef struct {
    uint16_t total_bytes;
    uint16_t tx_offset;
    uint8_t data[CAPTURE_STREAM_MAX_PACKET_BYTES];
} capture_stream_packet_t;

static capture_stream_packet_t packet_queue[CAPTURE_STREAM_PACKET_QUEUE_LEN];
static uint8_t queue_head;
static uint8_t queue_tail;
static uint8_t queue_count;
static bool was_connected;
static uint32_t dropped_packets;
static uint32_t dropped_bytes;

static inline bool queue_full(void) {
    return queue_count >= CAPTURE_STREAM_PACKET_QUEUE_LEN;
}

static inline bool queue_empty(void) {
    return queue_count == 0u;
}

static inline capture_stream_packet_t *queue_peek_head(void) {
    return queue_empty() ? NULL : &packet_queue[queue_head];
}

static inline capture_stream_packet_t *queue_peek_tail(void) {
    return queue_full() ? NULL : &packet_queue[queue_tail];
}

static inline void queue_push_complete(void) {
    queue_tail = (uint8_t)((queue_tail + 1u) % CAPTURE_STREAM_PACKET_QUEUE_LEN);
    queue_count++;
}

static inline void queue_pop(void) {
    if (queue_empty()) {
        return;
    }
    queue_head = (uint8_t)((queue_head + 1u) % CAPTURE_STREAM_PACKET_QUEUE_LEN);
    queue_count--;
}

static bool stream_ready(void) {
    return tud_mounted() && tud_cdc_connected();
}

static void clear_queue(void) {
    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;
}

static void drop_packet_bytes(uint32_t bytes) {
    dropped_packets++;
    dropped_bytes += bytes;
}

static bool reserve_packet(uint16_t total_bytes, capture_stream_packet_t **packet) {
    if (!stream_ready()) {
        drop_packet_bytes(total_bytes);
        return false;
    }

    if (queue_full()) {
        drop_packet_bytes(total_bytes);
        return false;
    }

    capture_stream_packet_t *slot = queue_peek_tail();
    if (!slot) {
        drop_packet_bytes(total_bytes);
        return false;
    }

    slot->total_bytes = total_bytes;
    slot->tx_offset = 0;
    *packet = slot;
    return true;
}

void capture_stream_init(void) {
    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;
    was_connected = false;
    dropped_packets = 0;
    dropped_bytes = 0;
}

void capture_stream_reset(void) {
    clear_queue();
}

bool capture_stream_connected(void) {
    return stream_ready();
}

void capture_stream_submit_audio(uint32_t block_seq,
                                 uint64_t frame_start,
                                 uint64_t t_us,
                                 uint32_t raw_lrclk_hz,
                                 uint32_t flags,
                                 const stereo_frame_t *frames,
                                 uint32_t frame_count) {
    if (!frames || frame_count == 0u || frame_count > CAPTURE_STREAM_MAX_AUDIO_FRAMES) {
        return;
    }

    uint32_t payload_bytes = frame_count * (uint32_t)sizeof(stereo_frame_t);
    uint16_t total_bytes = (uint16_t)(sizeof(pgm_capture_packet_header_t) + payload_bytes);
    capture_stream_packet_t *packet = NULL;
    if (!reserve_packet(total_bytes, &packet)) {
        return;
    }

    pgm_capture_packet_header_t header = {
        .magic = PGM_CAPTURE_MAGIC,
        .version = PGM_CAPTURE_PROTOCOL_VERSION,
        .type = PGM_CAPTURE_PACKET_TYPE_AUDIO,
        .payload_bytes = payload_bytes,
        .block_seq = block_seq,
        .frame_start = frame_start,
        .frame_count = frame_count,
        .t_us = t_us,
        .raw_lrclk_hz = raw_lrclk_hz,
        .flags = flags,
    };

    memcpy(packet->data, &header, sizeof(header));
    memcpy(packet->data + sizeof(header), frames, payload_bytes);
    queue_push_complete();
}

void capture_stream_submit_status(uint32_t block_seq,
                                  uint64_t t_us,
                                  uint32_t flags,
                                  const pgm_capture_status_payload_t *status) {
    if (!status) {
        return;
    }

    uint32_t payload_bytes = (uint32_t)sizeof(*status);
    uint16_t total_bytes = (uint16_t)(sizeof(pgm_capture_packet_header_t) + payload_bytes);
    capture_stream_packet_t *packet = NULL;
    if (!reserve_packet(total_bytes, &packet)) {
        return;
    }

    pgm_capture_packet_header_t header = {
        .magic = PGM_CAPTURE_MAGIC,
        .version = PGM_CAPTURE_PROTOCOL_VERSION,
        .type = PGM_CAPTURE_PACKET_TYPE_STATUS,
        .payload_bytes = payload_bytes,
        .block_seq = block_seq,
        .frame_start = 0,
        .frame_count = 0,
        .t_us = t_us,
        .raw_lrclk_hz = status->raw_rate_hz,
        .flags = flags,
    };

    memcpy(packet->data, &header, sizeof(header));
    memcpy(packet->data + sizeof(header), status, sizeof(*status));
    queue_push_complete();
}

void capture_stream_task(void) {
    bool connected = stream_ready();
    if (!connected) {
        if (was_connected) {
            clear_queue();
        }
        was_connected = false;
        return;
    }

    was_connected = true;

    capture_stream_packet_t *packet = queue_peek_head();
    if (!packet) {
        return;
    }

    uint32_t remaining = (uint32_t)packet->total_bytes - packet->tx_offset;
    if (remaining == 0u) {
        queue_pop();
        return;
    }

    uint32_t writable = tud_cdc_write_available();
    if (writable == 0u) {
        return;
    }

    uint32_t chunk = remaining < writable ? remaining : writable;
    uint32_t written = tud_cdc_write(packet->data + packet->tx_offset, chunk);
    packet->tx_offset = (uint16_t)(packet->tx_offset + written);
    tud_cdc_write_flush();

    if (packet->tx_offset >= packet->total_bytes) {
        queue_pop();
    }
}

uint32_t capture_stream_get_dropped_packets(void) {
    return dropped_packets;
}

uint32_t capture_stream_get_dropped_bytes(void) {
    return dropped_bytes;
}

uint32_t capture_stream_get_queue_depth(void) {
    return queue_count;
}
