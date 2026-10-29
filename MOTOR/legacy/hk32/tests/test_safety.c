#include <assert.h>
#include "../firmware/app/motor_safety.h"

static uint16_t in1, in2;
static uint8_t emitter;
static unsigned int calls;

static void output(uint16_t a, uint16_t b, uint8_t e)
{
    in1 = a;
    in2 = b;
    emitter = e;
    ++calls;
}

int main(void)
{
    motor_safety_t m;
    motor_safety_init(&m, output);
    assert(!in1 && !in2 && !emitter);
    assert(motor_safety_set(&m, 600, 100U));
    assert(in1 == 600U && !in2 && emitter);
    assert(motor_safety_set(&m, -400, 200U));
    assert(!in1 && in2 == 400U && emitter && calls == 5U);
    motor_safety_poll(&m, 449U);
    assert(in2 == 400U);
    motor_safety_poll(&m, 450U);
    assert(!in1 && !in2 && !emitter && !m.active);
    assert(!motor_safety_set(&m, 1001, 500U));
    assert(!in1 && !in2 && !emitter);
    return 0;
}
