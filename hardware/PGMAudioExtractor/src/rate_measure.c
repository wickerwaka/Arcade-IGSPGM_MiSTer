#include "rate_measure.h"

#include "hardware/gpio.h"
#include "pico/time.h"

#ifndef PGM_AUDIO_MIN_SAMPLE_RATE
#define PGM_AUDIO_MIN_SAMPLE_RATE 32000u
#endif

#ifndef PGM_AUDIO_MAX_SAMPLE_RATE
#define PGM_AUDIO_MAX_SAMPLE_RATE 44100u
#endif

#ifndef PGM_AUDIO_NOMINAL_SAMPLE_RATE
#define PGM_AUDIO_NOMINAL_SAMPLE_RATE 44100u
#endif

static uint32_t current_rate_hz;
static uint32_t raw_rate_hz;
static bool current_rate_valid;
static uint32_t last_elapsed_us;
static uint32_t last_idle_us;
static uint32_t last_edge_count_snapshot;
static rate_measure_status_t current_status;
static uint32_t measured_gpio;
static volatile uint32_t last_edge_us;
static volatile uint32_t first_edge_us;
static volatile uint32_t edge_count;
static bool irq_enabled;

static void rate_measure_gpio_irq(uint gpio, uint32_t events) {
    if (gpio != measured_gpio || !(events & GPIO_IRQ_EDGE_RISE)) {
        return;
    }

    uint32_t now_us = time_us_32();
    if (last_edge_us != 0) {
        uint32_t period_us = now_us - last_edge_us;
        if (period_us >= 8u && period_us <= 200u) {
            if (first_edge_us == 0) {
                first_edge_us = last_edge_us;
                edge_count = 1;
            }
            edge_count++;
        } else {
            first_edge_us = 0;
            edge_count = 0;
        }
    }
    last_edge_us = now_us;
}

static uint32_t clamp_rate(uint32_t hz) {
    if (hz < PGM_AUDIO_MIN_SAMPLE_RATE) {
        return PGM_AUDIO_MIN_SAMPLE_RATE;
    }
    if (hz > PGM_AUDIO_MAX_SAMPLE_RATE) {
        return PGM_AUDIO_MAX_SAMPLE_RATE;
    }
    return hz;
}

void rate_measure_init(uint32_t lrclk_gpio) {
    current_rate_hz = PGM_AUDIO_NOMINAL_SAMPLE_RATE;
    raw_rate_hz = 0;
    current_rate_valid = false;
    last_elapsed_us = 0;
    last_idle_us = 0;
    last_edge_count_snapshot = 0;
    current_status = RATE_MEASURE_STATUS_WAITING_FOR_EDGES;
    measured_gpio = lrclk_gpio;
    last_edge_us = 0;
    first_edge_us = 0;
    edge_count = 0;
    irq_enabled = false;

    gpio_init(measured_gpio);
    gpio_set_dir(measured_gpio, GPIO_IN);
    gpio_disable_pulls(measured_gpio);
    gpio_set_irq_enabled(measured_gpio, GPIO_IRQ_EDGE_RISE, false);
}

void rate_measure_enable(void) {
    if (irq_enabled) {
        return;
    }

    last_edge_us = 0;
    first_edge_us = 0;
    edge_count = 0;
    gpio_set_irq_enabled_with_callback(measured_gpio, GPIO_IRQ_EDGE_RISE, true, &rate_measure_gpio_irq);
    irq_enabled = true;
}

void rate_measure_task(void) {
    uint32_t snapshot_first_edge_us = first_edge_us;
    uint32_t snapshot_last_edge_us = last_edge_us;
    uint32_t snapshot_edge_count = edge_count;

    last_edge_count_snapshot = snapshot_edge_count;
    last_elapsed_us = (snapshot_first_edge_us != 0 && snapshot_last_edge_us >= snapshot_first_edge_us)
                           ? (snapshot_last_edge_us - snapshot_first_edge_us)
                           : 0;
    last_idle_us = (snapshot_last_edge_us != 0) ? (time_us_32() - snapshot_last_edge_us) : 0;
    raw_rate_hz = 0;

    if (snapshot_last_edge_us == 0) {
        current_rate_valid = false;
        current_status = RATE_MEASURE_STATUS_WAITING_FOR_EDGES;
        return;
    }

    if (snapshot_first_edge_us == 0 || snapshot_edge_count < 32) {
        current_rate_valid = false;
        current_status = RATE_MEASURE_STATUS_TOO_FEW_EDGES;
        return;
    }

    uint32_t idle_us = last_idle_us;
    uint32_t elapsed_us = last_elapsed_us;
    if (elapsed_us == 0) {
        current_rate_valid = false;
        current_status = RATE_MEASURE_STATUS_ZERO_ELAPSED;
        return;
    }

    if (idle_us > 5000u) {
        current_rate_valid = false;
        current_status = RATE_MEASURE_STATUS_IDLE_TIMEOUT;
        return;
    }

    uint32_t measured_hz = (uint32_t)(((uint64_t)(snapshot_edge_count - 1u) * 1000000u) / elapsed_us);
    raw_rate_hz = measured_hz;
    current_rate_hz = clamp_rate(measured_hz);
    current_rate_valid = true;
    current_status = RATE_MEASURE_STATUS_OK;
}

void rate_measure_set_mock_hz(uint32_t sample_rate_hz) {
    raw_rate_hz = sample_rate_hz;
    current_rate_hz = clamp_rate(sample_rate_hz);
    current_rate_valid = true;
    last_elapsed_us = 0;
    last_idle_us = 0;
    last_edge_count_snapshot = 0;
    current_status = RATE_MEASURE_STATUS_OK;
}

uint32_t rate_measure_get_hz(void) {
    return current_rate_hz;
}

uint32_t rate_measure_get_raw_hz(void) {
    return raw_rate_hz;
}

uint32_t rate_measure_get_edge_count(void) {
    return last_edge_count_snapshot;
}

uint32_t rate_measure_get_elapsed_us(void) {
    return last_elapsed_us;
}

uint32_t rate_measure_get_idle_us(void) {
    return last_idle_us;
}

rate_measure_status_t rate_measure_get_status(void) {
    return current_status;
}

bool rate_measure_is_valid(void) {
    return current_rate_valid;
}
