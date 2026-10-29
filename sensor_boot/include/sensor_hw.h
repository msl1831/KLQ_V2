#ifndef SENSOR_HW_H
#define SENSOR_HW_H
#include "py32f0xx.h"
void sensor_hw_init(void);
uint8_t sensor_hw_uart_get(uint8_t *byte);
uint8_t sensor_hw_uart_put(uint8_t byte);
void sensor_hw_uart_drain(void);
#endif
