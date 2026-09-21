#ifndef KLQ_MINI_PYTHON_H
#define KLQ_MINI_PYTHON_H
#include <stdbool.h>
#include <stdint.h>

typedef enum { MINI_IDLE=0, MINI_RUNNING, MINI_DONE, MINI_ERROR } mini_status_t;
typedef enum {
    MINI_ERR_NONE=0, MINI_ERR_SYNTAX, MINI_ERR_NAME, MINI_ERR_ARGUMENT,
    MINI_ERR_RANGE, MINI_ERR_STACK, MINI_ERR_UNSUPPORTED, MINI_ERR_RUNTIME
} mini_error_t;

void mini_python_init(const uint8_t *source, uint32_t length);
bool mini_python_start(void);
mini_status_t mini_python_poll(void);
void mini_python_stop(void);
mini_status_t mini_python_status(void);
mini_error_t mini_python_error(void);
uint16_t mini_python_error_line(void);

#endif
