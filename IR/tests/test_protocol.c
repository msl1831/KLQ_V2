#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "device_service.h"
#include "ir_sensor.h"
#include "uart.h"

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("protocol failed line %u: %s\n", __LINE__, #x); exit(1); } } while (0)
static uint8_t sent[KLQ_MAX_FRAME_SIZE], tx_accept = 1U, tx_idle = 1U, adc_ok = 1U;
static uint16_t sent_size;
static uint32_t now;
static unsigned resets, lamps_off, responses;
static frame_parser_t parser;

uint32_t board_millis(void) { return now; }
uint8_t board_reset_flags(void) { return 9U; }
void board_emitters_off(void) { ++lamps_off; }
void board_system_reset(void) { ++resets; }
uint16_t uart_error_count(void) { return 7U; }
uint8_t uart_tx_idle(void) { return tx_idle; }
uint8_t uart_write(const uint8_t *data, uint16_t size)
{
    CHECK(size <= sizeof(sent));
    if (!tx_accept) return 0U;
    memcpy(sent, data, size); sent_size = size; ++responses;
    return 1U;
}
void ir_sensor_get_sample(ir_sample_t *s)
{
    memset(s, 0, sizeof(*s));
    s->sequence = 123U; s->capture_time_ms = 456U;
    s->left_delta = 2000; s->right_delta = -500;
    s->left_value = 488U; s->right_value = 0U;
    s->left_ambient = 2500U; s->left_active = 500U;
    s->right_ambient = 1000U; s->right_active = 1500U;
    s->status = 2U; s->quality_flags = 32U;
}
uint8_t ir_sensor_state(void) { return 2U; }
uint32_t ir_sensor_measurement_count(void) { return 17U; }
uint16_t ir_sensor_timeout_count(void) { return 3U; }
uint8_t ir_sensor_adc_ok(void) { return adc_ok; }

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t ref_crc(uint32_t start, const uint8_t *p, unsigned size)
{
    uint32_t crc = ~start;
    unsigned i, bit;
    for (i = 0; i < size; ++i) {
        crc ^= p[i];
        for (bit = 0; bit < 8U; ++bit) crc = (crc & 1U) ? (crc >> 1) ^ 0xedb88320U : crc >> 1;
    }
    return ~crc;
}
static unsigned request(uint8_t *f, uint16_t cmd, const uint8_t *data, uint16_t size)
{
    memcpy(f, "KLQ1", 4U); wr32(f + 4, 0x12345678U); wr16(f + 8, cmd); wr16(f + 10, size);
    if (size) memcpy(f + 16, data, size);
    wr32(f + 12, ref_crc(ref_crc(0U, f, 12U), data, size));
    return 16U + size;
}
static void feed(const uint8_t *f, unsigned size)
{
    unsigned i;
    for (i = 0; i < size; ++i) frame_parser_push(&parser, f[i], now, device_service_handle_frame);
}
static const uint8_t *expect(uint16_t cmd, uint32_t status, unsigned size)
{
    CHECK(sent_size == 20U + size && !memcmp(sent, "KLQ1", 4U));
    CHECK(rd32(sent + 4) == 0x12345678U && rd16(sent + 8) == (uint16_t)(cmd | 0x8000U));
    CHECK(rd16(sent + 10) == size + 4U && rd32(sent + 16) == status);
    CHECK(rd32(sent + 12) == ref_crc(ref_crc(0U, sent, 12U), sent + 16, size + 4U));
    return sent + 20;
}
static const uint8_t *command(uint16_t cmd, const uint8_t *data, uint16_t length, uint32_t status, unsigned size)
{
    uint8_t f[KLQ_MAX_FRAME_SIZE];
    feed(f, request(f, cmd, data, length));
    return expect(cmd, status, size);
}
static void encoding_bounds(void)
{
    uint8_t f[150], data[129];
    memset(f, 0x5a, sizeof(f)); memset(data, 0xa5, sizeof(data));
    CHECK(frame_crc32((const uint8_t *)"123456789", 9U) == 0xcbf43926U);
    CHECK(frame_encode_response(f, 148U, 1U, 6U, 0U, data, 128U) == 148U);
    CHECK(f[148] == 0x5a && f[149] == 0x5a);
    CHECK(frame_encode_response(f, 147U, 1U, 6U, 0U, data, 128U) == 0U);
    CHECK(frame_encode_response(f, 148U, 1U, 6U, 0U, data, 129U) == 0U);
    CHECK(frame_encode_response(f, 148U, 1U, 6U, 0U, data, 65535U) == 0U);
    CHECK(frame_encode_response(f, 148U, 1U, 6U, 0U, 0, 1U) == 0U);
    CHECK(frame_encode_response(0, 148U, 1U, 6U, 0U, 0, 0U) == 0U);
}
static void service(void)
{
    const uint8_t *d;
    uint8_t echo[65];
    unsigned i;
    for (i = 0; i < sizeof(echo); ++i) echo[i] = (uint8_t)i;
    d = command(KLQ_CMD_INFO, 0, 0U, 0U, 36U);
    CHECK(rd16(d) == 1U && d[2] == 1U && d[3] == 1U);
    CHECK(rd16(d + 4) == 3U && rd16(d + 6) == 1U && rd16(d + 8) == 2U && !rd16(d + 10));
    CHECK(rd32(d + 12) == 0x00010000U && rd32(d + 16) == 7U);
    CHECK(rd16(d + 20) == 132U && !rd16(d + 22));
    CHECK(rd32(d + 24) == 0x11223344U && rd32(d + 28) == 0x55667788U && rd32(d + 32) == 0x99aabbccU);
    d = command(KLQ_CMD_GET_SAMPLE, 0, 0U, 0U, 28U);
    CHECK(rd32(d) == 123U && rd32(d + 4) == 456U);
    CHECK((int16_t)rd16(d + 8) == 2000 && (int16_t)rd16(d + 10) == -500);
    CHECK(rd16(d + 12) == 488U && rd16(d + 14) == 0U);
    CHECK(rd16(d + 16) == 2500U && rd16(d + 18) == 500U && rd16(d + 20) == 1000U && rd16(d + 22) == 1500U);
    CHECK(d[24] == 2U && d[25] == 32U && d[26] == 2U && d[27] == 0U);
    d = command(KLQ_CMD_ECHO, echo, 64U, 0U, 64U); CHECK(!memcmp(d, echo, 64U));
    command(KLQ_CMD_ECHO, echo, 65U, KLQ_STATUS_BAD_LENGTH, 0U);
    command(KLQ_CMD_INFO, echo, 1U, KLQ_STATUS_BAD_LENGTH, 0U);
    command(0x1234U, 0, 0U, KLQ_STATUS_BAD_COMMAND, 0U);
    command(KLQ_CMD_SET_CONFIG, 0, 0U, KLQ_STATUS_NOT_SUPPORTED, 0U);
    command(KLQ_CMD_BOOT_HOLD, 0, 0U, KLQ_STATUS_NOT_SUPPORTED, 0U);
    command(KLQ_CMD_ENTER_BOOTLOADER, 0, 0U, KLQ_STATUS_NOT_SUPPORTED, 0U);
    d = command(KLQ_CMD_GET_STATUS, 0, 0U, 0U, 16U);
    CHECK(rd32(d) == now && rd16(d + 4) == 7U && rd16(d + 6) == parser.error_count);
    CHECK(rd32(d + 8) == 17U && rd16(d + 12) == 3U && d[14] == 2U && d[15] == 9U);
    adc_ok = 0U;
    d = command(KLQ_CMD_SELF_TEST, 0, 0U, 0U, 4U);
    CHECK(d[0] == 1U && !d[1] && d[2] == 1U && !d[3]);
}
static void parser_recovery(void)
{
    uint8_t f[40];
    unsigned n, before;
    n = request(f, KLQ_CMD_INFO, 0, 0U);
    before = responses; f[12] ^= 1U; feed(f, n); CHECK(responses == before && parser.error_count);
    f[12] ^= 1U; feed(f, n); expect(KLQ_CMD_INFO, 0U, 36U);
    before = responses; wr16(f + 10, 133U); feed(f, n); CHECK(responses == before);
    n = request(f, KLQ_CMD_INFO, 0, 0U); feed(f, n); expect(KLQ_CMD_INFO, 0U, 36U);
    now = 0xfffffffau; frame_parser_push(&parser, 'K', now, device_service_handle_frame);
    frame_parser_push(&parser, 'L', now, device_service_handle_frame);
    frame_parser_poll(&parser, 15U); CHECK(!parser.used && !parser.expected);
    feed((const uint8_t *)"noiseKK", 7U); feed(f, n); expect(KLQ_CMD_INFO, 0U, 36U);
}
static void reset_safety(void)
{
    uint8_t f[16];
    unsigned n, before;
    device_service_init(&parser); tx_accept = 0U;
    n = request(f, KLQ_CMD_RESET, 0, 0U); feed(f, n);
    CHECK(!device_service_reset_pending() && !lamps_off && !resets);
    tx_accept = 1U; now = 0xfffffffeU; feed(f, n); expect(KLQ_CMD_RESET, 0U, 0U);
    CHECK(device_service_reset_pending() && lamps_off && !resets);
    tx_idle = 1U; device_service_poll(0xffffffffU); CHECK(!resets);
    tx_idle = 0U; device_service_poll(0U); CHECK(!resets);
    before = responses; n = request(f, KLQ_CMD_INFO, 0, 0U); feed(f, n);
    CHECK(responses == before);
    tx_idle = 1U; device_service_poll(0U); CHECK(resets == 1U);
}
int main(void)
{
    frame_parser_init(&parser); device_service_init(&parser);
    encoding_bounds(); service(); parser_recovery(); reset_safety();
    printf("protocol/service: %u assertions passed\n", checks);
    return 0;
}
