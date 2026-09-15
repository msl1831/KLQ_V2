#ifndef KLQ_USER_PROGRAM_H
#define KLQ_USER_PROGRAM_H

#include <stdbool.h>
#include <stdint.h>

#define USER_PROGRAM_CAPACITY 16384u

typedef enum {
    USER_PROGRAM_EMPTY = 0,
    USER_PROGRAM_DOWNLOADING,
    USER_PROGRAM_READY,
    USER_PROGRAM_RUNNING
} user_program_state_t;

void user_program_init(void);
bool user_program_begin(uint32_t length, uint32_t crc);
bool user_program_write(uint32_t offset, const uint8_t *data, uint32_t length);
bool user_program_finish_download(void);
bool user_program_start(void);
void user_program_stop(void);
void user_program_finish_execution(void);
bool user_program_set_display(const uint8_t columns[13]);
user_program_state_t user_program_state(void);
bool user_program_valid(void);
uint32_t user_program_length(void);
uint32_t user_program_received(void);
const uint8_t *user_program_data(void);

#endif
