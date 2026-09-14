#include "peripherals.h"
#include "apm32e10x.h"
#include "apm32e10x_gpio.h"
#include "apm32e10x_rcm.h"
#include "apm32e10x_usart.h"

static void gpio_uart_pair(GPIO_T *port, uint16_t tx_pin, uint16_t rx_pin)
{
    GPIO_Config_T io;

    if (tx_pin) {
        io.pin = tx_pin;
        io.mode = GPIO_MODE_AF_PP;
        io.speed = GPIO_SPEED_50MHz;
        GPIO_Config(port, &io);
    }
    if (rx_pin) {
        io.pin = rx_pin;
        io.mode = GPIO_MODE_IN_FLOATING;
        io.speed = GPIO_SPEED_50MHz;
        GPIO_Config(port, &io);
    }
}

static void uart_enable(USART_T *uart)
{
    USART_Config_T config;

    USART_ConfigStructInit(&config);
    config.baudRate = KLQ_UART_BAUDRATE;
    config.wordLength = USART_WORD_LEN_8B;
    config.stopBits = USART_STOP_BIT_1;
    config.parity = USART_PARITY_NONE;
    config.mode = USART_MODE_TX_RX;
    config.hardwareFlow = USART_HARDWARE_FLOW_NONE;
    USART_Config(uart, &config);
    USART_Enable(uart);
}

void peripherals_uart_init(void)
{
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_GPIOA |
                              RCM_APB2_PERIPH_GPIOB |
                              RCM_APB2_PERIPH_GPIOC |
                              RCM_APB2_PERIPH_GPIOD |
                              RCM_APB2_PERIPH_USART1);
    RCM_EnableAPB1PeriphClock(RCM_APB1_PERIPH_USART2 |
                              RCM_APB1_PERIPH_USART3 |
                              RCM_APB1_PERIPH_UART4 |
                              RCM_APB1_PERIPH_UART5);

    gpio_uart_pair(GPIOA, GPIO_PIN_9, GPIO_PIN_10);   /* UART1: external port 1 */
    gpio_uart_pair(GPIOA, GPIO_PIN_2, GPIO_PIN_3);    /* UART2: external port 2 */
    gpio_uart_pair(GPIOB, GPIO_PIN_10, GPIO_PIN_11);  /* UART3: external port 3 */
    gpio_uart_pair(GPIOC, GPIO_PIN_10, GPIO_PIN_11);  /* UART4: built-in audio */
    gpio_uart_pair(GPIOC, GPIO_PIN_12, 0);            /* UART5 TX: ECB02C */
    gpio_uart_pair(GPIOD, 0, GPIO_PIN_2);             /* UART5 RX: ECB02C */

    uart_enable(USART1);
    uart_enable(USART2);
    uart_enable(USART3);
    uart_enable(UART4);
    uart_enable(UART5);
}
