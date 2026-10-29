#include "sensor_hw.h"

void sensor_hw_init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN;
#if defined(SENSOR_MOTOR)
    GPIOA->BRR = 3U;
    GPIOA->OTYPER &= ~3UL;
    GPIOA->PUPDR = (GPIOA->PUPDR & ~15UL) | 10U;
    GPIOA->MODER = (GPIOA->MODER & ~15UL) | 5U;
#else
    GPIOA->BRR = (1UL << 6);
    GPIOA->MODER = (GPIOA->MODER & ~(3UL << 12)) | (1UL << 12);
#if defined(SENSOR_UL)
    GPIOA->BRR = (1UL << 5);
    GPIOA->MODER = (GPIOA->MODER & ~(3UL << 10)) | (1UL << 10);
#else
    /* PB2/PB3 emitters are active low; preload high before output mode. */
    GPIOB->BSRR = 12U;
    GPIOB->MODER = (GPIOB->MODER & ~(15UL << 4)) | (5UL << 4);
#endif
#endif
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
uint8_t sensor_hw_uart_get(uint8_t *byte)
{
    uint32_t sr = USART1->SR;
    if (sr & (USART_SR_ORE|USART_SR_FE|USART_SR_NE|USART_SR_PE)) {
        (void)USART1->DR; return 2U;
    }
    if (!(sr & USART_SR_RXNE)) return 0U;
    *byte = (uint8_t)USART1->DR; return 1U;
}
uint8_t sensor_hw_uart_put(uint8_t byte)
{
    uint32_t n = 100000UL;
    while (!(USART1->SR & USART_SR_TXE) && n--) { }
    if (!(USART1->SR & USART_SR_TXE)) return 0U;
    USART1->DR = byte; return 1U;
}
void sensor_hw_uart_drain(void)
{
    uint32_t n = 100000UL;
    while (!(USART1->SR & USART_SR_TC) && n--) { }
}
