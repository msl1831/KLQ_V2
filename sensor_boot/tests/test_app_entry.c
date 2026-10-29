#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "board.h"
#include "device_service.h"
#include "uart.h"
#ifdef SENSOR_UL
#include "cs100a.h"
#else
#include "ir_sensor.h"
#endif
static uint8_t sent[148],accept=1U,idle=1U;
static unsigned resets,off;
static uint32_t now;
uint32_t board_millis(void) { return now; }
uint8_t board_reset_flags(void) { return 0U; }
void board_system_reset(void) { ++resets; }
void board_emitters_off(void) { ++off; }
uint16_t uart_error_count(void) { return 0U; }
uint8_t uart_tx_idle(void) { return idle; }
uint8_t uart_write(const uint8_t *p,uint16_t n)
{ assert(n<=sizeof(sent)); if(!accept) return 0U; memcpy(sent,p,n); return 1U; }
#ifdef SENSOR_UL
void cs100a_get_sample(cs100a_sample_t *p) { memset(p,0,sizeof(*p)); }
uint8_t cs100a_state(void) { return 0U; }
uint32_t cs100a_measurement_count(void) { return 0U; }
uint16_t cs100a_timeout_count(void) { return 0U; }
#else
void ir_sensor_get_sample(ir_sample_t *p) { memset(p,0,sizeof(*p)); }
uint8_t ir_sensor_state(void) { return 0U; }
uint32_t ir_sensor_measurement_count(void) { return 0U; }
uint16_t ir_sensor_timeout_count(void) { return 0U; }
uint8_t ir_sensor_adc_ok(void) { return 1U; }
#endif
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
int main(void)
{
    frame_parser_t parser;
    klq_frame_t f;
    frame_parser_init(&parser); device_service_init(&parser);
    memset(&f,0,sizeof(f)); f.command=KLQ_CMD_INFO; f.sequence=10U;
    device_service_handle_frame(&f);
    assert(get32(sent+36U)==15U && sent[42U]==0x80U && sent[43U]==0x47U);
    f.command=KLQ_CMD_ENTER_BOOTLOADER; f.payload_length=1U;
    device_service_handle_frame(&f); assert(get32(sent+16U)==2U);
    f.payload_length=0U; accept=0U; device_service_handle_frame(&f);
    device_service_poll(100U); assert(!resets && !off);
    accept=1U; now=0xFFFFFFFEUL; device_service_handle_frame(&f);
    assert(get32(sent+16U)==0U);
    device_service_poll(0xFFFFFFFFUL); assert(!resets);
    idle=0U; device_service_poll(0U); assert(!resets);
    idle=1U; device_service_poll(0U); assert(resets==1U);
#ifndef SENSOR_UL
    assert(off);
#endif
    printf("application ENTER_BOOTLOADER / TX drain / wrap PASS\n");
    return 0;
}
