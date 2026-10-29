#include "main.h"
#include "board.h"
#include "cs100a.h"
#include "device_service.h"
#include "frame_codec.h"
#include "uart.h"

#ifdef KLQ_BOOT_APP
#include "app_entry.h"
#endif
static frame_parser_t parser;

static void status_led_poll(uint32_t now_ms)
{
    static uint32_t checked_ms;
    static uint8_t phase, fault_checks;
    cs100a_sample_t sample;

    if ((uint32_t)(now_ms - checked_ms) < 100U) return;
    checked_ms = now_ms;
    cs100a_get_sample(&sample);
    if ((sample.status == CS100A_SAMPLE_NO_ECHO) ||
        (sample.status == CS100A_SAMPLE_HW_FAULT) ||
        ((uint32_t)(board_millis() - sample.capture_time_ms) >= 200U)) {
        if (fault_checks < 5U) ++fault_checks;
    } else fault_checks = 0U;
    board_led_set(fault_checks >= 5U ? ((phase & 1U) == 0U) : (phase == 0U));
    if (++phase == 10U) phase = 0U;
}

int main(void)
{
    uint8_t byte;
    uint32_t now_ms;

#ifdef KLQ_BOOT_APP
    sensor_app_enter();
#endif
    board_init();
    uart_init();
    cs100a_init();
    frame_parser_init(&parser);
    device_service_init(&parser);

    while (1) {
        now_ms = board_millis();
        while (uart_read_byte(&byte) != 0U) {
            frame_parser_push(&parser, byte, now_ms, device_service_handle_frame);
        }
        frame_parser_poll(&parser, now_ms);
        cs100a_poll(now_ms);
        device_service_poll(now_ms);
        status_led_poll(now_ms);
        board_feed_watchdog();
    }
}

void Error_Handler(void)
{
    while (1) {
    }
}
