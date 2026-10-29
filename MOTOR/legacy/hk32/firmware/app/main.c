#include <stdint.h>
#include "hk32f030m.h"
#include "system_hk32f030m.h"
#include "motor_safety.h"
#include "../boot/update.h"

#define FRAME_SIZE 148U
#define RESPONSE_BIT 0x8000U

const uint16_t AHBPrescTable[16] = {1, 6, 1, 1, 1, 1, 1, 1,
                                    2, 4, 8, 16, 64, 128, 256, 512};

static volatile uint32_t ms;
static uint8_t rx[FRAME_SIZE], tx[FRAME_SIZE];
static uint16_t used, expected;
static uint32_t last_byte_ms;
static int16_t requested_pwm;
static motor_safety_t motor;

void SysTick_Handler(void) { ++ms; }

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t crc_step(uint32_t c, uint8_t v)
{
    uint8_t i;
    c ^= v;
    for (i = 0U; i < 8U; ++i)
        c = (c >> 1) ^ ((0UL - (c & 1UL)) & 0xEDB88320UL);
    return c;
}

static uint32_t frame_crc(const uint8_t *p, uint16_t n)
{
    uint32_t c = 0xFFFFFFFFUL;
    uint16_t i;
    for (i = 0U; i < 12U; ++i) c = crc_step(c, p[i]);
    for (i = 16U; i < n; ++i) c = crc_step(c, p[i]);
    return ~c;
}

static uint8_t uart_put(uint8_t b)
{
    uint32_t n = 100000UL;
    while (!(USART1->ISR & USART_ISR_TXE) && n--) { }
    if (!(USART1->ISR & USART_ISR_TXE)) return 0U;
    USART1->TDR = b;
    return 1U;
}

static void uart_drain(void)
{
    uint32_t n = 100000UL;
    while (!(USART1->ISR & USART_ISR_TC) && n--) { }
}

static void reply(uint32_t seq, uint16_t cmd, uint32_t status,
                  const uint8_t *data, uint16_t n)
{
    uint16_t i, total = (uint16_t)(20U + n);
    if (n > 128U) return;
    tx[0]='K'; tx[1]='L'; tx[2]='Q'; tx[3]='1';
    wr32(tx + 4U, seq); wr16(tx + 8U, cmd | RESPONSE_BIT);
    wr16(tx + 10U, (uint16_t)(n + 4U)); wr32(tx + 16U, status);
    for (i=0U; i<n; ++i) tx[20U+i]=data[i];
    wr32(tx + 12U, frame_crc(tx, total));
    for (i=0U; i<total; ++i) if (!uart_put(tx[i])) break;
}

static void motor_output(uint16_t in1, uint16_t in2, uint8_t emitter_on)
{
    TIM1->CCR3 = 0U;
    TIM1->CCR4 = 0U;
    TIM1->EGR = TIM_EGR_UG;
    GPIOD->BSRR = 1U << 6;
    if (in1 || in2) {
        TIM1->CCR3 = ((uint32_t)in1 * 1600U) / MOTOR_PWM_MAX;
        TIM1->CCR4 = ((uint32_t)in2 * 1600U) / MOTOR_PWM_MAX;
        if (emitter_on) GPIOD->BRR = 1U << 6;
    }
}

static void board_init(void)
{
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN | RCC_AHBENR_GPIODEN;
    GPIOD->BRR = (1U << 3) | (1U << 4);
    GPIOD->BSRR = 1U << 6;
    GPIOD->MODER = (GPIOD->MODER & ~((3UL << 6)|(3UL << 8)|(3UL << 12))) |
                   (1UL << 6)|(1UL << 8)|(1UL << 12);
    GPIOD->MODER &= ~((3UL << 2)|(3UL << 4));
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN | RCC_APB2ENR_USART1EN;
    TIM1->CR1 = 0U;
    TIM1->PSC = 0U;
    TIM1->ARR = 1599U; /* 20 kHz from 32 MHz HSI. */
    TIM1->CCR3 = TIM1->CCR4 = 0U;
    TIM1->CCMR2 = (6UL << 4) | (6UL << 12) | (1UL << 3) | (1UL << 11);
    TIM1->CCER = TIM_CCER_CC3E | TIM_CCER_CC4E;
    TIM1->BDTR = TIM_BDTR_MOE;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
    GPIOD->AFR[0] = (GPIOD->AFR[0] & ~((15UL << 12)|(15UL << 16))) |
                    (3UL << 12)|(3UL << 16);
    GPIOD->MODER = (GPIOD->MODER & ~((3UL << 6)|(3UL << 8))) |
                    (2UL << 6)|(2UL << 8);
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~(15UL << 12)) | (1UL << 12);
    GPIOB->AFR[0] = (GPIOB->AFR[0] & ~(15UL << 16)) | (1UL << 16);
    GPIOA->MODER = (GPIOA->MODER & ~(3UL << 6)) | (2UL << 6);
    GPIOB->MODER = (GPIOB->MODER & ~(3UL << 8)) | (2UL << 8);
    USART1->CR1 = 0U;
    USART1->BRR = (SystemCoreClock + 57600UL) / 115200UL;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

static void process_frame(void)
{
    uint32_t seq=rd32(rx+4U), status=1U;
    uint16_t cmd=rd16(rx+8U), n=rd16(rx+10U);
    const uint8_t *p=rx+16U;
    uint8_t data[36];
    uint16_t len=0U;
    int16_t duty;
    if (cmd & RESPONSE_BIT) return;
    switch (cmd) {
    case 0x0001U: /* INFO */
        if (n) { status=2U; break; }
        wr16(data,1U); data[2]=1U; data[3]=2U;
        wr16(data+4U,MOTOR_BOARD_ID); wr16(data+6U,MOTOR_HW_COMPAT);
        wr16(data+8U,1U); wr16(data+10U,0U);
        wr32(data+12U,0x00010000UL); wr32(data+16U,3U);
        wr16(data+20U,132U); wr16(data+22U,MOTOR_APP_CAPACITY);
        wr32(data+24U,*(const uint32_t *)UID_BASE);
        wr32(data+28U,*(const uint32_t *)(UID_BASE+4U)); wr32(data+32U,0U);
        status=0U; len=36U; break;
    case 0x0300U: /* SET_PWM: signed -1000..1000; refresh every <250 ms. */
        if (n!=2U) { status=2U; break; }
        duty=(int16_t)rd16(p);
        if (!motor_safety_set(&motor,duty,ms)) {
            requested_pwm=0;
            status=2U;
        } else {
            requested_pwm=duty;
            status=0U;
        }
        wr16(data,(uint16_t)requested_pwm); len=2U; break;
    case 0x0301U: /* GET_STATUS: uptime, duty, raw encoder bits, active. */
        if (n) { status=2U; break; }
        wr32(data,ms); wr16(data+4U,(uint16_t)requested_pwm);
        data[6]=(uint8_t)((GPIOD->IDR >> 1) & 3U);
        data[7]=motor.active;
        len=8U; status=0U; break;
    case 0x0302U: /* STOP */
        if (n) { status=2U; break; }
        requested_pwm=0; motor_safety_stop(&motor); status=0U; break;
    case 0x01F0U: /* ENTER_BOOTLOADER */
        if (n) { status=2U; break; }
        requested_pwm=0; motor_safety_stop(&motor);
        reply(seq,cmd,0U,0,0U);
        uart_drain(); NVIC_SystemReset();
        return;
    default: break;
    }
    reply(seq,cmd,status,data,len);
}

static void receive(uint8_t b)
{
    static const uint8_t magic[4]={'K','L','Q','1'};
    uint16_t n;
    last_byte_ms=ms;
    if (used<4U) {
        if (b!=magic[used]) {
            used=b=='K'?1U:0U;
            if (used) rx[0]='K';
            return;
        }
        rx[used++]=b;
        return;
    }
    if (used>=FRAME_SIZE) { used=expected=0U; return; }
    rx[used++]=b;
    if (used==16U) {
        n=rd16(rx+10U);
        if (n>132U) { used=expected=0U; return; }
        expected=(uint16_t)(16U+n);
    }
    if (expected && used==expected) {
        if (rd32(rx+12U)==frame_crc(rx,expected)) process_frame();
        used=expected=0U;
    }
}

int main(void)
{
    uint8_t b;
    __disable_irq();
    FLASH->INT_VEC_OFFSET=MOTOR_APP_BASE-MOTOR_FLASH_BASE;
    board_init();
    motor_safety_init(&motor,motor_output);
    SysTick_Config(SystemCoreClock/1000UL);
    __enable_irq();
    for (;;) {
        if (USART1->ISR & USART_ISR_ORE) USART1->ICR=USART_ICR_ORECF;
        if (USART1->ISR & USART_ISR_RXNE) {
            b=(uint8_t)USART1->RDR;
            receive(b);
        }
        if (used && (uint32_t)(ms-last_byte_ms)>20U)
            used=expected=0U;
        if (motor.active && (uint32_t)(ms-motor.last_command_ms)>=MOTOR_LINK_TIMEOUT_MS)
            requested_pwm=0;
        motor_safety_poll(&motor,ms);
    }
}
