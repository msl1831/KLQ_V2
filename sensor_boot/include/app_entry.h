#ifndef SENSOR_APP_ENTRY_H
#define SENSOR_APP_ENTRY_H
#include "py32f0xx.h"
static void sensor_app_enter(void)
{
    /* Vendor SystemInit resets VTOR; relocate before enabling app interrupts. */
    __disable_irq();
    SCB->VTOR = 0x08001800UL;
    __DSB(); __ISB();
    __enable_irq();
}
#endif
