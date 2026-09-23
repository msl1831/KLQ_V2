#include "board.h"
#include "usbd_init.h"

uint32_t SystemCoreClock = 72000000u;
volatile uint32_t board_ms;

#define BOOT_REQUEST_ADDRESS 0x2001FFF8u
#define BOOT_REQUEST_MAGIC   0x4B4C5142u /* "KLQB" */

/* Called by the reset assembly BEFORE C runtime initialization. No RAM globals. */
void SystemInit(void)
{
    volatile uint32_t timeout;
    RCM->APB2CLKEN |= (1u << 4); /* GPIOC */
    GPIOC->BSC = 1u << 5;       /* Latch high before enabling PC5 output. */
    GPIOC->CFGLOW = (GPIOC->CFGLOW & ~(15u << 20)) | (2u << 20);
    RCM->CTRL_B.HSIEN = 1;
    RCM->CFG_B.SCLKSW = 0;
    while (RCM->CFG_B.SCLKSWSTS != 0) {}
    RCM->CTRL_B.PLLEN = 0;
    RCM->CTRL_B.HSEEN = 1;
    for (timeout = 0; timeout < 2000000u && !RCM->CTRL_B.HSERDYFLG; ++timeout) {}
    if (!RCM->CTRL_B.HSERDYFLG) { while (1) {} } /* Keep power and SWD available. */
    FMC->CTRL1_B.PBEN = 1;
    FMC->CTRL1_B.WS = 2;
    RCM->CFG_B.AHBPSC = 0;
    RCM->CFG_B.APB1PSC = 4; /* APB1 = 36 MHz */
    RCM->CFG_B.APB2PSC = 0;
    RCM->CFG_B.PLLSRCSEL = 1;
    RCM->CFG_B.PLLHSEPSC = 1; /* 16 MHz / 2 * 9 = 72 MHz */
    RCM->CFG_B.PLLMULCFG = 7;
    RCM->CTRL_B.PLLEN = 1;
    for (timeout = 0; timeout < 2000000u && !RCM->CTRL_B.PLLRDYFLG; ++timeout) {}
    if (!RCM->CTRL_B.PLLRDYFLG) { while (1) {} }
    RCM->CFG_B.SCLKSW = 2;
    while (RCM->CFG_B.SCLKSWSTS != 2) {}
#ifdef KLQ_DEMO_APP
    SCB->VTOR = APP_BASE;
#else
    SCB->VTOR = 0x08000000u;
#endif
}

void SystemCoreClockUpdate(void) { SystemCoreClock = 72000000u; }
void SysTick_Handler(void) { ++board_ms; }
void delay_us(uint32_t us)
{
    uint32_t remaining = us * (SystemCoreClock / 1000000u);
    uint32_t period = SysTick->LOAD + 1u;
    uint32_t previous = SysTick->VAL;
    /* SysTick keeps timing independent of debugger-owned DEMCR/DWT state.
     * Poll the downcounter so short delays also work with interrupts masked. */
    while (remaining) {
        uint32_t current = SysTick->VAL;
        uint32_t elapsed = previous >= current ? previous-current : previous+period-current;
        if (elapsed >= remaining) break;
        remaining -= elapsed;
        previous = current;
    }
}
void delay_ms(uint32_t ms) { while (ms--) delay_us(1000); }

void board_init(void)
{
    SysTick_Config(SystemCoreClock / 1000);
    display_init();
}

static uint8_t key_down(void) { return (GPIOC->IDATA & GPIO_PIN_4)==0; }

void board_power_off(bool usb_active)
{
    uint8_t raw, stable, released;
    uint32_t changed, pressed=0, now;
    if (usb_active) usb_disconnect();
    display_clear();
    GPIOC->BC = GPIO_PIN_5;
    /* USB/SWD may still power the MCU. Require release before a new 1.5 s hold. */
    raw=stable=key_down();
    released=(uint8_t)!stable;
    changed=board_ms;
    for (;;) {
        now=board_ms;
        {
            uint8_t v=key_down();
            if (v!=raw) { raw=v; changed=now; }
            if (v!=stable && now-changed>=POWER_KEY_DEBOUNCE_MS) {
                stable=v;
                if (!v) released=1;
                else if (released) pressed=now;
            }
        }
        if (released && stable && now-pressed>=POWER_KEY_HOLD_MS) {
            GPIOC->BSC = GPIO_PIN_5;
            NVIC_SystemReset();
        }
    }
}

void board_power_on_gate(void)
{
    uint8_t soft=RCM_ReadStatusFlag(RCM_FLAG_SWRST);
    uint8_t cold=RCM_ReadStatusFlag(RCM_FLAG_PORRST);
    uint8_t raw, stable;
    uint32_t changed, pressed;
    GPIO_Config_T io;
    RCM_ClearStatusFlag();
    io.pin=GPIO_PIN_4;
    io.mode=GPIO_MODE_IN_PU;
    io.speed=GPIO_SPEED_2MHz;
    GPIO_Config(GPIOC,&io);
    /* USB upgrade and off-state wake use software reset; do not gate them again. */
    if (soft || (!cold && !key_down())) return;
    raw=stable=key_down();
    if (!stable) board_power_off(false);
    changed=pressed=board_ms;
    while (board_ms-pressed<POWER_KEY_HOLD_MS) {
        uint8_t v=key_down();
        uint32_t now=board_ms;
        if (v!=raw) { raw=v; changed=now; }
        if (v!=stable && now-changed>=POWER_KEY_DEBOUNCE_MS) {
            stable=v;
            if (!v) board_power_off(false);
        }
    }
}

#ifndef KLQ_DEMO_APP
static uint32_t boot_changed, boot_pressed;
static uint8_t boot_raw, boot_stable, boot_armed;

void board_boot_key_init(void)
{
    boot_raw=boot_stable=key_down();
    boot_armed=0;
    boot_changed=boot_pressed=board_ms;
}

void board_boot_key_poll(void)
{
    uint8_t v=key_down();
    uint32_t now=board_ms;
    if (v!=boot_raw) { boot_raw=v; boot_changed=now; }
    if (v!=boot_stable && now-boot_changed>=POWER_KEY_DEBOUNCE_MS) {
        boot_stable=v;
        if (v) boot_pressed=now;
        else boot_armed=1;
    }
    if (!boot_stable && !boot_armed && now-boot_changed>=POWER_KEY_DEBOUNCE_MS) boot_armed=1;
    if (boot_stable && boot_armed && now-boot_pressed>=POWER_KEY_HOLD_MS)
        board_power_off(true);
}
#endif

void usb_disconnect(void)
{
    GPIO_Config_T io;
    NVIC_DisableIRQ(USBD1_LP_CAN1_RX0_IRQn);
    /* Release USB ownership before driving the bus to SE0 for detach. A core
     * reset alone leaves the transceiver active and the host can retain its
     * previous device address when the application starts at address zero. */
    RCM_EnableAPB1PeriphClock(RCM_APB1_PERIPH_USB);
    USBD_PowerOff();
    USBD_Disable();
    RCM_DisableAPB1PeriphClock(RCM_APB1_PERIPH_USB);
    NVIC_ClearPendingIRQ(USBD1_LP_CAN1_RX0_IRQn);
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_GPIOA);
    GPIO_ResetBit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
    io.pin = GPIO_PIN_11 | GPIO_PIN_12;
    io.mode = GPIO_MODE_OUT_PP;
    io.speed = GPIO_SPEED_2MHz;
    GPIO_Config(GPIOA, &io);
    delay_ms(300); /* Allow Windows hubs to debounce detach from fixed D+ pull-up. */
}

void board_request_bootloader(void)
{
    volatile uint32_t *request = (volatile uint32_t *)BOOT_REQUEST_ADDRESS;
    request[0] = BOOT_REQUEST_MAGIC;
    request[1] = ~BOOT_REQUEST_MAGIC;
    __DSB();
}

bool board_consume_bootloader_request(void)
{
    volatile uint32_t *request = (volatile uint32_t *)BOOT_REQUEST_ADDRESS;
    bool requested = request[0] == BOOT_REQUEST_MAGIC && request[1] == ~BOOT_REQUEST_MAGIC;
    request[0] = 0;
    request[1] = 0;
    __DSB();
    return requested;
}

__asm void jump_stack(uint32_t stack, uint32_t entry)
{
    MSR MSP, r0
    DSB
    ISB
    CPSIE i
    BX r1
}

void board_jump(uint32_t address)
{
    unsigned i;
    usb_disconnect();
    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    for (i = 0; i < 8; ++i) { NVIC->ICER[i] = 0xffffffffu; NVIC->ICPR[i] = 0xffffffffu; }
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    SCB->VTOR = address;
    __set_CONTROL(0);
    jump_stack(*(uint32_t *)address, *(uint32_t *)(address + 4));
    while (1) {}
}
