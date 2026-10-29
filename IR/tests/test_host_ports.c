#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apm32e10x.h"
#include "board.h"
#include "frame_codec.h"

volatile uint32_t board_ms;
USART_T test_uart[3];
static unsigned checks;
static uint8_t tick_auto;
#define CHECK(x) do { ++checks; if (!(x)) { printf("host ports failed line %u: %s\n", __LINE__, #x); exit(1); } } while (0)

static volatile uint32_t *clock_ptr(void)
{
    if (tick_auto) ++board_ms;
    return &board_ms;
}

uint32_t crc32_update(uint32_t start, const void *data, uint32_t size)
{
    const uint8_t *q = (const uint8_t *)data;
    uint32_t crc = ~start, i;
    unsigned bit;
    for (i = 0; i < size; ++i) {
        crc ^= q[i];
        for (bit = 0; bit < 8U; ++bit) crc = (crc & 1U) ? (crc >> 1) ^ 0xedb88320U : crc >> 1;
    }
    return ~crc;
}

/* The runner copies the actual source unchanged so only hardware headers are substituted. */
#define board_ms (*clock_ptr())
#include "external_ports_source.c"
#undef board_ms

static void setup(void)
{
    unsigned i;
    board_ms = 0U; tick_auto = 0U; memset(test_uart, 0, sizeof(test_uart));
    for (i = 0; i < 3U; ++i) test_uart[i].STS_B.TXBEFLG = 1U;
    external_ports_init();
    CHECK(!external_port_status(3U));
}
static void inject(const uint8_t *data, uint16_t size)
{
    uint8_t f[148];
    uint16_t n, i;
    n = frame_encode_response(f, sizeof(f), p[0].sequence, p[0].command, 0U, data, size);
    CHECK(n != 0U);
    for (i = 0; i < n; ++i) byte_in(&p[0], f[i]);
}
static void identify(uint8_t mode, uint8_t device_class, uint16_t sensor, uint32_t caps)
{
    uint8_t d[36];
    memset(d, 0, sizeof(d));
    p16(d, 1U); d[2] = mode; d[3] = device_class;
    p16(d + 4, sensor == 2U ? 3U : 1U); p16(d + 6, 1U); p16(d + 8, sensor);
    p32(d + 12, 0x10000U); p32(d + 16, caps); p16(d + 20, 132U);
    p32(d + 24, 0x12345678U);
    request(&p[0], CMD_INFO); inject(d, sizeof(d));
    CHECK(p[0].s.online && p[0].s.sensor_type == sensor && p[0].s.device_class == device_class);
}
static void make_ir(uint8_t *d)
{
    memset(d, 0, 28U); p32(d, 17U); p32(d + 4, 250U);
    p16(d + 8, 2000U); p16(d + 10, (uint16_t)-500);
    p16(d + 12, 488U); p16(d + 14, 0U);
    p16(d + 16, 3000U); p16(d + 18, 1000U); p16(d + 20, 1000U); p16(d + 22, 1500U);
    d[24] = 2U; d[25] = 32U; d[26] = 1U;
}
static void ir_and_type_lengths(void)
{
    uint8_t d[28];
    uint32_t errors;
    setup(); identify(1U, 1U, 2U, 1U);
    CHECK(sample_supported(&p[0]) && p[0].next_at == IR_SAMPLE_MS);
    board_ms = p[0].next_at; external_ports_poll();
    CHECK(p[0].pending && p[0].command == CMD_SAMPLE);
    make_ir(d); inject(d, sizeof(d));
    CHECK(p[0].s.sample_sequence == 17U && p[0].s.capture_time_ms == 250U);
    CHECK(p[0].s.ir_left_delta == 2000 && p[0].s.ir_right_delta == -500);
    CHECK(p[0].s.ir_left == 488U && p[0].s.ir_right == 0U && !p[0].s.distance_mm);
    CHECK(p[0].s.ir_left_ambient == 3000U && p[0].s.ir_right_active == 1500U);
    CHECK(p[0].s.sample_status == 2U && p[0].s.quality_flags == 32U && p[0].s.driver_state == 1U);
    request(&p[0], CMD_SAMPLE); errors = p[0].s.frame_error_count; inject(d, 16U);
    CHECK(p[0].s.frame_error_count == errors + 1U && p[0].s.sample_sequence == 17U);
    identify(1U, 1U, 1U, 1U);
    CHECK(!p[0].s.ir_left && !p[0].s.ir_left_delta && !p[0].s.sample_sequence);
    request(&p[0], CMD_SAMPLE); errors = p[0].s.frame_error_count; inject(d, 28U);
    CHECK(p[0].s.frame_error_count == errors + 1U && !p[0].s.sample_sequence);
    memset(d, 0, 16U); p32(d, 19U); p32(d + 4, 500U); p16(d + 8, 1234U); p16(d + 10, 420U); d[12] = 1U;
    request(&p[0], CMD_SAMPLE); inject(d, 16U);
    CHECK(p[0].s.sample_sequence == 19U && p[0].s.distance_mm == 420U && p[0].s.echo_ticks == 1234U);
    CHECK(!p[0].s.ir_left && p[0].next_at == board_ms + SAMPLE_IDLE_MS);
    CHECK(p[0].s.sample_host_ms == board_ms);
    external_ports_set_active(true);
    request(&p[0], CMD_SAMPLE); inject(d, 16U);
    CHECK(p[0].next_at == board_ms + SAMPLE_ACTIVE_MS);
    external_ports_set_active(false);
}
static void unknown_devices(void)
{
    unsigned which;
    for (which = 0; which < 4U; ++which) {
        setup();
        identify(which == 1U ? 0U : 1U, which == 2U ? 2U : 1U,
                 which == 0U ? 99U : 2U, which == 3U ? 0U : 1U);
        CHECK(!sample_supported(&p[0]));
        board_ms = p[0].next_at; external_ports_poll();
        CHECK(p[0].command == CMD_INFO);
    }
}
static void invalid_values(void)
{
    uint8_t d[28];
    unsigned which;
    for (which = 0; which < 8U; ++which) {
        setup(); identify(1U, 1U, 2U, 1U); make_ir(d);
        switch (which) {
        case 0: p16(d + 8, 4096U); break;
        case 1: p16(d + 10, (uint16_t)-4096); break;
        case 2: p16(d + 12, 1001U); break;
        case 3: p16(d + 22, 4096U); break;
        case 4: d[24] = 4U; break;
        case 5: d[25] = 64U; break;
        case 6: d[27] = 1U; break;
        case 7: d[26] = 4U; break;
        }
        request(&p[0], CMD_SAMPLE); inject(d, sizeof(d));
        CHECK(p[0].s.frame_error_count == 1U && !p[0].s.sample_sequence);
    }
}
static void timeout_unbind(void)
{
    uint8_t d[28];
    unsigned i;
    setup(); identify(1U, 1U, 2U, 1U); make_ir(d);
    request(&p[0], CMD_SAMPLE); inject(d, sizeof(d));
    request(&p[0], CMD_SAMPLE);
    for (i = 0; i < 3U; ++i) { board_ms = p[0].deadline; external_ports_poll(); }
    CHECK(!p[0].s.online && !p[0].pending && p[0].s.timeout_count == 3U);
    CHECK(!p[0].s.sensor_type && !p[0].s.fw_version && !p[0].s.capabilities && !p[0].s.uid[0]);
    CHECK(!p[0].s.sample_sequence && !p[0].s.ir_left_delta && !p[0].s.ir_left && !p[0].s.ir_left_ambient);
    board_ms = p[0].next_at; external_ports_poll(); CHECK(p[0].command == CMD_INFO);
}
static void transmit_timeout_unbind(void)
{
    uint8_t d[28];
    uint32_t errors;
    setup(); identify(1U, 1U, 2U, 1U); make_ir(d);
    request(&p[0], CMD_SAMPLE); inject(d, sizeof(d));
    CHECK(p[0].s.online && p[0].s.sample_sequence && p[0].s.ir_left);
    errors = p[0].s.uart_error_count;
    p[0].uart->STS_B.TXBEFLG = 0U;
    tick_auto = 1U;
    request(&p[0], CMD_SAMPLE);
    tick_auto = 0U;
    CHECK(p[0].s.uart_error_count == errors + 1U);
    CHECK(!p[0].s.online && !p[0].s.sensor_type && !p[0].s.uid[0] && !p[0].s.fw_version && !p[0].s.capabilities);
    CHECK(!p[0].s.sample_sequence && !p[0].s.capture_time_ms && !p[0].s.sample_status);
    CHECK(!p[0].s.ir_left_delta && !p[0].s.ir_right_delta && !p[0].s.ir_left && !p[0].s.ir_right);
    CHECK(!p[0].s.ir_left_ambient && !p[0].s.ir_left_active && !p[0].s.ir_right_ambient && !p[0].s.ir_right_active);
    CHECK(!p[0].pending && !p[0].bad && !p[0].used && !p[0].expected);
    CHECK(p[0].next_at == board_ms + DISCOVER_MS);
}
int main(void)
{
    ir_and_type_lengths(); unknown_devices(); invalid_values(); timeout_unbind(); transmit_timeout_unbind();
    printf("host external ports: %u assertions passed\n", checks);
    return 0;
}
