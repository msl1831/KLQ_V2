#ifndef KLQ_BOARD_H
#define KLQ_BOARD_H
#include "apm32e10x.h"
#include "apm32e10x_gpio.h"
#include "apm32e10x_rcm.h"
#include "apm32e10x_fmc.h"
#include <stdint.h>
#include <stdbool.h>
#define APP_BASE 0x08008000u
#define META_BASE 0x0807F800u
#define APP_MAX (META_BASE - APP_BASE)
#define FLASH_PAGE 2048u
extern volatile uint32_t board_ms;
enum {
    DISPLAY_ICON_DOWNLOAD = 0,
    DISPLAY_ICON_COMPLETE,
    DISPLAY_ICON_ERROR,
    DISPLAY_ICON_STANDBY,
    DISPLAY_ICON_RUNNING
};
void board_init(void);
void delay_ms(uint32_t ms);
void delay_us(uint32_t us);
void display_init(void);
void display_icon(unsigned icon);
void display_columns(const uint8_t columns[13]);
void display_clear(void);
void usb_disconnect(void);
void board_request_bootloader(void);
bool board_consume_bootloader_request(void);
void board_jump(uint32_t address);
#endif
