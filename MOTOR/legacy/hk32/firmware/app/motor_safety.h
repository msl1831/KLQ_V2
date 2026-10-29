#ifndef MOTOR_SAFETY_H
#define MOTOR_SAFETY_H

#include <stdint.h>

#define MOTOR_PWM_MAX          1000U
#define MOTOR_LINK_TIMEOUT_MS  250U

/* PWM is 0..1000. emitter_on=1 means drive PD6 low. */
typedef void (*motor_output_fn)(uint16_t in1, uint16_t in2,
                                uint8_t emitter_on);

typedef struct {
    motor_output_fn output;
    uint32_t last_command_ms;
    uint8_t active;
} motor_safety_t;

void motor_safety_init(motor_safety_t *m, motor_output_fn output);
void motor_safety_stop(motor_safety_t *m);
uint8_t motor_safety_set(motor_safety_t *m, int16_t signed_pwm,
                         uint32_t now_ms);
void motor_safety_poll(motor_safety_t *m, uint32_t now_ms);

#endif
