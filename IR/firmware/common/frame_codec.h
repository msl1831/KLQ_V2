#ifndef FRAME_CODEC_H
#define FRAME_CODEC_H

#include <stdint.h>
#include "klq_protocol.h"

typedef struct {
    uint32_t sequence;
    uint16_t command;
    uint16_t payload_length;
    const uint8_t *payload;
} klq_frame_t;

typedef struct {
    uint8_t data[KLQ_MAX_FRAME_SIZE];
    uint16_t used;
    uint16_t expected;
    uint32_t last_byte_ms;
    uint16_t error_count;
} frame_parser_t;

typedef void (*frame_handler_t)(const klq_frame_t *frame);

void frame_parser_init(frame_parser_t *parser);
void frame_parser_push(frame_parser_t *parser, uint8_t byte, uint32_t now_ms,
                       frame_handler_t handler);
void frame_parser_poll(frame_parser_t *parser, uint32_t now_ms);
uint16_t frame_encode_response(uint8_t *output, uint16_t output_capacity,
                               uint32_t sequence, uint16_t request_command,
                               uint32_t status, const uint8_t *payload,
                               uint16_t payload_length);
uint32_t frame_crc32(const uint8_t *data, uint16_t length);

#endif
