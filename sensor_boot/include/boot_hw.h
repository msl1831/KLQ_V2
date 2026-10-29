#ifndef SENSOR_BOOT_HW_H
#define SENSOR_BOOT_HW_H

#include <stdint.h>
#include "update.h"

void boot_hw_init(void);
uint8_t boot_uart_get(uint8_t *byte);
uint8_t boot_uart_put(uint8_t byte);
void boot_uart_drain(void);
uint8_t boot_image_valid(void);
void boot_jump_app(void);
extern const sensor_flash_ops_t boot_flash_ops;

#endif
