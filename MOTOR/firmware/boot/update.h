#ifndef MOTOR_UPDATE_H
#define MOTOR_UPDATE_H

#include <stdint.h>

/* PY32F002BF15U6TR: 24 KiB Flash, 3 KiB SRAM, 128-byte programming pages. */
#define MOTOR_FLASH_BASE       0x08000000UL
#define MOTOR_BOOT_SIZE        0x1800UL
#define MOTOR_APP_BASE         0x08001800UL
#define MOTOR_META_BASE        0x08005F80UL
#define MOTOR_RAM_END          0x20000C00UL
#define MOTOR_APP_CAPACITY     (MOTOR_META_BASE - MOTOR_APP_BASE)
#define MOTOR_FLASH_PAGE_SIZE  128U
#define MOTOR_DATA_MAX         128U
#define MOTOR_BOARD_ID         2U
#define MOTOR_HW_COMPAT        2U
#define MOTOR_FIRMWARE_VERSION 0x00020000UL

typedef struct {
    int (*read)(uint32_t address, uint8_t *data, uint16_t length);
    int (*invalidate)(void); /* Erase metadata page and verify blank. */
    int (*erase_app)(uint32_t length); /* Only pages inside app range. */
    int (*write_app)(uint32_t address, const uint8_t *data, uint16_t length);
    int (*commit)(uint16_t board_id, uint16_t hw_compat,
                  uint16_t length, uint32_t crc32); /* Verify record after write. */
} motor_flash_ops_t;

typedef struct {
    motor_flash_ops_t io;
    uint32_t expected_crc;
    uint16_t length;
    uint16_t next_offset;
    uint8_t active;
    uint8_t complete;
} motor_update_t;

enum {
    MOTOR_UPDATE_OK = 0,
    MOTOR_UPDATE_BAD_ARGUMENT = 2,
    MOTOR_UPDATE_BUSY = 4,
    MOTOR_UPDATE_IO_ERROR = 6
};

void motor_update_init(motor_update_t *u, const motor_flash_ops_t *io);
uint8_t motor_update_begin(motor_update_t *u, uint16_t board_id,
                           uint16_t hw_compat, uint16_t length, uint32_t crc32);
uint8_t motor_update_data(motor_update_t *u, uint16_t offset,
                          const uint8_t *data, uint16_t length);
uint8_t motor_update_end(motor_update_t *u);

#endif
