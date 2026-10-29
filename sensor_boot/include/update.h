#ifndef SENSOR_UPDATE_H
#define SENSOR_UPDATE_H

#include <stdint.h>

/* PY32F002BF15U6TR: 24 KiB Flash, 3 KiB SRAM, 128-byte programming pages. */
#define SENSOR_FLASH_BASE       0x08000000UL
#define SENSOR_BOOT_SIZE        0x1800UL
#define SENSOR_APP_BASE         0x08001800UL
#define SENSOR_META_BASE        0x08005F80UL
#define SENSOR_RAM_END          0x20000C00UL
#define SENSOR_APP_CAPACITY     (SENSOR_META_BASE - SENSOR_APP_BASE)
#define SENSOR_FLASH_PAGE_SIZE  128U
#define SENSOR_DATA_MAX         128U
#define SENSOR_ID_OFFSET        0xC0U
#define SENSOR_APP_MIN_SIZE     (SENSOR_ID_OFFSET + 16U)
#include "profile.h"
#define SENSOR_FIRMWARE_VERSION 0x00010000UL

typedef struct {
    int (*read)(uint32_t address, uint8_t *data, uint16_t length);
    int (*invalidate)(void); /* Erase metadata page and verify blank. */
    int (*erase_app)(uint32_t length); /* Only pages inside app range. */
    int (*write_app)(uint32_t address, const uint8_t *data, uint16_t length);
    int (*commit)(uint16_t board_id, uint16_t hw_compat,
                  uint16_t length, uint32_t crc32); /* Verify record after write. */
} sensor_flash_ops_t;

typedef struct {
    sensor_flash_ops_t io;
    uint32_t expected_crc;
    uint16_t length;
    uint16_t next_offset;
    uint8_t active;
    uint8_t complete;
} sensor_update_t;

enum {
    SENSOR_UPDATE_OK = 0,
    SENSOR_UPDATE_BAD_ARGUMENT = 2,
    SENSOR_UPDATE_BUSY = 4,
    SENSOR_UPDATE_IO_ERROR = 6
};

void sensor_update_init(sensor_update_t *u, const sensor_flash_ops_t *io);
uint8_t sensor_identity_valid(const uint8_t *p);
uint8_t sensor_update_begin(sensor_update_t *u, uint16_t board_id,
                           uint16_t hw_compat, uint16_t length, uint32_t crc32);
uint8_t sensor_update_data(sensor_update_t *u, uint16_t offset,
                          const uint8_t *data, uint16_t length);
uint8_t sensor_update_end(sensor_update_t *u);

#endif
