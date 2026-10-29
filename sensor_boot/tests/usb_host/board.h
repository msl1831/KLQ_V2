#ifndef TEST_USB_BOARD_H
#define TEST_USB_BOARD_H
#include <stdint.h>
#include <stdbool.h>
extern uint8_t mock_app[32],mock_meta[32];
#define APP_BASE ((uint32_t)mock_app)
#define META_BASE ((uint32_t)mock_meta)
#define APP_MAX 489472U
#define FLASH_PAGE 2048U
extern volatile uint32_t board_ms;
enum { DISPLAY_ICON_DOWNLOAD,DISPLAY_ICON_COMPLETE,DISPLAY_ICON_ERROR };
void delay_ms(uint32_t ms);
void display_icon(unsigned icon);
void display_columns(const uint8_t *p);
void usb_disconnect(void);
void board_request_bootloader(void);
void board_jump(uint32_t address);
void NVIC_SystemReset(void);
#endif
