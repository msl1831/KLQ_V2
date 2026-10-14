#ifndef KLQ_SIM_MOCK_H
#define KLQ_SIM_MOCK_H
#include <stdint.h>
#include <stdbool.h>
/* Suppress only MCU board declarations; compile the two production C files unchanged. */
#define KLQ_BOARD_H
extern volatile uint32_t board_ms;
typedef struct { unsigned pin, mode, speed; } GPIO_Config_T;
#define GPIOC ((void *)0)
#define GPIO_PIN_3 8u
#define GPIO_MODE_IN_PU 1u
#define GPIO_SPEED_2MHz 2u
#define RCM_APB2_PERIPH_GPIOC 1u
void RCM_EnableAPB2PeriphClock(unsigned value);
void GPIO_Config(void *port, GPIO_Config_T *io);
unsigned GPIO_ReadInputBit(void *port, unsigned pin);
#endif
