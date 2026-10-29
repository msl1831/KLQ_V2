#include "motor_safety.h"

void motor_safety_init(motor_safety_t *m, motor_output_fn output)
{
    m->output = output;
    m->last_command_ms = 0U;
    m->active = 0U;
    output(0U, 0U);
}

void motor_safety_stop(motor_safety_t *m)
{
    m->active = 0U;
    m->output(0U, 0U);
}

uint8_t motor_safety_set(motor_safety_t *m, int16_t signed_pwm,
                         uint32_t now_ms)
{
    if (signed_pwm < -(int16_t)MOTOR_PWM_MAX ||
        signed_pwm > (int16_t)MOTOR_PWM_MAX) {
        motor_safety_stop(m);
        return 0U;
    }
    m->output(0U, 0U);
    if (signed_pwm > 0)
        m->output((uint16_t)signed_pwm, 0U);
    else if (signed_pwm < 0)
        m->output(0U, (uint16_t)-signed_pwm);
    m->active = signed_pwm != 0;
    m->last_command_ms = now_ms;
    return 1U;
}

void motor_safety_poll(motor_safety_t *m, uint32_t now_ms)
{
    if (m->active &&
        (uint32_t)(now_ms - m->last_command_ms) >= MOTOR_LINK_TIMEOUT_MS)
        motor_safety_stop(m);
}
