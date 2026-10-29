#include "py32f002b_it.h"
#include "board.h"
#include "uart.h"

void NMI_Handler(void) {}

void HardFault_Handler(void)
{
    while (1) {
    }
}

void SVC_Handler(void) {}
void PendSV_Handler(void) {}

void SysTick_Handler(void)
{
    board_systick_irq();
}

void USART1_IRQHandler(void)
{
    uart_irq_handler();
}

void TIM1_CC_IRQHandler(void)
{
    board_tim1_irq();
}

void TIM14_IRQHandler(void)
{
    board_tim14_irq();
}
