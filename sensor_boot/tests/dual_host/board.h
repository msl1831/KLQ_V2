#ifndef TEST_USB_BOARD_H
#define TEST_USB_BOARD_H
#include <stdint.h>
#include <stdbool.h>
extern uint8_t mock_app[32],mock_meta[32];
#define APP_BASE 0x08008000u
#define META_BASE 0x0807F800u
#define APP_MAX 489472U
#define FLASH_PAGE 2048U
extern volatile uint32_t board_ms;
enum { DISPLAY_ICON_DOWNLOAD,DISPLAY_ICON_COMPLETE,DISPLAY_ICON_ERROR };
uint32_t board_fault_code(void);
uint32_t board_fault_pc(void);
bool board_watchdog_enabled(void);
#define FMC_FLAG_OC 1u
#define FMC_FLAG_PE 2u
#define FMC_FLAG_WPE 4u
#define FMC_STATUS_COMPLETE 0u
void FMC_Unlock(void);
void FMC_Lock(void);
void FMC_ClearStatusFlag(unsigned flags);
unsigned FMC_ErasePage(uint32_t address);
unsigned FMC_ProgramHalfWord(uint32_t address,uint16_t value);
void delay_ms(uint32_t ms);
void display_icon(unsigned icon);
void display_columns(const uint8_t *p);
void usb_disconnect(void);
void board_request_bootloader(void);
void board_jump(uint32_t address);
void NVIC_SystemReset(void);
#endif
