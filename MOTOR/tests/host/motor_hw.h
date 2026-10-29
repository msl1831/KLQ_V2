#ifndef MOTOR_HW_H
#define MOTOR_HW_H
#include <stdint.h>
typedef struct { uint32_t VTOR; } motor_test_scb_t;
extern motor_test_scb_t motor_test_scb;
extern uint32_t motor_test_uid[3],SystemCoreClock;
#define SCB (&motor_test_scb)
#define UID_BASE ((uint32_t)motor_test_uid)
#define __disable_irq() ((void)0)
#define __enable_irq() ((void)0)
#define __DSB() ((void)0)
#define __ISB() ((void)0)
void NVIC_SystemReset(void);
void SysTick_Config(uint32_t reload);
void motor_hw_init(void);
void motor_hw_pwm_init(void);
void motor_hw_output(uint16_t in1,uint16_t in2);
uint8_t motor_hw_uart_get(uint8_t *byte);
uint8_t motor_hw_uart_put(uint8_t byte);
uint8_t motor_hw_uart_drain(void);
#endif
