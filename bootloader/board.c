#include "board.h"
#include "usbd_init.h"

uint32_t SystemCoreClock = 72000000u;
volatile uint32_t board_ms;

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
    uint32_t start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < us * 72u) {}
}
void delay_ms(uint32_t ms) { while (ms--) delay_us(1000); }

void board_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= 1;
    SysTick_Config(SystemCoreClock / 1000);
    display_init();
}

void usb_disconnect(void)
{
    GPIO_Config_T io;
    NVIC_DisableIRQ(USBD1_LP_CAN1_RX0_IRQn);
    /* Keep the USB analog block powered; hold only the digital core in reset. */
    USBD_SetRegCTRL(1);
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_GPIOA);
    GPIO_ResetBit(GPIOA, GPIO_PIN_12);
    io.pin = GPIO_PIN_12;
    io.mode = GPIO_MODE_OUT_PP;
    io.speed = GPIO_SPEED_2MHz;
    GPIO_Config(GPIOA, &io);
    delay_ms(300); /* Allow Windows hubs to debounce detach from fixed D+ pull-up. */
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
