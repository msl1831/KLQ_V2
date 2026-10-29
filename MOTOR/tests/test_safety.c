#include <assert.h>
#include "../firmware/app/motor_safety.h"

static uint16_t in1, in2;
static unsigned int calls;

static void output(uint16_t a, uint16_t b)
{
    in1 = a;
    in2 = b;
    assert(!a || !b);
    ++calls;
}

int main(void)
{
    motor_safety_t m;
    motor_safety_init(&m, output);
    assert(!in1 && !in2);
    assert(motor_safety_set(&m, 600, 100U));
    assert(in1 == 600U && !in2);
    assert(motor_safety_set(&m, -400, 200U));
    assert(!in1 && in2 == 400U && calls == 5U);
    motor_safety_poll(&m, 449U);
    assert(in2 == 400U);
    motor_safety_poll(&m, 450U);
    assert(!in1 && !in2 && !m.active);
    assert(!motor_safety_set(&m, 1001, 500U));
    assert(!in1 && !in2);
    assert(motor_safety_set(&m, -1000, 0xFFFFFF80UL));
    motor_safety_poll(&m, 121U);
    assert(m.active && in2 == 1000U);
    motor_safety_poll(&m, 122U);
    assert(!m.active && !in1 && !in2);
    assert(motor_safety_set(&m, 1000, 1000U));
    assert(motor_safety_set(&m, 0, 1001U));
    assert(!m.active && !in1 && !in2);
    return 0;
}
