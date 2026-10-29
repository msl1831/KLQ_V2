#include "motor_hw.h"
#include "../app/motor_safety.h"

void motor_hw_init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    GPIOA->BRR = 3U;
    GPIOA->OTYPER &= ~3UL;
    GPIOA->PUPDR = (GPIOA->PUPDR & ~15UL) | 10U;
    GPIOA->MODER = (GPIOA->MODER & ~15UL) | 5U;
    SystemCoreClockUpdate();
    RCC->APBENR2 |= RCC_APBENR2_USART1EN;
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~((15UL<<12)|(15UL<<16))) | (1UL<<12)|(1UL<<16);
    GPIOA->PUPDR = (GPIOA->PUPDR & ~((3UL<<6)|(3UL<<8))) | (1UL<<6)|(1UL<<8);
    GPIOA->MODER = (GPIOA->MODER & ~((3UL<<6)|(3UL<<8))) | (2UL<<6)|(2UL<<8);
    USART1->CR1 = 0U;
    USART1->CR2 = USART1->CR3 = 0U;
    USART1->BRR = (SystemCoreClock + 57600UL) / 115200UL;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}
void motor_hw_pwm_init(void)
{
    RCC->APBENR2 |= RCC_APBENR2_TIM1EN;
    TIM1->CR1 = 0U; TIM1->PSC = 0U;
    TIM1->ARR = SystemCoreClock / 20000UL - 1U;
    TIM1->CCR1 = TIM1->CCR2 = 0U;
    TIM1->CCMR1 = (6UL<<4)|(6UL<<12)|(1UL<<3)|(1UL<<11);
    TIM1->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E;
    TIM1->BDTR = TIM_BDTR_MOE;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
    /* PA0=TIM1_CH1, PA1=TIM1_CH2, AF2; PA2/PB6 remain SWD. */
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~255UL) | 0x22U;
    GPIOA->MODER = (GPIOA->MODER & ~15UL) | 10U;
}
void motor_hw_output(uint16_t in1, uint16_t in2)
{
    /* Latch both outputs low before loading either direction. */
    TIM1->CCR1 = TIM1->CCR2 = 0U;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->CCR1 = (uint32_t)in1 * (TIM1->ARR + 1U) / MOTOR_PWM_MAX;
    TIM1->CCR2 = (uint32_t)in2 * (TIM1->ARR + 1U) / MOTOR_PWM_MAX;
}
uint8_t motor_hw_uart_get(uint8_t *byte)
{
    uint32_t sr = USART1->SR;
    if (sr & (USART_SR_ORE|USART_SR_FE|USART_SR_NE|USART_SR_PE)) {
        (void)USART1->DR; return 2U;
    }
    if (!(sr & USART_SR_RXNE)) return 0U;
    *byte = (uint8_t)USART1->DR; return 1U;
}
uint8_t motor_hw_uart_put(uint8_t byte)
{
    uint32_t n = 100000UL;
    while (!(USART1->SR & USART_SR_TXE) && n--) { }
    if (!(USART1->SR & USART_SR_TXE)) return 0U;
    USART1->DR = byte; return 1U;
}
uint8_t motor_hw_uart_drain(void)
{
    uint32_t n = 100000UL;
    while (!(USART1->SR & USART_SR_TC) && n--) { }
    return (USART1->SR & USART_SR_TC) != 0U;
}
