#include "ir_sensor.h"
#include "board.h"
#include <string.h>

static ir_sample_t latest;
static uint16_t raw[4], sum, timeout_counter;
static uint32_t state_at, cycle_started, measurement_counter;
static uint8_t state, phase, count, adc_ok;

static void fault(uint32_t now_ms)
{
    uint32_t sequence = latest.sequence + 1U;
    board_emitters_off();
    board_adc_abort();
    memset(&latest, 0, sizeof(latest));
    latest.sequence = sequence;
    latest.capture_time_ms = now_ms;
    latest.status = IR_SAMPLE_HW_FAULT;
    ++timeout_counter;
    adc_ok = 0U;
    state = IR_STATE_FAULT;
    state_at = now_ms;
}

static uint16_t normalize(int16_t delta)
{
    if (delta <= 0) return 0U;
    return (uint16_t)(((uint32_t)delta * 1000U + 2047U) / 4095U);
}

static void publish(void)
{
    uint8_t flags = 0U;
    latest.left_ambient = raw[0];
    latest.left_active = raw[1];
    latest.right_ambient = raw[2];
    latest.right_active = raw[3];
    latest.left_delta = (int16_t)((int32_t)raw[0] - (int32_t)raw[1]);
    latest.right_delta = (int16_t)((int32_t)raw[2] - (int32_t)raw[3]);
    latest.left_value = normalize(latest.left_delta);
    latest.right_value = normalize(latest.right_delta);
    if (raw[0] <= 8U) flags |= IR_QUALITY_LEFT_AMBIENT_LOW;
    if (raw[2] <= 8U) flags |= IR_QUALITY_RIGHT_AMBIENT_LOW;
    if (raw[1] <= 8U) flags |= IR_QUALITY_LEFT_ACTIVE_LOW;
    if (raw[3] <= 8U) flags |= IR_QUALITY_RIGHT_ACTIVE_LOW;
    if (latest.left_delta < 0) flags |= IR_QUALITY_LEFT_NEGATIVE;
    if (latest.right_delta < 0) flags |= IR_QUALITY_RIGHT_NEGATIVE;
    latest.quality_flags = flags;
    latest.status = flags != 0U ? IR_SAMPLE_DEGRADED : IR_SAMPLE_VALID;
    latest.capture_time_ms = board_millis();
    ++latest.sequence;
    ++measurement_counter;
}

void ir_sensor_init(void)
{
    uint32_t now_ms;
    board_emitters_off();
    memset(&latest, 0, sizeof(latest));
    measurement_counter = 0U;
    timeout_counter = 0U;
    state = IR_STATE_IDLE;
    phase = count = 0U;
    sum = 0U;
    adc_ok = board_adc_init();
    now_ms = board_millis();
    cycle_started = now_ms - IR_SAMPLE_PERIOD_MS;
    if (adc_ok == 0U) fault(now_ms);
}

void ir_sensor_poll(uint32_t now_ms)
{
    uint16_t value;
    uint8_t result;
    if (state == IR_STATE_FAULT) {
        if ((uint32_t)(now_ms - state_at) < IR_FAULT_RETRY_MS) return;
        adc_ok = board_adc_init();
        now_ms = board_millis();
        if (adc_ok == 0U) { fault(now_ms); return; }
        state = IR_STATE_IDLE;
        cycle_started = now_ms - IR_SAMPLE_PERIOD_MS;
    }
    if (state == IR_STATE_IDLE) {
        if ((uint32_t)(now_ms - cycle_started) < IR_SAMPLE_PERIOD_MS) return;
        cycle_started = now_ms;
        phase = 0U;
        board_emitters_off();
        state_at = board_millis();
        state = IR_STATE_SETTLE;
        return;
    }
    if (state == IR_STATE_SETTLE) {
        if ((uint32_t)(now_ms - state_at) < IR_SETTLE_MS) return;
        count = 0U;
        sum = 0U;
        if (board_adc_start((uint8_t)(phase >> 1)) == 0U) {
            fault(board_millis());
            return;
        }
        state_at = board_millis();
        state = IR_STATE_CONVERT;
        return;
    }
    result = board_adc_poll(&value);
    if (result == 2U || (result == 0U &&
        (uint32_t)(now_ms - state_at) >= IR_ADC_TIMEOUT_MS)) {
        fault(board_millis());
        return;
    }
    if (result == 0U) return;
    /* First conversion after each lamp/channel change is discarded. */
    if (count != 0U) sum = (uint16_t)(sum + value);
    if (++count < 5U) {
        if (board_adc_start((uint8_t)(phase >> 1)) == 0U) {
            fault(board_millis());
            return;
        }
        state_at = board_millis();
        return;
    }
    raw[phase] = (uint16_t)((sum + 2U) >> 2);
    ++phase;
    if (phase == 4U) {
        board_emitters_off();
        publish();
        state = IR_STATE_IDLE;
    } else {
        board_emitter_set(phase == 1U ? 1U : (phase == 3U ? 2U : 0U));
        state_at = board_millis();
        state = IR_STATE_SETTLE;
    }
}

void ir_sensor_get_sample(ir_sample_t *sample) { *sample = latest; }
uint8_t ir_sensor_state(void) { return state; }
uint32_t ir_sensor_measurement_count(void) { return measurement_counter; }
uint16_t ir_sensor_timeout_count(void) { return timeout_counter; }
uint8_t ir_sensor_adc_ok(void) { return adc_ok; }
