#ifndef KLQ_SYSTEM_GUARD_MOCK_H
#define KLQ_SYSTEM_GUARD_MOCK_H
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
typedef struct { struct { volatile uint32_t LSIEN,LSIRDYFLG; } CSTS_B; } GuardRCM;
typedef struct { volatile uint32_t KEY,PSC,CNTRLD,STS; volatile uint32_t *(*key_slot)(void); } GuardIWDT;
typedef struct { volatile uint32_t CFG; } GuardDBGMCU;
typedef struct { volatile uint32_t CFSR; } GuardSCB;
typedef struct { volatile uint32_t IDATA,BC,BSC; } GuardGPIO;
extern volatile uint32_t board_ms;
extern GuardRCM mock_rcm;
extern GuardIWDT mock_iwdt;
extern GuardDBGMCU mock_dbg;
extern GuardSCB mock_scb;
extern GuardGPIO mock_gpioc;
#define RCM (&mock_rcm)
#define IWDT (&mock_iwdt)
#define DBGMCU (&mock_dbg)
#define SCB (&mock_scb)
#define GPIOC (&mock_gpioc)
#define GPIO_PIN_4 (1u<<4)
#define GPIO_PIN_5 (1u<<5)
#define POWER_KEY_DEBOUNCE_MS 25u
#define POWER_KEY_HOLD_MS 1500u
void stub_dsb(void);
void stub_dmb(void);
#define __DSB() stub_dsb()
#define __DMB() stub_dmb()
void NVIC_SystemReset(void);
void board_watchdog_feed(void);
void usb_detach(void);
void display_clear(void);
void delay_ms(uint32_t ms);
#endif
