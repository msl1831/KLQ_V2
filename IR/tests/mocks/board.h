#ifndef IR_TEST_BOARD_H
#define IR_TEST_BOARD_H
#include <stdint.h>
#define IR_BOARD_ID 3U
#define IR_HW_COMPAT 1U
#define IR_SENSOR_TYPE 2U
#define IR_FIRMWARE_VERSION 0x00010000UL
void board_init(void);
uint32_t board_millis(void);
uint8_t board_reset_flags(void);
void board_led_set(uint8_t on);
void board_system_reset(void);
void board_feed_watchdog(void);
void board_systick_irq(void);
void board_emitters_off(void);
void board_emitter_set(uint8_t selection);
uint8_t board_adc_init(void);
uint8_t board_adc_start(uint8_t channel);
uint8_t board_adc_poll(uint16_t *value);
void board_adc_abort(void);
#endif
