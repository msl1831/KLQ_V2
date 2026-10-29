#include "cs100a.h"
#include "board.h"
#include "py32f0xx.h"

#define CS100A_TIMEOUT_MS       70U
#define CS100A_GUARD_MS         1U
#define CS100A_MIN_DISTANCE_MM  20U
#define CS100A_MAX_ECHO_TICKS   33000U

static volatile cs100a_state_t state;
static volatile uint16_t rise_capture;
static volatile uint32_t state_started_ms;
static volatile cs100a_sample_t latest;
static volatile uint32_t measurement_counter;
static volatile uint16_t timeout_counter;

static void publish(uint16_t ticks, uint8_t status, uint32_t now_ms)
{
    uint32_t distance = ((uint32_t)ticks * 34U + 50U) / 100U;
    latest.sequence++;
    latest.capture_time_ms = now_ms;
    latest.echo_ticks = ticks;
    latest.distance_mm = (uint16_t)distance;
    latest.status = status;
    latest.quality_flags = 0U;
    ++measurement_counter;
    state_started_ms = now_ms;
    state = CS100A_STATE_GUARD;
    board_echo_expect_rising();
}

void cs100a_init(void)
{
    state = CS100A_STATE_IDLE;
    rise_capture = 0U;
    state_started_ms = board_millis();
    latest.sequence = 0U;
    latest.capture_time_ms = 0U;
    latest.echo_ticks = 0U;
    latest.distance_mm = 0U;
    latest.status = CS100A_SAMPLE_NONE;
    latest.quality_flags = 0U;
    measurement_counter = 0U;
    timeout_counter = 0U;
    board_echo_expect_rising();
}

void cs100a_poll(uint32_t now_ms)
{
    cs100a_state_t current = state;
    if ((current == CS100A_STATE_IDLE) ||
        ((current == CS100A_STATE_GUARD) &&
         ((uint32_t)(now_ms - state_started_ms) >= CS100A_GUARD_MS))) {
        state = CS100A_STATE_TRIGGER;
        state_started_ms = now_ms;
        board_trigger_pulse();
    } else if ((current == CS100A_STATE_WAIT_RISE) &&
               ((uint32_t)(now_ms - state_started_ms) >= CS100A_TIMEOUT_MS)) {
        ++timeout_counter;
        publish(0U, CS100A_SAMPLE_NO_ECHO, now_ms);
    } else if ((current == CS100A_STATE_MEASURE_HIGH) &&
               ((uint32_t)(now_ms - state_started_ms) >= CS100A_TIMEOUT_MS)) {
        ++timeout_counter;
        publish(CS100A_MAX_ECHO_TICKS, CS100A_SAMPLE_OUT_OF_RANGE, now_ms);
    }
}

void cs100a_trigger_finished_irq(void)
{
    if (state == CS100A_STATE_TRIGGER) {
        state_started_ms = board_millis();
        state = CS100A_STATE_WAIT_RISE;
    }
}

void cs100a_echo_edge_irq(uint16_t capture)
{
    if (state == CS100A_STATE_WAIT_RISE) {
        rise_capture = capture;
        state_started_ms = board_millis();
        state = CS100A_STATE_MEASURE_HIGH;
        board_echo_expect_falling();
    } else if (state == CS100A_STATE_MEASURE_HIGH) {
        uint16_t ticks = (uint16_t)(capture - rise_capture);
        uint16_t distance = (uint16_t)(((uint32_t)ticks * 34U + 50U) / 100U);
        uint8_t status = CS100A_SAMPLE_VALID;
        if (ticks >= CS100A_MAX_ECHO_TICKS) {
            status = CS100A_SAMPLE_OUT_OF_RANGE;
        } else if (distance < CS100A_MIN_DISTANCE_MM) {
            status = CS100A_SAMPLE_TOO_CLOSE;
        }
        publish(ticks, status, board_millis());
    }
}

void cs100a_get_sample(cs100a_sample_t *sample)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    *sample = latest;
    if (primask == 0U) {
        __enable_irq();
    }
}

uint8_t cs100a_state(void)
{
    return (uint8_t)state;
}

uint32_t cs100a_measurement_count(void)
{
    return measurement_counter;
}

uint16_t cs100a_timeout_count(void)
{
    return timeout_counter;
}
