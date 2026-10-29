#include "../include/update.h"
/* Fixed just after the PY32 vector table, included in the full-image CRC. */
__attribute__((section(".sensor_identity"),used))
const uint32_t sensor_app_identity[4] = {
    0x3144494BUL, SENSOR_BOARD_ID | ((uint32_t)SENSOR_HW_COMPAT << 16), SENSOR_APP_BASE, 1U
};
