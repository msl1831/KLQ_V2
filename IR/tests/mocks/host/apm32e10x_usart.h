#ifndef IR_TEST_APM32_USART_H
#define IR_TEST_APM32_USART_H
#define USART_FLAG_RXBNE 0x20U
#define USART_INT_RXBNE 1U
#define USART_INT_ERR 2U
#define USART_INT_PE 3U
#define USART_EnableInterrupt(uart, interrupt) ((void)0)
#endif
