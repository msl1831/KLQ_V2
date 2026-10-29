#ifndef IR_TEST_APM32_H
#define IR_TEST_APM32_H
#include <stdint.h>
typedef struct { uint32_t STS; struct { unsigned TXBEFLG; } STS_B; uint32_t DATA; } USART_T;
typedef unsigned IRQn_Type;
extern USART_T test_uart[3];
#define USART1 (&test_uart[0])
#define USART2 (&test_uart[1])
#define USART3 (&test_uart[2])
#define USART1_IRQn 37U
#define NVIC_SetPriority(irq, priority) ((void)0)
#define NVIC_EnableIRQ(irq) ((void)0)
#endif
