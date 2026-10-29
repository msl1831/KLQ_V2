#ifndef SENSOR_TEST_HW_H
#define SENSOR_TEST_HW_H
#include <stdint.h>
extern uint32_t mock_uid[3];
#define UID_BASE ((uint32_t)mock_uid)
#define SystemCoreClock 24000000UL
#define SysTick_Config(x) ((void)(x))
#endif
