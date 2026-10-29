#ifndef IR_SENSOR_H
#define IR_SENSOR_H

#include <stdint.h>

#ifndef IR_SAMPLE_PERIOD_MS
#define IR_SAMPLE_PERIOD_MS 10U
#endif
#ifndef IR_SETTLE_MS
#define IR_SETTLE_MS 1U
#endif
#define IR_ADC_TIMEOUT_MS 3U
#define IR_FAULT_RETRY_MS 100U

enum {
    IR_SAMPLE_NONE = 0U, IR_SAMPLE_VALID = 1U,
    IR_SAMPLE_DEGRADED = 2U, IR_SAMPLE_HW_FAULT = 3U
};
enum {
    IR_STATE_IDLE = 0U, IR_STATE_SETTLE = 1U,
    IR_STATE_CONVERT = 2U, IR_STATE_FAULT = 3U
};
enum {
    IR_QUALITY_LEFT_AMBIENT_LOW = 1U,
    IR_QUALITY_RIGHT_AMBIENT_LOW = 2U,
    IR_QUALITY_LEFT_ACTIVE_LOW = 4U,
    IR_QUALITY_RIGHT_ACTIVE_LOW = 8U,
    IR_QUALITY_LEFT_NEGATIVE = 16U,
    IR_QUALITY_RIGHT_NEGATIVE = 32U
};

typedef struct {
    uint32_t sequence, capture_time_ms;
    int16_t left_delta, right_delta;
    uint16_t left_value, right_value;
    uint16_t left_ambient, left_active, right_ambient, right_active;
    uint8_t status, quality_flags;
} ir_sample_t;

void ir_sensor_init(void);
void ir_sensor_poll(uint32_t now_ms);
void ir_sensor_get_sample(ir_sample_t *sample);
uint8_t ir_sensor_state(void);
uint32_t ir_sensor_measurement_count(void);
uint16_t ir_sensor_timeout_count(void);
uint8_t ir_sensor_adc_ok(void);

#endif
