#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "ir_sensor.h"

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("sampling failed line %u: %s\n", __LINE__, #x); exit(1); } } while (0)
static uint32_t now;
static uint16_t readings[2][2];
static uint8_t lamp, channel, pending, adc_mode, init_ok;
static unsigned phase_key, phase_reads, dummy_count, aborts;

uint32_t board_millis(void) { return now; }
void board_emitters_off(void) { lamp = 0U; }
void board_emitter_set(uint8_t selection) { CHECK(selection <= 2U); lamp = selection; }
uint8_t board_adc_init(void) { return init_ok; }
void board_adc_abort(void) { pending = 0U; ++aborts; }
uint8_t board_adc_start(uint8_t value)
{
    unsigned key;
    CHECK(!pending && value < 2U);
    CHECK(lamp == 0U || lamp == value + 1U);
    if (adc_mode == 1U) return 0U;
    channel = value;
    key = (unsigned)channel * 3U + lamp;
    if (key != phase_key) { phase_key = key; phase_reads = 0U; }
    ++phase_reads;
    pending = 1U;
    return 1U;
}
uint8_t board_adc_poll(uint16_t *value)
{
    CHECK(pending);
    if (adc_mode == 2U) return 0U;
    pending = 0U;
    if (adc_mode == 3U) return 2U;
    if (phase_reads == 1U) { *value = 123U; ++dummy_count; }
    else *value = readings[channel][lamp ? 1 : 0];
    return 1U;
}

static void step(void)
{
    unsigned i;
    ++now;
    for (i = 0; i < 16U; ++i) ir_sensor_poll(now);
}
static ir_sample_t snapshot(void) { ir_sample_t s; ir_sensor_get_sample(&s); return s; }
static void setup(uint32_t time)
{
    now = time; lamp = pending = adc_mode = 0U; init_ok = 1U;
    phase_key = 99U; phase_reads = dummy_count = aborts = 0U;
    readings[0][0] = 3000U; readings[0][1] = 1000U;
    readings[1][0] = 2000U; readings[1][1] = 1500U;
    ir_sensor_init();
    CHECK(snapshot().sequence == 0U && snapshot().status == 0U && lamp == 0U);
}
static ir_sample_t next_sample(uint32_t sequence, unsigned limit)
{
    ir_sample_t s;
    do { step(); s = snapshot(); } while (s.sequence == sequence && --limit);
    CHECK(s.sequence != sequence);
    return s;
}
static void healthy_pair(void)
{
    ir_sample_t s, old;
    unsigned i;
    setup(0U);
    step(); CHECK(snapshot().sequence == 0U);
    s = next_sample(0U, 30U);
    CHECK(s.status == 1U && !s.quality_flags && !lamp);
    CHECK(s.left_ambient == 3000U && s.left_active == 1000U);
    CHECK(s.right_ambient == 2000U && s.right_active == 1500U);
    CHECK(s.left_delta == 2000 && s.right_delta == 500);
    CHECK(s.left_value == 488U && s.right_value == 122U);
    CHECK(dummy_count == 4U && ir_sensor_measurement_count() == 1U);
    old = s;
    readings[0][0] = 4095U; readings[0][1] = 0U;
    readings[1][0] = 1000U; readings[1][1] = 1500U;
    for (i = 0; i < 3U; ++i) { step(); s = snapshot(); if (s.sequence == old.sequence) CHECK(!memcmp(&s, &old, sizeof(s))); }
    s = next_sample(old.sequence, 30U);
    CHECK(s.left_delta == 4095 && s.left_value == 1000U);
    CHECK(s.right_delta == -500 && s.right_value == 0U);
    CHECK(s.status == 2U && s.quality_flags == ((1U << 2) | (1U << 5)));
    CHECK(ir_sensor_measurement_count() == 2U);
}
static void quality_and_rollover(void)
{
    ir_sample_t s;
    setup(0xfffffffcu);
    readings[0][0] = readings[0][1] = 8U;
    readings[1][0] = readings[1][1] = 0U;
    s = next_sample(0U, 30U);
    CHECK(s.capture_time_ms < 30U);
    CHECK(s.status == 2U && s.quality_flags == 15U);
    CHECK(!s.left_delta && !s.right_delta && !s.left_value && !s.right_value);
    CHECK(!lamp);
}
static void faults(void)
{
    ir_sample_t s;
    uint8_t mode;
    unsigned i;
    for (mode = 1U; mode <= 3U; ++mode) {
        setup(0U); adc_mode = mode;
        s = next_sample(0U, 30U);
        CHECK(s.status == 3U && !lamp && !pending && aborts);
        CHECK(!s.left_value && !s.right_value && !s.left_ambient && !s.right_active);
        CHECK(ir_sensor_measurement_count() == 0U && ir_sensor_timeout_count() == 1U);
        adc_mode = 0U;
        for (i = 0; i < 90U; ++i) { step(); CHECK(snapshot().sequence == s.sequence && !lamp); }
        s = next_sample(s.sequence, 40U);
        CHECK(s.status == 1U && ir_sensor_measurement_count() == 1U && !lamp);
    }
    setup(0U); init_ok = 0U; ir_sensor_init();
    s = snapshot(); CHECK(s.status == 3U && !lamp && !ir_sensor_adc_ok());
    init_ok = 1U;
    s = next_sample(s.sequence, 150U);
    CHECK(s.status == 1U && ir_sensor_adc_ok());
}
static void faults_while_lit(void)
{
    uint8_t mode;
    ir_sample_t s;
    unsigned limit;
    for (mode = 1U; mode <= 3U; ++mode) {
        setup(0U); limit = 20U;
        while (!lamp && limit--) step();
        CHECK(lamp == 1U && !snapshot().sequence);
        adc_mode = mode;
        s = next_sample(0U, 20U);
        CHECK(s.status == 3U && !lamp && !pending && !ir_sensor_measurement_count());
    }
}
static void nominal_period(void)
{
    ir_sample_t s;
    uint32_t previous;
    unsigned i;
    setup(0U); s = next_sample(0U, 30U); previous = s.capture_time_ms;
    s = next_sample(s.sequence, 30U);
    CHECK((uint32_t)(s.capture_time_ms - previous) == IR_SAMPLE_PERIOD_MS);
    now += 1000U;
    for (i = 0; i < 16U; ++i) ir_sensor_poll(now);
    CHECK(snapshot().sequence == s.sequence);
    s = next_sample(s.sequence, 30U);
    CHECK(ir_sensor_measurement_count() == 3U && !lamp);
    previous = s.capture_time_ms;
    s = next_sample(s.sequence, 30U);
    CHECK((uint32_t)(s.capture_time_ms - previous) == IR_SAMPLE_PERIOD_MS);
}
int main(void)
{
    healthy_pair(); quality_and_rollover(); faults(); faults_while_lit(); nominal_period();
    printf("sampling: %u assertions passed\n", checks);
    return 0;
}
