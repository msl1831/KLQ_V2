#include <stdint.h>
#include "motor_hw.h"
#include "boot_hw.h"

#define FRAME_SIZE 148U
#define PAYLOAD_MAX 132U
#define RESPONSE_BIT 0x8000U

static volatile uint32_t ms;
static uint8_t rx[FRAME_SIZE], tx[FRAME_SIZE];
static uint16_t used, expected;
static uint32_t last_byte_ms;
static uint8_t hold, image_ok;
static motor_update_t update;

void SysTick_Handler(void) { ++ms; }

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t x)
{
    p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8);
}

static void wr32(uint8_t *p, uint32_t x)
{
    p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8);
    p[2] = (uint8_t)(x >> 16); p[3] = (uint8_t)(x >> 24);
}

static uint32_t crc_step(uint32_t crc, uint8_t v)
{
    uint8_t b;
    crc ^= v;
    for (b = 0U; b < 8U; ++b)
        crc = (crc >> 1) ^ ((0UL - (crc & 1UL)) & 0xEDB88320UL);
    return crc;
}

static uint32_t frame_crc(const uint8_t *p, uint16_t n)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint16_t i;
    for (i = 0U; i < 12U; ++i) crc = crc_step(crc, p[i]);
    for (i = 16U; i < n; ++i) crc = crc_step(crc, p[i]);
    return ~crc;
}

static void send_response(uint32_t seq, uint16_t cmd, uint32_t status,
                          const uint8_t *data, uint16_t n)
{
    uint16_t i, size;
    if (n > 128U) return;
    size = (uint16_t)(20U + n);
    tx[0] = 'K'; tx[1] = 'L'; tx[2] = 'Q'; tx[3] = '1';
    wr32(tx + 4U, seq);
    wr16(tx + 8U, cmd | RESPONSE_BIT);
    wr16(tx + 10U, (uint16_t)(4U + n));
    wr32(tx + 16U, status);
    for (i = 0U; i < n; ++i) tx[20U + i] = data[i];
    wr32(tx + 12U, frame_crc(tx, size));
    for (i = 0U; i < size; ++i)
        if (!boot_uart_put(tx[i])) break;
}

static void process_frame(void)
{
    uint32_t seq = rd32(rx + 4U), status = 1U;
    uint16_t cmd = rd16(rx + 8U), n = rd16(rx + 10U);
    const uint8_t *p = rx + 16U;
    uint8_t data[36];
    uint16_t len = 0U;
    if (cmd & RESPONSE_BIT) return;
    switch (cmd) {
    case 0x0001U: /* INFO */
        if (n) { status = 2U; break; }
        wr16(data, 1U); data[2] = 0U; data[3] = 2U;
        wr16(data + 4U, MOTOR_BOARD_ID); wr16(data + 6U, MOTOR_HW_COMPAT);
        wr16(data + 8U, 2U); wr16(data + 10U, 0U);
        wr32(data + 12U, MOTOR_FIRMWARE_VERSION); wr32(data + 16U, 8U);
        wr16(data + 20U, PAYLOAD_MAX); wr16(data + 22U, MOTOR_APP_CAPACITY);
        wr32(data + 24U, *(const uint32_t *)UID_BASE);
        wr32(data + 28U, *(const uint32_t *)(UID_BASE + 4U));
        wr32(data + 32U, *(const uint32_t *)(UID_BASE + 8U));
        len = 36U; status = 0U; break;
    case 0x0009U: /* BOOT_HOLD */
    case 0x01F0U: /* ENTER_BOOTLOADER while already in boot */
        if (n) { status = 2U; break; }
        hold = 1U; status = 0U; break;
    case 0x0200U: /* BEGIN */
        if (n != 10U) { status = 2U; break; }
        hold = 1U;
        status = motor_update_begin(&update, rd16(p), rd16(p + 2U),
                                    rd16(p + 4U), rd32(p + 6U));
        if (!status) image_ok = 0U;
        wr16(data, update.next_offset); len = 2U; break;
    case 0x0201U: /* DATA */
        if (n < 4U) { status = 2U; break; }
        status = motor_update_data(&update, rd16(p), p + 2U,
                                   (uint16_t)(n - 2U));
        wr16(data, update.next_offset); len = 2U; break;
    case 0x0202U: /* END */
        if (n) { status = 2U; break; }
        status = motor_update_end(&update);
        if (!status) image_ok = boot_image_valid();
        break;
    case 0x0203U: /* RUN */
        if (n) { status = 2U; break; }
        image_ok = boot_image_valid();
        status = image_ok ? 0U : 6U;
        send_response(seq, cmd, status, 0, 0U);
        if (image_ok) { boot_uart_drain(); boot_jump_app(); }
        return;
    default: break;
    }
    send_response(seq, cmd, status, data, len);
}

static void receive(uint8_t b)
{
    static const uint8_t magic[4] = {'K','L','Q','1'};
    uint16_t n;
    last_byte_ms = ms;
    if (used < 4U) {
        if (b != magic[used]) {
            used = b == 'K' ? 1U : 0U;
            if (used) rx[0] = 'K';
            return;
        }
        rx[used++] = b;
        return;
    }
    if (used >= FRAME_SIZE) { used = expected = 0U; return; }
    rx[used++] = b;
    if (used == 16U) {
        n = rd16(rx + 10U);
        if (n > PAYLOAD_MAX) { used = expected = 0U; return; }
        expected = (uint16_t)(16U + n);
    }
    if (expected && used == expected) {
        if (rd32(rx + 12U) == frame_crc(rx, expected)) process_frame();
        used = expected = 0U;
    }
}

int main(void)
{
    uint8_t b,state;
    boot_hw_init();
    motor_update_init(&update, &boot_flash_ops);
    image_ok = boot_image_valid();
    SysTick_Config(SystemCoreClock / 1000UL);
    for (;;) {
        state=boot_uart_get(&b);
        if (state==1U) receive(b);
        else if (state==2U) used=expected=0U;
        if (used && (uint32_t)(ms - last_byte_ms) > 20U)
            used = expected = 0U;
        if (image_ok && !hold && ms >= 500U) boot_jump_app();
    }
}
