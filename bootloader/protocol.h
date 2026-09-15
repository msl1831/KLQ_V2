#ifndef KLQ_PROTOCOL_H
#define KLQ_PROTOCOL_H
#include <stdint.h>
#include <stdbool.h>
#define FRAME_MAGIC 0x31514c4bu /* KLQ1 */
#define MAX_PAYLOAD 1028u
enum {
    CMD_INFO = 1,
    CMD_BEGIN,
    CMD_DATA,
    CMD_END,
    CMD_RUN,
    CMD_ECHO,
    CMD_RESET,
    CMD_DISPLAY,
    CMD_STOP,
    CMD_FINISH
};
enum { OK=0, ERR_COMMAND, ERR_LENGTH, ERR_CRC, ERR_STATE, ERR_RANGE, ERR_FLASH, ERR_IMAGE };
uint32_t crc32_update(uint32_t crc, const void *data, uint32_t n);
bool protocol_system_firmware_valid(void);
void protocol_poll(void);
#endif
