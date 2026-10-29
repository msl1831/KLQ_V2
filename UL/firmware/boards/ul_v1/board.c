#include "board.h"
#include "cs100a.h"
#include "py32f0xx.h"
#include "py32f002b_ll_bus.h"
#include "py32f002b_ll_cortex.h"
#include "py32f002b_ll_gpio.h"
#include "py32f002b_ll_iwdg.h"
#include "py32f002b_ll_rcc.h"
#include "py32f002b_ll_tim.h"
#include "py32f002b_ll_utils.h"

static volatile uint32_t tick_ms;
static uint8_t saved_reset_flags;

static void clock_init(void)
{
    LL_RCC_HSI_Enable();
    while (LL_RCC_HSI_IsReady() == 0U) {
    }
    LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSISYS);
    while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSISYS) {
    }
    LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
    LL_Init1msTick(24000000U);
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
    LL_SetSystemCoreClock(24000000U);
}

static void capture_init(void)
{
    LL_GPIO_InitTypeDef gpio = {0};
    LL_TIM_IC_InitTypeDef capture = {0};
    LL_TIM_InitTypeDef timer = {0};

    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_TIM1);

    gpio.Pin = LL_GPIO_PIN_0;
    gpio.Mode = LL_GPIO_MODE_ALTERNATE;
    gpio.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    gpio.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio.Pull = LL_GPIO_PULL_DOWN;
    gpio.Alternate = LL_GPIO_AF2_TIM1;
    LL_GPIO_Init(GPIOA, &gpio);

    capture.ICActiveInput = LL_TIM_ACTIVEINPUT_DIRECTTI;
    capture.ICPrescaler = LL_TIM_ICPSC_DIV1;
    capture.ICPolarity = LL_TIM_IC_POLARITY_RISING;
    capture.ICFilter = LL_TIM_IC_FILTER_FDIV1_N4;
    LL_TIM_IC_Init(TIM1, LL_TIM_CHANNEL_CH1, &capture);

    timer.Prescaler = 48U - 1U;
    timer.CounterMode = LL_TIM_COUNTERMODE_UP;
    timer.Autoreload = 0xFFFFU;
    timer.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
    timer.RepetitionCounter = 0U;
    LL_TIM_Init(TIM1, &timer);
    LL_TIM_EnableIT_CC1(TIM1);
    LL_TIM_CC_EnableChannel(TIM1, LL_TIM_CHANNEL_CH1);
    NVIC_SetPriority(TIM1_CC_IRQn, 0U);
    NVIC_EnableIRQ(TIM1_CC_IRQn);
    LL_TIM_EnableCounter(TIM1);
}

static void trigger_init(void)
{
    LL_GPIO_InitTypeDef gpio = {0};
    LL_TIM_InitTypeDef timer = {0};

    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_TIM14);
    gpio.Pin = LL_GPIO_PIN_5;
    gpio.Mode = LL_GPIO_MODE_OUTPUT;
    gpio.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    gpio.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio.Pull = LL_GPIO_PULL_DOWN;
    LL_GPIO_Init(GPIOA, &gpio);
    LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_5);

    timer.Prescaler = 24U - 1U;
    timer.CounterMode = LL_TIM_COUNTERMODE_UP;
    timer.Autoreload = 50U - 1U;
    timer.ClockDivision = LL_TIM_CLOCKDIVISION_DIV1;
    timer.RepetitionCounter = 0U;
    LL_TIM_Init(TIM14, &timer);
    LL_TIM_SetOnePulseMode(TIM14, LL_TIM_ONEPULSEMODE_SINGLE);
    LL_TIM_EnableIT_UPDATE(TIM14);
    NVIC_SetPriority(TIM14_IRQn, 0U);
    NVIC_EnableIRQ(TIM14_IRQn);
}

static void watchdog_init(void)
{
#if UL_ENABLE_IWDG
    LL_RCC_LSI_Enable();
    while (LL_RCC_LSI_IsReady() == 0U) {
    }
    LL_IWDG_Enable(IWDG);
    LL_IWDG_EnableWriteAccess(IWDG);
    LL_IWDG_SetPrescaler(IWDG, LL_IWDG_PRESCALER_32);
    LL_IWDG_SetReloadCounter(IWDG, 2048U);
    while (LL_IWDG_IsReady(IWDG) == 0U) {
    }
    LL_IWDG_ReloadCounter(IWDG);
#endif
}

void board_init(void)
{
    saved_reset_flags = 0U;
    if (LL_RCC_IsActiveFlag_IWDGRST() != 0U) saved_reset_flags |= 0x01U;
    if (LL_RCC_IsActiveFlag_SFTRST() != 0U) saved_reset_flags |= 0x02U;
    if (LL_RCC_IsActiveFlag_PINRST() != 0U) saved_reset_flags |= 0x04U;
    if (LL_RCC_IsActiveFlag_PWRRST() != 0U) saved_reset_flags |= 0x08U;
    LL_RCC_ClearResetFlags();
    tick_ms = 0U;
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
    LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_6);
    LL_GPIO_SetPinOutputType(GPIOA, LL_GPIO_PIN_6, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_6, LL_GPIO_SPEED_FREQ_LOW);
    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_6, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_6, LL_GPIO_MODE_OUTPUT);
    clock_init();
    capture_init();
    trigger_init();
    watchdog_init();
}

uint32_t board_millis(void)
{
    return tick_ms;
}

uint8_t board_reset_flags(void)
{
    return saved_reset_flags;
}

void board_led_set(uint8_t on)
{
    if (on != 0U) LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_6);
    else LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_6);
}

void board_trigger_pulse(void)
{
    LL_TIM_DisableCounter(TIM14);
    LL_TIM_SetCounter(TIM14, 0U);
    LL_TIM_ClearFlag_UPDATE(TIM14);
    LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_5);
    LL_TIM_EnableCounter(TIM14);
}

void board_echo_expect_rising(void)
{
    LL_TIM_IC_SetPolarity(TIM1, LL_TIM_CHANNEL_CH1, LL_TIM_IC_POLARITY_RISING);
    LL_TIM_ClearFlag_CC1(TIM1);
}

void board_echo_expect_falling(void)
{
    LL_TIM_IC_SetPolarity(TIM1, LL_TIM_CHANNEL_CH1, LL_TIM_IC_POLARITY_FALLING);
    LL_TIM_ClearFlag_CC1(TIM1);
}

uint16_t board_echo_capture(void)
{
    return (uint16_t)LL_TIM_IC_GetCaptureCH1(TIM1);
}

void board_system_reset(void)
{
    NVIC_SystemReset();
}

void board_feed_watchdog(void)
{
#if UL_ENABLE_IWDG
    LL_IWDG_ReloadCounter(IWDG);
#endif
}

void board_systick_irq(void)
{
    ++tick_ms;
}

void board_tim1_irq(void)
{
    if ((LL_TIM_IsActiveFlag_CC1(TIM1) != 0U) &&
        (LL_TIM_IsEnabledIT_CC1(TIM1) != 0U)) {
        LL_TIM_ClearFlag_CC1(TIM1);
        cs100a_echo_edge_irq(board_echo_capture());
    }
    if (LL_TIM_IsActiveFlag_CC1OVR(TIM1) != 0U) {
        LL_TIM_ClearFlag_CC1OVR(TIM1);
    }
}

void board_tim14_irq(void)
{
    if ((LL_TIM_IsActiveFlag_UPDATE(TIM14) != 0U) &&
        (LL_TIM_IsEnabledIT_UPDATE(TIM14) != 0U)) {
        LL_TIM_ClearFlag_UPDATE(TIM14);
        LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_5);
        cs100a_trigger_finished_irq();
    }
}
