#ifndef IR_TEST_HOST_PROTOCOL_H
#define IR_TEST_HOST_PROTOCOL_H
#include <stdint.h>
uint32_t crc32_update(uint32_t crc, const void *data, uint32_t n);
#endif
