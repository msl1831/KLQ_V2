#include "device_service.h"
#include "board.h"
#include "cs100a.h"
#include "uart.h"
#include "py32f002b_ll_utils.h"

#define DEVICE_CLASS_SENSOR 1U
#define MODE_APPLICATION    1U
#define CAP_SAMPLE          (1UL << 0)
#define CAP_STATUS          (1UL << 1)
#define CAP_SELF_TEST       (1UL << 2)

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
                          const uint8_t *payload, uint16_t payload_length)
{
    uint16_t length = frame_encode_response(response, sizeof(response),
                                            frame->sequence, frame->command,
                                            status, payload, payload_length);
    return length != 0U ? uart_write(response, length) : 0U;
}

static void handle_info(const klq_frame_t *frame)
{
    uint8_t payload[36];
    put_u16(&payload[0], KLQ_PROTOCOL_VERSION);
    payload[2] = MODE_APPLICATION;
    payload[3] = DEVICE_CLASS_SENSOR;
    put_u16(&payload[4], UL_BOARD_ID);
    put_u16(&payload[6], UL_HW_COMPAT);
    put_u16(&payload[8], UL_SENSOR_TYPE);
    put_u16(&payload[10], 0U);
    put_u32(&payload[12], UL_FIRMWARE_VERSION);
    put_u32(&payload[16], CAP_SAMPLE | CAP_STATUS | CAP_SELF_TEST
#ifdef KLQ_BOOT_APP
            | 8U
#endif
            );
    put_u16(&payload[20], KLQ_MAX_PAYLOAD);
#ifdef KLQ_BOOT_APP
    put_u16(&payload[22], 18304U);
#else
    put_u16(&payload[22], 20352U);
#endif
    put_u32(&payload[24], LL_GetUID_Word0());
    put_u32(&payload[28], LL_GetUID_Word1());
    put_u32(&payload[32], LL_GetUID_Word2());
    send_response(frame, KLQ_STATUS_OK, payload, sizeof(payload));
}

static void handle_sample(const klq_frame_t *frame)
{
    cs100a_sample_t sample;
    uint8_t payload[16];
    cs100a_get_sample(&sample);
    put_u32(&payload[0], sample.sequence);
    put_u32(&payload[4], sample.capture_time_ms);
    put_u16(&payload[8], sample.echo_ticks);
    put_u16(&payload[10], sample.distance_mm);
    payload[12] = sample.status;
    payload[13] = sample.quality_flags;
    payload[14] = cs100a_state();
    payload[15] = 0U;
    send_response(frame, KLQ_STATUS_OK, payload, sizeof(payload));
}

static void handle_status(const klq_frame_t *frame)
{
    uint8_t payload[16];
    put_u32(&payload[0], board_millis());
    put_u16(&payload[4], uart_error_count());
    put_u16(&payload[6], active_parser->error_count);
    put_u32(&payload[8], cs100a_measurement_count());
    put_u16(&payload[12], cs100a_timeout_count());
    payload[14] = cs100a_state();
    payload[15] = board_reset_flags();
    send_response(frame, KLQ_STATUS_OK, payload, sizeof(payload));
}

void device_service_init(frame_parser_t *parser)
{
    active_parser = parser;
    reset_pending = 0U;
    reset_requested_ms = 0U;
}

void device_service_handle_frame(const klq_frame_t *frame)
{
    uint8_t self_test[4];
    if (reset_pending) return;
    if ((frame->command != KLQ_CMD_ECHO) && (frame->payload_length != 0U)) {
        send_response(frame, KLQ_STATUS_BAD_LENGTH, 0, 0U);
        return;
    }

    switch (frame->command) {
    case KLQ_CMD_INFO:
        handle_info(frame);
        break;
    case KLQ_CMD_ECHO:
        if (frame->payload_length > 64U) {
            send_response(frame, KLQ_STATUS_BAD_LENGTH, 0, 0U);
        } else {
            send_response(frame, KLQ_STATUS_OK, frame->payload, frame->payload_length);
        }
        break;
    case KLQ_CMD_GET_SAMPLE:
        handle_sample(frame);
        break;
    case KLQ_CMD_GET_STATUS:
        handle_status(frame);
        break;
    case KLQ_CMD_SELF_TEST:
        self_test[0] = 1U;
        self_test[1] = 1U;
        self_test[2] = (cs100a_measurement_count() != 0U) ? 1U : 0U;
        self_test[3] = 0U;
        send_response(frame, KLQ_STATUS_OK, self_test, sizeof(self_test));
        break;
#ifdef KLQ_BOOT_APP
    case KLQ_CMD_ENTER_BOOTLOADER:
#endif
    case KLQ_CMD_RESET:
        if (send_response(frame, KLQ_STATUS_OK, 0, 0U)) {
            reset_pending = 1U;
            reset_requested_ms = board_millis();
        }
        break;
    case KLQ_CMD_SET_CONFIG:
    case KLQ_CMD_BOOT_HOLD:
#ifndef KLQ_BOOT_APP
    case KLQ_CMD_ENTER_BOOTLOADER:
#endif
        send_response(frame, KLQ_STATUS_NOT_SUPPORTED, 0, 0U);
        break;
    default:
        send_response(frame, KLQ_STATUS_BAD_COMMAND, 0, 0U);
        break;
    }
}

void device_service_poll(uint32_t now_ms)
{
    if ((reset_pending != 0U) && uart_tx_idle() &&
        ((uint32_t)(now_ms - reset_requested_ms) >= 2U)) {
        board_system_reset();
    }
}
