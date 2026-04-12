#ifndef RATE_MEASURE_H
#define RATE_MEASURE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RATE_MEASURE_STATUS_OK = 0,
    RATE_MEASURE_STATUS_WAITING_FOR_EDGES,
    RATE_MEASURE_STATUS_TOO_FEW_EDGES,
    RATE_MEASURE_STATUS_IDLE_TIMEOUT,
    RATE_MEASURE_STATUS_ZERO_ELAPSED,
} rate_measure_status_t;

void rate_measure_init(uint32_t lrclk_gpio);
void rate_measure_enable(void);
void rate_measure_task(void);
void rate_measure_set_mock_hz(uint32_t sample_rate_hz);
uint32_t rate_measure_get_hz(void);
uint32_t rate_measure_get_raw_hz(void);
uint32_t rate_measure_get_edge_count(void);
uint32_t rate_measure_get_elapsed_us(void);
uint32_t rate_measure_get_idle_us(void);
rate_measure_status_t rate_measure_get_status(void);
bool rate_measure_is_valid(void);

#endif
