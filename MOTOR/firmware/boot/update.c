#include "update.h"

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void motor_update_init(motor_update_t *u, const motor_flash_ops_t *io)
{
    u->io = *io;
    u->expected_crc = 0U;
    u->length = u->next_offset = 0U;
    u->active = u->complete = 0U;
}

uint8_t motor_update_begin(motor_update_t *u, uint16_t board_id,
                           uint16_t hw_compat, uint16_t length, uint32_t crc32)
{
    if (board_id != MOTOR_BOARD_ID || hw_compat != MOTOR_HW_COMPAT ||
        length < 8U || length > MOTOR_APP_CAPACITY || (length & 1U))
        return MOTOR_UPDATE_BAD_ARGUMENT;
    if (u->active && !u->complete && u->length == length &&
        u->expected_crc == crc32)
        return MOTOR_UPDATE_OK; /* Lost BEGIN response. */
    u->active = u->complete = 0U;
    if (!u->io.invalidate() || !u->io.erase_app(length))
        return MOTOR_UPDATE_IO_ERROR;
    u->length = length;
    u->expected_crc = crc32;
    u->next_offset = 0U;
    u->active = 1U;
    return MOTOR_UPDATE_OK;
}

uint8_t motor_update_data(motor_update_t *u, uint16_t offset,
                          const uint8_t *data, uint16_t length)
{
    uint8_t check[MOTOR_DATA_MAX];
    uint16_t i;
    if (!u->active || u->complete)
        return MOTOR_UPDATE_BUSY;
    if (!data || !length || length > MOTOR_DATA_MAX ||
        (offset & (MOTOR_FLASH_PAGE_SIZE - 1U)) || (length & 1U) ||
        (uint32_t)offset + length > u->length ||
        (length != MOTOR_FLASH_PAGE_SIZE && (uint32_t)offset + length != u->length))
        return MOTOR_UPDATE_BAD_ARGUMENT;
    if (offset > u->next_offset ||
        (offset < u->next_offset &&
         (uint32_t)offset + length > u->next_offset))
        return MOTOR_UPDATE_BAD_ARGUMENT;
    if (offset == u->next_offset &&
        !u->io.write_app(MOTOR_APP_BASE + offset, data, length)) {
        u->active = 0U; /* Restart BEGIN after a failed page; never program it twice. */
        return MOTOR_UPDATE_IO_ERROR;
    }
    if (!u->io.read(MOTOR_APP_BASE + offset, check, length)) {
        u->active = 0U;
        return MOTOR_UPDATE_IO_ERROR;
    }
    for (i = 0U; i < length; ++i)
        if (check[i] != data[i]) {
            u->active = 0U;
            return MOTOR_UPDATE_IO_ERROR;
        }
    if (offset == u->next_offset)
        u->next_offset = (uint16_t)(offset + length);
    return MOTOR_UPDATE_OK;
}

uint8_t motor_update_end(motor_update_t *u)
{
    uint8_t chunk[MOTOR_DATA_MAX];
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t sp, pc;
    uint16_t offset, count, i;
    uint8_t bit;
    if (!u->active)
        return MOTOR_UPDATE_BUSY;
    if (u->complete)
        return MOTOR_UPDATE_OK; /* Lost END response. */
    if (u->next_offset != u->length)
        return MOTOR_UPDATE_BAD_ARGUMENT;
    if (!u->io.read(MOTOR_APP_BASE, chunk, 8U))
        return MOTOR_UPDATE_IO_ERROR;
    sp = le32(chunk);
    pc = le32(chunk + 4U);
    if ((sp & 7U) || sp <= 0x20000000UL || sp > MOTOR_RAM_END ||
        !(pc & 1U) || (pc & ~1UL) < MOTOR_APP_BASE ||
        (pc & ~1UL) >= MOTOR_APP_BASE + u->length) {
        u->active = 0U;
        return MOTOR_UPDATE_BAD_ARGUMENT;
    }
    for (offset = 0U; offset < u->length; offset = (uint16_t)(offset + count)) {
        count = (uint16_t)(u->length - offset);
        if (count > MOTOR_DATA_MAX) count = MOTOR_DATA_MAX;
        if (!u->io.read(MOTOR_APP_BASE + offset, chunk, count))
            return MOTOR_UPDATE_IO_ERROR;
        for (i = 0U; i < count; ++i) {
            crc ^= chunk[i];
            for (bit = 0U; bit < 8U; ++bit)
                crc = (crc >> 1) ^ ((0UL - (crc & 1UL)) & 0xEDB88320UL);
        }
    }
    if (~crc != u->expected_crc) {
        u->active = 0U;
        return MOTOR_UPDATE_BAD_ARGUMENT;
    }
    if (!u->io.commit(MOTOR_BOARD_ID, MOTOR_HW_COMPAT,
                      u->length, u->expected_crc)) {
        u->active = 0U;
        return MOTOR_UPDATE_IO_ERROR;
    }
    u->complete = 1U;
    return MOTOR_UPDATE_OK;
}
