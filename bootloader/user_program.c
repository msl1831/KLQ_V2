#include "user_program.h"
#include "protocol.h"
#include "robot_ui.h"
#include <string.h>

static uint8_t program_buffer[USER_PROGRAM_CAPACITY];
static user_program_state_t state;
static uint32_t expected_length;
static uint32_t expected_crc;
static uint32_t received_length;

void user_program_init(void)
{
    state = USER_PROGRAM_EMPTY;
    expected_length = 0;
    expected_crc = 0;
    received_length = 0;
}

bool user_program_begin(uint32_t length, uint32_t crc)
{
    if (!length || length > USER_PROGRAM_CAPACITY) return false;
    state = USER_PROGRAM_DOWNLOADING;
    expected_length = length;
    expected_crc = crc;
    received_length = 0;
    robot_ui_set_downloading();
    return true;
}

bool user_program_write(uint32_t offset, const uint8_t *data, uint32_t length)
{
    if (state != USER_PROGRAM_DOWNLOADING || !length || offset != received_length ||
        length > expected_length - received_length)
        return false;
    memcpy(program_buffer + received_length, data, length);
    received_length += length;
    return true;
}

bool user_program_finish_download(void)
{
    if (state != USER_PROGRAM_DOWNLOADING || received_length != expected_length) return false;
    if (crc32_update(0, program_buffer, expected_length) != expected_crc) {
        state = USER_PROGRAM_EMPTY;
        expected_length = 0;
        received_length = 0;
        robot_ui_set_error();
        return false;
    }
    state = USER_PROGRAM_READY;
    robot_ui_set_download_complete();
    return true;
}

bool user_program_start(void)
{
    if (state != USER_PROGRAM_READY || robot_ui_state() != ROBOT_UI_STANDBY) return false;
    state = USER_PROGRAM_RUNNING;
    robot_ui_set_running();
    /* The Python VM will consume user_program_data() when it is integrated. */
    return true;
}

void user_program_stop(void)
{
    if (state == USER_PROGRAM_RUNNING) state = USER_PROGRAM_READY;
    else if (state == USER_PROGRAM_DOWNLOADING) {
        state = USER_PROGRAM_EMPTY;
        expected_length = 0;
        received_length = 0;
    }
    robot_ui_set_standby();
}

void user_program_finish_execution(void)
{
    if (state == USER_PROGRAM_RUNNING) state = USER_PROGRAM_READY;
    robot_ui_set_standby();
}

bool user_program_set_display(const uint8_t columns[13])
{
    return state == USER_PROGRAM_RUNNING && robot_ui_set_user_pattern(columns);
}

user_program_state_t user_program_state(void) { return state; }
bool user_program_valid(void) { return state == USER_PROGRAM_READY || state == USER_PROGRAM_RUNNING; }
uint32_t user_program_length(void) { return user_program_valid() ? expected_length : 0; }
uint32_t user_program_received(void) { return received_length; }
const uint8_t *user_program_data(void) { return program_buffer; }
