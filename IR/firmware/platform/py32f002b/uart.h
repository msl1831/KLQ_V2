#ifndef IR_UART_H
#define IR_UART_H

#include <stdint.h>

void uart_init(void);
uint8_t uart_read_byte(uint8_t *byte);
uint8_t uart_write(const uint8_t *data, uint16_t length);
uint8_t uart_tx_idle(void);
uint16_t uart_error_count(void);
void uart_irq_handler(void);

#endif
