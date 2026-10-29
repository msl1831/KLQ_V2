#include "device_service.h"
#include "board.h"
#include "ir_sensor.h"
#include "uart.h"
#include "py32f002b_ll_utils.h"

static frame_parser_t *active_parser;
static uint8_t response[KLQ_MAX_FRAME_SIZE];
static uint8_t reset_pending;
static uint32_t reset_requested_ms;

static void put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint8_t send_response(const klq_frame_t *frame, uint32_t status,
                             const uint8_t *payload, uint16_t length)
{
    uint16_t size = frame_encode_response(response, sizeof(response),
                                          frame->sequence, frame->command,
                                          status, payload, length);
    return size != 0U ? uart_write(response, size) : 0U;
}

static void handle_info(const klq_frame_t *frame)
{
    uint8_t data[36];
    put_u16(data, KLQ_PROTOCOL_VERSION);
    data[2] = 1U;
    data[3] = 1U;
    put_u16(data + 4, IR_BOARD_ID);
    put_u16(data + 6, IR_HW_COMPAT);
    put_u16(data + 8, IR_SENSOR_TYPE);
    put_u16(data + 10, 0U);
    put_u32(data + 12, IR_FIRMWARE_VERSION);
    put_u32(data + 16, 7U
#ifdef KLQ_BOOT_APP
            | 8U
#endif
            );
    put_u16(data + 20, KLQ_MAX_PAYLOAD);
#ifdef KLQ_BOOT_APP
    put_u16(data + 22, 18304U);
#else
    put_u16(data + 22, 0U);
#endif
    put_u32(data + 24, LL_GetUID_Word0());
    put_u32(data + 28, LL_GetUID_Word1());
    put_u32(data + 32, LL_GetUID_Word2());
    (void)send_response(frame, KLQ_STATUS_OK, data, sizeof(data));
}

static void handle_sample(const klq_frame_t *frame)
{
    ir_sample_t sample;
    uint8_t data[28];
    ir_sensor_get_sample(&sample);
    put_u32(data, sample.sequence);
    put_u32(data + 4, sample.capture_time_ms);
    put_u16(data + 8, (uint16_t)sample.left_delta);
    put_u16(data + 10, (uint16_t)sample.right_delta);
    put_u16(data + 12, sample.left_value);
    put_u16(data + 14, sample.right_value);
    put_u16(data + 16, sample.left_ambient);
    put_u16(data + 18, sample.left_active);
    put_u16(data + 20, sample.right_ambient);
    put_u16(data + 22, sample.right_active);
    data[24] = sample.status;
    data[25] = sample.quality_flags;
    data[26] = ir_sensor_state();
    data[27] = 0U;
    (void)send_response(frame, KLQ_STATUS_OK, data, sizeof(data));
}

static void handle_status(const klq_frame_t *frame)
{
    uint8_t data[16];
    put_u32(data, board_millis());
    put_u16(data + 4, uart_error_count());
    put_u16(data + 6, active_parser->error_count);
    put_u32(data + 8, ir_sensor_measurement_count());
    put_u16(data + 12, ir_sensor_timeout_count());
    data[14] = ir_sensor_state();
    data[15] = board_reset_flags();
    (void)send_response(frame, KLQ_STATUS_OK, data, sizeof(data));
}

void device_service_init(frame_parser_t *parser)
{
    active_parser = parser;
    reset_pending = 0U;
    reset_requested_ms = 0U;
}

void device_service_handle_frame(const klq_frame_t *frame)
{
    uint8_t data[4];
    if (reset_pending) return;
    if ((frame->command != KLQ_CMD_ECHO) && frame->payload_length) {
        (void)send_response(frame, KLQ_STATUS_BAD_LENGTH, 0, 0U);
        return;
    }
    switch (frame->command) {
    case KLQ_CMD_INFO: handle_info(frame); break;
    case KLQ_CMD_ECHO:
        if (frame->payload_length > 64U)
            (void)send_response(frame, KLQ_STATUS_BAD_LENGTH, 0, 0U);
        else
            (void)send_response(frame, KLQ_STATUS_OK, frame->payload, frame->payload_length);
        break;
    case KLQ_CMD_GET_SAMPLE: handle_sample(frame); break;
    case KLQ_CMD_GET_STATUS: handle_status(frame); break;
    case KLQ_CMD_SELF_TEST:
        data[0] = 1U;
        data[1] = ir_sensor_adc_ok();
        data[2] = ir_sensor_measurement_count() != 0U;
        data[3] = 0U;
        (void)send_response(frame, KLQ_STATUS_OK, data, sizeof(data));
        break;
#ifdef KLQ_BOOT_APP
    case KLQ_CMD_ENTER_BOOTLOADER:
#endif
    case KLQ_CMD_RESET:
        if (send_response(frame, KLQ_STATUS_OK, 0, 0U)) {
            board_emitters_off();
            reset_pending = 1U;
            reset_requested_ms = board_millis();
        }
        break;
    case KLQ_CMD_SET_CONFIG:
    case KLQ_CMD_BOOT_HOLD:
#ifndef KLQ_BOOT_APP
    case KLQ_CMD_ENTER_BOOTLOADER:
#endif
        (void)send_response(frame, KLQ_STATUS_NOT_SUPPORTED, 0, 0U);
        break;
    default:
        (void)send_response(frame, KLQ_STATUS_BAD_COMMAND, 0, 0U);
        break;
    }
}

uint8_t device_service_reset_pending(void) { return reset_pending; }

void device_service_poll(uint32_t now_ms)
{
    if (!reset_pending) return;
    board_emitters_off();
    if (uart_tx_idle() && ((uint32_t)(now_ms - reset_requested_ms) >= 2U))
        board_system_reset();
}
