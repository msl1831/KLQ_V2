#include "main.h"
#include "board.h"
#include "ir_sensor.h"
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
    static uint8_t phase;
    ir_sample_t sample;
    uint8_t fault;

    if ((uint32_t)(now_ms - checked_ms) < 100U) return;
    checked_ms = now_ms;
    ir_sensor_get_sample(&sample);
    fault = (sample.status == 3U) ||
            ((uint32_t)(board_millis() - sample.capture_time_ms) >= 200U);
    board_led_set(fault ? ((phase & 1U) == 0U) : (phase == 0U));
    if (++phase == 10U) phase = 0U;
}

int main(void)
{
    uint8_t byte, budget;
    uint32_t now_ms;

#ifdef KLQ_BOOT_APP
    sensor_app_enter();
#endif
    board_init();
    uart_init();
    ir_sensor_init();
    frame_parser_init(&parser);
    device_service_init(&parser);
    while (1) {
        now_ms = board_millis();
        budget = 64U;
        while ((budget-- != 0U) && !device_service_reset_pending() &&
               uart_read_byte(&byte)) {
            frame_parser_push(&parser, byte, now_ms, device_service_handle_frame);
        }
        frame_parser_poll(&parser, now_ms);
        if (!device_service_reset_pending()) ir_sensor_poll(board_millis());
        device_service_poll(board_millis());
        status_led_poll(board_millis());
        board_feed_watchdog();
    }
}

void Error_Handler(void)
{
    board_emitters_off();
    __disable_irq();
    while (1) {}
}
