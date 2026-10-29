#ifndef UL_BOARD_H
#define UL_BOARD_H

#include <stdint.h>

#define UL_BOARD_ID             0x0001U
#define UL_HW_COMPAT            0x0001U
#define UL_SENSOR_TYPE          0x0001U
#define UL_FIRMWARE_VERSION     0x00010001UL
#define UL_ENABLE_IWDG          0U

void board_init(void);
uint32_t board_millis(void);
uint8_t board_reset_flags(void);
void board_led_set(uint8_t on);
void board_trigger_pulse(void);
void board_echo_expect_rising(void);
void board_echo_expect_falling(void);
uint16_t board_echo_capture(void);
void board_system_reset(void);
void board_feed_watchdog(void);
void board_systick_irq(void);
void board_tim1_irq(void);
void board_tim14_irq(void);

#endif
