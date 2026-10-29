#include <assert.h>
#include <string.h>
#include "../firmware/boot/update.h"

static uint8_t flash[0x4000];
static uint8_t valid;
static uint8_t fail_write;

static int flash_read(uint32_t a, uint8_t *p, uint16_t n)
{
    if (a < MOTOR_FLASH_BASE || a - MOTOR_FLASH_BASE + n > sizeof(flash)) return 0;
    memcpy(p, flash + a - MOTOR_FLASH_BASE, n);
    return 1;
}

static int invalidate(void)
{
    memset(flash + MOTOR_META_BASE - MOTOR_FLASH_BASE, 0xFF, MOTOR_FLASH_PAGE_SIZE);
    valid = 0;
    return 1;
}

static int erase_app(uint32_t n)
{
    if (n > MOTOR_APP_CAPACITY) return 0;
    memset(flash + MOTOR_APP_BASE - MOTOR_FLASH_BASE, 0xFF,
           (size_t)((n + 127U) & ~127UL));
    return 1;
}

static int write_app(uint32_t a, const uint8_t *p, uint16_t n)
{
    uint16_t i;
    if (fail_write || a < MOTOR_APP_BASE || a + n > MOTOR_META_BASE) return 0;
    for (i = 0; i < n; ++i) flash[a + i - MOTOR_FLASH_BASE] &= p[i];
    return 1;
}

static int commit(uint16_t board, uint16_t hw, uint16_t n, uint32_t crc)
{
    if (board != MOTOR_BOARD_ID || hw != MOTOR_HW_COMPAT ||
        n != 16U || !crc) return 0;
    valid = 1;
    return 1;
}

static uint32_t crc32(const uint8_t *p, uint16_t n)
{
    uint32_t c = 0xFFFFFFFFUL;
    uint16_t i;
    uint8_t b;
    for (i = 0; i < n; ++i) {
        c ^= p[i];
        for (b = 0; b < 8U; ++b)
            c = (c >> 1) ^ ((0UL - (c & 1UL)) & 0xEDB88320UL);
    }
    return ~c;
}

int main(void)
{
    static const uint8_t app[16] = {
        0x00, 0x08, 0x00, 0x20, 0x09, 0x12, 0x00, 0x08,
        1, 2, 3, 4, 5, 6, 7, 8
    };
    motor_update_t u;
    motor_flash_ops_t io;
    uint32_t crc = crc32(app, 16U);
    memset(flash, 0xFF, sizeof(flash));
    io.read = flash_read;
    io.invalidate = invalidate;
    io.erase_app = erase_app;
    io.write_app = write_app;
    io.commit = commit;
    motor_update_init(&u, &io);
    assert(motor_update_begin(&u, 99U, MOTOR_HW_COMPAT, 16U, crc) == MOTOR_UPDATE_BAD_ARGUMENT);
    assert(motor_update_begin(&u, MOTOR_BOARD_ID, MOTOR_HW_COMPAT, 16U, crc) == MOTOR_UPDATE_OK);
    assert(!valid);
    assert(motor_update_data(&u, 8U, app + 8U, 8U) == MOTOR_UPDATE_BAD_ARGUMENT);
    assert(motor_update_data(&u, 0U, app, 8U) == MOTOR_UPDATE_OK);
    assert(motor_update_data(&u, 0U, app, 8U) == MOTOR_UPDATE_OK);
    assert(motor_update_end(&u) == MOTOR_UPDATE_BAD_ARGUMENT);
    fail_write = 1;
    assert(motor_update_data(&u, 8U, app + 8U, 8U) == MOTOR_UPDATE_IO_ERROR);
    fail_write = 0;
    assert(motor_update_data(&u, 8U, app + 8U, 8U) == MOTOR_UPDATE_OK);
    assert(motor_update_end(&u) == MOTOR_UPDATE_OK);
    assert(valid && u.complete);
    assert(motor_update_end(&u) == MOTOR_UPDATE_OK);
    assert(motor_update_begin(&u, MOTOR_BOARD_ID, MOTOR_HW_COMPAT, 16U, crc) == MOTOR_UPDATE_OK);
    assert(!valid); /* Interrupted replacement must not keep old validity. */
    assert(motor_update_data(&u, 0U, app, 16U) == MOTOR_UPDATE_OK);
    flash[MOTOR_APP_BASE - MOTOR_FLASH_BASE + 15U] ^= 1U;
    assert(motor_update_end(&u) == MOTOR_UPDATE_BAD_ARGUMENT);
    assert(!valid);
    return 0;
}
