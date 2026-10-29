#ifndef MOTOR_HW_H
#define MOTOR_HW_H
#include "py32f0xx.h"
void motor_hw_init(void);
void motor_hw_pwm_init(void);
void motor_hw_output(uint16_t in1, uint16_t in2);
uint8_t motor_hw_uart_get(uint8_t *byte);
uint8_t motor_hw_uart_put(uint8_t byte);
uint8_t motor_hw_uart_drain(void);
#endif
