#ifndef KLQ_POWER_KEY_H
#define KLQ_POWER_KEY_H
#include <stdint.h>

void power_key_init(void);
void power_key_poll(void);
uint8_t power_key_raw_level(void);
uint8_t power_key_pressed(void);
uint16_t power_key_short_count(void);

#endif
