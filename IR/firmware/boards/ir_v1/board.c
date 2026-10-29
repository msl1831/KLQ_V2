#include "board.h"
#include "py32f0xx.h"
#include "py32f002b_ll_adc.h"
#include "py32f002b_ll_bus.h"
#include "py32f002b_ll_cortex.h"
#include "py32f002b_ll_gpio.h"
#include "py32f002b_ll_rcc.h"
#include "py32f002b_ll_utils.h"

#define EMITTER_PINS (LL_GPIO_PIN_2 | LL_GPIO_PIN_3)

static volatile uint32_t tick_ms;
static uint8_t saved_reset_flags, clock_ok, adc_ready, adc_channel;

static uint8_t clock_init(void)
{
    uint32_t wait;
    LL_RCC_HSI_Enable();
    for (wait = 24000U; wait != 0U; --wait)
        if (LL_RCC_HSI_IsReady() != 0U) break;
    if (wait == 0U) return 0U;
    LL_RCC_SetHSIDiv(LL_RCC_HSI_DIV_1);
    LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
    LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSISYS);
    for (wait = 24000U; wait != 0U; --wait)
        if (LL_RCC_GetSysClkSource() == LL_RCC_SYS_CLKSOURCE_STATUS_HSISYS) break;
    if (wait == 0U) return 0U;
    LL_SetSystemCoreClock(24000000U);
    LL_Init1msTick(24000000U);
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
    return 1U;
}

void board_init(void)
{
    saved_reset_flags = 0U;
    if (LL_RCC_IsActiveFlag_IWDGRST() != 0U) saved_reset_flags |= 1U;
    if (LL_RCC_IsActiveFlag_SFTRST() != 0U) saved_reset_flags |= 2U;
    if (LL_RCC_IsActiveFlag_PINRST() != 0U) saved_reset_flags |= 4U;
    if (LL_RCC_IsActiveFlag_PWRRST() != 0U) saved_reset_flags |= 8U;
    LL_RCC_ClearResetFlags();
    tick_ms = 0U;
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA | LL_IOP_GRP1_PERIPH_GPIOB);
    /* Preload inactive levels before enabling outputs; leave SWD pins alone. */
    board_emitters_off();
    LL_GPIO_SetPinOutputType(GPIOB, EMITTER_PINS, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(GPIOB, LL_GPIO_PIN_2, LL_GPIO_SPEED_FREQ_LOW);
    LL_GPIO_SetPinSpeed(GPIOB, LL_GPIO_PIN_3, LL_GPIO_SPEED_FREQ_LOW);
    LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_2, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_3, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_2, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_3, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_6);
    LL_GPIO_SetPinOutputType(GPIOA, LL_GPIO_PIN_6, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_6, LL_GPIO_SPEED_FREQ_LOW);
    LL_GPIO_SetPinPull(GPIOA, LL_GPIO_PIN_6, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_6, LL_GPIO_MODE_OUTPUT);
    adc_ready = 0U;
    adc_channel = 0xFFU;
    clock_ok = clock_init();
}

uint32_t board_millis(void) { return tick_ms; }
uint8_t board_reset_flags(void) { return saved_reset_flags; }

void board_led_set(uint8_t on)
{
    if (on != 0U) LL_GPIO_SetOutputPin(GPIOA, LL_GPIO_PIN_6);
    else LL_GPIO_ResetOutputPin(GPIOA, LL_GPIO_PIN_6);
}

void board_emitters_off(void)
{
    LL_GPIO_SetOutputPin(GPIOB, EMITTER_PINS);
}

void board_emitter_set(uint8_t selection)
{
    board_emitters_off();
    if (selection == 1U) LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_3);
    else if (selection == 2U) LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_2);
}

void board_adc_abort(void)
{
    board_emitters_off();
    LL_ADC_Reset(ADC1);
    adc_ready = 0U;
    adc_channel = 0xFFU;
}

uint8_t board_adc_init(void)
{
    uint32_t wait, started;
    adc_ready = 0U;
    adc_channel = 0xFFU;
    if (clock_ok == 0U) return 0U;
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOB);
    LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_0, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_1, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_0, LL_GPIO_MODE_ANALOG);
    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_1, LL_GPIO_MODE_ANALOG);
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_ADC1);
    LL_ADC_Reset(ADC1);
    LL_ADC_SetClock(ADC1, LL_ADC_CLOCK_SYNC_PCLK_DIV8);
    LL_ADC_SetResolution(ADC1, LL_ADC_RESOLUTION_12B);
    LL_ADC_SetDataAlignment(ADC1, LL_ADC_DATA_ALIGN_RIGHT);
    LL_ADC_SetLowPowerMode(ADC1, LL_ADC_LP_MODE_NONE);
    LL_ADC_SetSamplingTimeCommonChannels(ADC1, LL_ADC_SAMPLINGTIME_239CYCLES_5);
    LL_ADC_REG_SetTriggerSource(ADC1, LL_ADC_REG_TRIG_SOFTWARE);
    LL_ADC_REG_SetContinuousMode(ADC1, LL_ADC_REG_CONV_SINGLE);
    LL_ADC_REG_SetOverrun(ADC1, LL_ADC_REG_OVR_DATA_OVERWRITTEN);
    LL_ADC_REG_SetSequencerDiscont(ADC1, LL_ADC_REG_SEQ_DISCONT_DISABLE);
    LL_ADC_SetCommonPathInternalCh(__LL_ADC_COMMON_INSTANCE(ADC1), LL_ADC_PATH_INTERNAL_NONE);
    LL_ADC_SetCalibrationMode(ADC1, LL_ADC_CAL_MODE_OFFSET);
    LL_ADC_SetCalibrationSamplingTime(ADC1, LL_ADC_CAL_SAMPLINGTIME_8CYCLES);
    LL_ADC_StartCalibration(ADC1);
    started = board_millis();
    for (wait = 240000U; wait != 0U; --wait) {
        if (LL_ADC_IsCalibrationOnGoing(ADC1) == 0U) break;
        if ((uint32_t)(board_millis() - started) >= 5U) break;
    }
    if (LL_ADC_IsCalibrationOnGoing(ADC1) != 0U ||
        LL_ADC_GetCalibrationOffsetStatus(ADC1) != LL_ADC_CAL_OFFSET_STATUS_SUCCESS) {
        board_adc_abort();
        return 0U;
    }
    /* 64 CPU NOPs exceed 4 ADC clocks at PCLK/8, even before loop overhead. */
    for (wait = 0U; wait < 64U; ++wait) __NOP();
    adc_ready = 1U;
    return 1U;
}

uint8_t board_adc_start(uint8_t channel)
{
    uint32_t wait;
    if (adc_ready == 0U || channel > 1U || LL_ADC_REG_IsConversionOngoing(ADC1) != 0U)
        return 0U;
    if (adc_channel != channel || LL_ADC_IsEnabled(ADC1) == 0U) {
        /* PY32F002B channel selection changes only while the ADC is disabled. */
        if (LL_ADC_IsEnabled(ADC1) != 0U) LL_ADC_Disable(ADC1);
        for (wait = 4800U; wait != 0U; --wait)
            if (LL_ADC_IsEnabled(ADC1) == 0U && LL_ADC_IsDisableOngoing(ADC1) == 0U) break;
        if (wait == 0U) return 0U;
        LL_ADC_REG_SetSequencerChannels(ADC1, channel == 0U ? LL_ADC_CHANNEL_0 : LL_ADC_CHANNEL_7);
        LL_ADC_Enable(ADC1);
        /* At least 8 ADC clocks after ADEN; no delay loop during conversions. */
        for (wait = 0U; wait < 128U; ++wait) __NOP();
        adc_channel = channel;
    }
    ADC1->ISR = ADC_ISR_EOC | ADC_ISR_EOSEQ | ADC_ISR_OVR;
    LL_ADC_REG_StartConversion(ADC1);
    return 1U;
}

uint8_t board_adc_poll(uint16_t *value)
{
    if (adc_ready == 0U || LL_ADC_IsActiveFlag_OVR(ADC1) != 0U) return 2U;
    if (LL_ADC_IsActiveFlag_EOC(ADC1) == 0U) return 0U;
    *value = LL_ADC_REG_ReadConversionData12(ADC1);
    ADC1->ISR = ADC_ISR_EOC | ADC_ISR_EOSEQ | ADC_ISR_OVR;
    return 1U;
}

void board_system_reset(void)
{
    board_emitters_off();
    NVIC_SystemReset();
}

void board_feed_watchdog(void) {}
void board_systick_irq(void) { ++tick_ms; }
