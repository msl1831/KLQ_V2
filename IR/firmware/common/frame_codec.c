#include "frame_codec.h"

#define FRAME_TIMEOUT_MS 20U

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u16_le(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void write_u32_le(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

uint32_t frame_crc32(const uint8_t *data, uint16_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint16_t i;
    uint8_t bit;

    for (i = 0U; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0xEDB88320U);
        }
    }
    return ~crc;
}

void frame_parser_init(frame_parser_t *parser)
{
    parser->used = 0U;
    parser->expected = 0U;
    parser->last_byte_ms = 0U;
    parser->error_count = 0U;
}

static void parser_resync(frame_parser_t *parser)
{
    uint16_t i;
    uint16_t keep = 0U;
    static const uint8_t magic[4] = {'K', 'L', 'Q', '1'};

    for (i = 1U; i < parser->used; ++i) {
        uint16_t remaining = (uint16_t)(parser->used - i);
        uint16_t compare = remaining < 4U ? remaining : 4U;
        uint16_t j;
        for (j = 0U; j < compare; ++j) {
            if (parser->data[i + j] != magic[j]) {
                break;
            }
        }
        if (j == compare) {
            keep = remaining;
            if (keep > 3U) {
                keep = 3U;
            }
            for (j = 0U; j < keep; ++j) {
                parser->data[j] = parser->data[i + j];
            }
            break;
        }
    }
    parser->used = keep;
    parser->expected = 0U;
}

void frame_parser_push(frame_parser_t *parser, uint8_t byte, uint32_t now_ms,
                       frame_handler_t handler)
{
    uint16_t payload_length;
    uint32_t received_crc;
    uint32_t calculated_crc;
    klq_frame_t frame;

    parser->last_byte_ms = now_ms;
    if (parser->used >= KLQ_MAX_FRAME_SIZE) {
        ++parser->error_count;
        parser_resync(parser);
    }
    parser->data[parser->used++] = byte;

    if (parser->used <= 4U) {
        static const uint8_t magic[4] = {'K', 'L', 'Q', '1'};
        if (parser->data[parser->used - 1U] != magic[parser->used - 1U]) {
            parser_resync(parser);
        }
        return;
    }

    if (parser->used == KLQ_HEADER_SIZE) {
        payload_length = read_u16_le(&parser->data[10]);
        if (payload_length > KLQ_MAX_PAYLOAD) {
            ++parser->error_count;
            parser_resync(parser);
            return;
        }
        parser->expected = (uint16_t)(KLQ_HEADER_SIZE + payload_length);
    }

    if ((parser->expected == 0U) || (parser->used < parser->expected)) {
        return;
    }

    received_crc = read_u32_le(&parser->data[12]);
    calculated_crc = frame_crc32(parser->data, 12U);
    if (parser->expected > KLQ_HEADER_SIZE) {
        uint32_t crc = ~calculated_crc;
        uint16_t i;
        for (i = KLQ_HEADER_SIZE; i < parser->expected; ++i) {
            uint8_t bit;
            crc ^= parser->data[i];
            for (bit = 0U; bit < 8U; ++bit) {
                crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0xEDB88320U);
            }
        }
        calculated_crc = ~crc;
    }

    if (received_crc != calculated_crc) {
        ++parser->error_count;
        parser_resync(parser);
        return;
    }

    frame.sequence = read_u32_le(&parser->data[4]);
    frame.command = read_u16_le(&parser->data[8]);
    frame.payload_length = read_u16_le(&parser->data[10]);
    frame.payload = &parser->data[KLQ_HEADER_SIZE];
    handler(&frame);
    parser->used = 0U;
    parser->expected = 0U;
}

void frame_parser_poll(frame_parser_t *parser, uint32_t now_ms)
{
    if ((parser->used != 0U) && ((uint32_t)(now_ms - parser->last_byte_ms) > FRAME_TIMEOUT_MS)) {
        ++parser->error_count;
        parser->used = 0U;
        parser->expected = 0U;
    }
}

uint16_t frame_encode_response(uint8_t *output, uint16_t output_capacity,
                               uint32_t sequence, uint16_t request_command,
                               uint32_t status, const uint8_t *payload,
                               uint16_t payload_length)
{
    uint16_t total_payload;
    uint16_t total_length;
    uint16_t i;
    uint32_t crc;

    if ((payload_length > KLQ_MAX_PAYLOAD - 4U) ||
        ((payload_length != 0U) && (payload == 0)) || (output == 0)) {
        return 0U;
    }
    total_payload = (uint16_t)(payload_length + 4U);
    total_length = (uint16_t)(KLQ_HEADER_SIZE + total_payload);
    if (total_length > output_capacity) return 0U;
    output[0] = 'K'; output[1] = 'L'; output[2] = 'Q'; output[3] = '1';
    write_u32_le(&output[4], sequence);
    write_u16_le(&output[8], (uint16_t)(request_command | KLQ_RESPONSE_BIT));
    write_u16_le(&output[10], total_payload);
    write_u32_le(&output[12], 0U);
    write_u32_le(&output[16], status);
    for (i = 0U; i < payload_length; ++i) {
        output[20U + i] = payload[i];
    }

    crc = frame_crc32(output, 12U);
    if (payload_length + 4U != 0U) {
        uint32_t running = ~crc;
        for (i = 16U; i < total_length; ++i) {
            uint8_t bit;
            running ^= output[i];
            for (bit = 0U; bit < 8U; ++bit) {
                running = (running >> 1) ^ ((0U - (running & 1U)) & 0xEDB88320U);
            }
        }
        crc = ~running;
    }
    write_u32_le(&output[12], crc);
    return total_length;
}
