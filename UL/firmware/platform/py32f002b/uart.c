#include "uart.h"
#include "py32f0xx.h"
#include "py32f002b_ll_bus.h"
#include "py32f002b_ll_gpio.h"
#include "py32f002b_ll_usart.h"

#define UART_BUFFER_SIZE 256U
#define UART_BUFFER_MASK (UART_BUFFER_SIZE - 1U)

static volatile uint8_t rx_buffer[UART_BUFFER_SIZE];
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;
static volatile uint8_t tx_buffer[UART_BUFFER_SIZE];
static volatile uint16_t tx_head;
static volatile uint16_t tx_tail;
static volatile uint16_t error_counter;

void uart_init(void)
{
    LL_GPIO_InitTypeDef gpio = {0};
    LL_USART_InitTypeDef usart = {0};

    rx_head = rx_tail = tx_head = tx_tail = 0U;
    error_counter = 0U;
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_USART1);

    gpio.Pin = LL_GPIO_PIN_3 | LL_GPIO_PIN_4;
    gpio.Mode = LL_GPIO_MODE_ALTERNATE;
    gpio.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio.Pull = LL_GPIO_PULL_UP;
    gpio.Alternate = LL_GPIO_AF1_USART1;
    LL_GPIO_Init(GPIOA, &gpio);

    usart.BaudRate = 115200U;
    usart.DataWidth = LL_USART_DATAWIDTH_8B;
    usart.StopBits = LL_USART_STOPBITS_1;
    usart.Parity = LL_USART_PARITY_NONE;
    usart.TransferDirection = LL_USART_DIRECTION_TX_RX;
    usart.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
    usart.OverSampling = LL_USART_OVERSAMPLING_16;
    LL_USART_Init(USART1, &usart);
    LL_USART_ConfigAsyncMode(USART1);

    NVIC_SetPriority(USART1_IRQn, 1U);
    NVIC_EnableIRQ(USART1_IRQn);
    LL_USART_EnableIT_RXNE(USART1);
    LL_USART_EnableIT_ERROR(USART1);
    LL_USART_EnableIT_PE(USART1);
    LL_USART_Enable(USART1);
}

uint8_t uart_read_byte(uint8_t *byte)
{
    uint16_t tail = rx_tail;
    if (tail == rx_head) {
        return 0U;
    }
    *byte = rx_buffer[tail];
    rx_tail = (uint16_t)((tail + 1U) & UART_BUFFER_MASK);
    return 1U;
}

uint8_t uart_write(const uint8_t *data, uint16_t length)
{
    uint16_t head = tx_head;
    uint16_t free_space = (uint16_t)((tx_tail - head - 1U) & UART_BUFFER_MASK);
    uint16_t i;

    if (length > free_space) {
        return 0U;
    }
    for (i = 0U; i < length; ++i) {
        tx_buffer[head] = data[i];
        head = (uint16_t)((head + 1U) & UART_BUFFER_MASK);
    }
    tx_head = head;
    LL_USART_EnableIT_TXE(USART1);
    return 1U;
}

uint8_t uart_tx_idle(void)
{
    return (tx_head == tx_tail) && (LL_USART_IsActiveFlag_TC(USART1) != 0U);
}

uint16_t uart_error_count(void)
{
    return error_counter;
}

void uart_irq_handler(void)
{
    if ((LL_USART_IsActiveFlag_PE(USART1) != 0U) ||
        (LL_USART_IsActiveFlag_FE(USART1) != 0U) ||
        (LL_USART_IsActiveFlag_NE(USART1) != 0U) ||
        (LL_USART_IsActiveFlag_ORE(USART1) != 0U)) {
        LL_USART_ClearFlag_ORE(USART1);
        ++error_counter;
    }

    if ((LL_USART_IsActiveFlag_RXNE(USART1) != 0U) &&
        (LL_USART_IsEnabledIT_RXNE(USART1) != 0U)) {
        uint8_t byte = LL_USART_ReceiveData8(USART1);
        uint16_t next = (uint16_t)((rx_head + 1U) & UART_BUFFER_MASK);
        if (next == rx_tail) {
            ++error_counter;
        } else {
            rx_buffer[rx_head] = byte;
            rx_head = next;
        }
    }

    if ((LL_USART_IsActiveFlag_TXE(USART1) != 0U) &&
        (LL_USART_IsEnabledIT_TXE(USART1) != 0U)) {
        if (tx_tail == tx_head) {
            LL_USART_DisableIT_TXE(USART1);
        } else {
            LL_USART_TransmitData8(USART1, tx_buffer[tx_tail]);
            tx_tail = (uint16_t)((tx_tail + 1U) & UART_BUFFER_MASK);
        }
    }
}
