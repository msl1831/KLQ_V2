#ifndef CS100A_H
#define CS100A_H

#include <stdint.h>

typedef enum {
    CS100A_SAMPLE_NONE = 0U,
    CS100A_SAMPLE_VALID = 1U,
    CS100A_SAMPLE_TOO_CLOSE = 2U,
    CS100A_SAMPLE_OUT_OF_RANGE = 3U,
    CS100A_SAMPLE_NO_ECHO = 4U,
    CS100A_SAMPLE_HW_FAULT = 5U
} cs100a_sample_status_t;

typedef enum {
    CS100A_STATE_IDLE = 0U,
    CS100A_STATE_TRIGGER = 1U,
    CS100A_STATE_WAIT_RISE = 2U,
    CS100A_STATE_MEASURE_HIGH = 3U,
    CS100A_STATE_GUARD = 4U
} cs100a_state_t;

typedef struct {
    uint32_t sequence;
    uint32_t capture_time_ms;
    uint16_t echo_ticks;
    uint16_t distance_mm;
    uint8_t status;
    uint8_t quality_flags;
} cs100a_sample_t;

void cs100a_init(void);
void cs100a_poll(uint32_t now_ms);
void cs100a_trigger_finished_irq(void);
void cs100a_echo_edge_irq(uint16_t capture);
void cs100a_get_sample(cs100a_sample_t *sample);
uint8_t cs100a_state(void);
uint32_t cs100a_measurement_count(void);
uint16_t cs100a_timeout_count(void);

#endif
