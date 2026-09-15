#include "robot_ui.h"
#include "board.h"

#define UI_FEEDBACK_MS 800u

static robot_ui_state_t state;
static uint32_t feedback_until;

static void show_sweep_frame(unsigned head)
{
    uint8_t columns[13] = {0};
    if (head < 13u) columns[head] = 0x7fu;
    if (head > 0u && head - 1u < 13u) columns[head - 1u] = 0x08u;
    if (head > 1u && head - 2u < 13u) columns[head - 2u] = 0x08u;
    display_columns(columns);
}

void robot_ui_init(void)
{
    state = ROBOT_UI_BOOTING;
    feedback_until = 0;
    display_clear();
}

void robot_ui_startup_animation(void)
{
    unsigned frame;
    state = ROBOT_UI_BOOTING;
    for (frame = 0; frame < 15u; ++frame) {
        show_sweep_frame(frame);
        delay_ms(40);
    }
    display_icon(DISPLAY_ICON_STANDBY);
    delay_ms(160);
    display_clear();
    delay_ms(80);
    robot_ui_set_standby();
}

void robot_ui_poll(void)
{
    if ((state == ROBOT_UI_DOWNLOAD_COMPLETE || state == ROBOT_UI_ERROR) &&
        (int32_t)(board_ms - feedback_until) >= 0)
        robot_ui_set_standby();
}

void robot_ui_set_standby(void)
{
    state = ROBOT_UI_STANDBY;
    feedback_until = 0;
    display_icon(DISPLAY_ICON_STANDBY);
}

void robot_ui_set_downloading(void)
{
    state = ROBOT_UI_DOWNLOADING;
    feedback_until = 0;
    display_icon(DISPLAY_ICON_DOWNLOAD);
}

void robot_ui_set_download_complete(void)
{
    state = ROBOT_UI_DOWNLOAD_COMPLETE;
    feedback_until = board_ms + UI_FEEDBACK_MS;
    display_icon(DISPLAY_ICON_COMPLETE);
}

void robot_ui_set_error(void)
{
    state = ROBOT_UI_ERROR;
    feedback_until = board_ms + UI_FEEDBACK_MS;
    display_icon(DISPLAY_ICON_ERROR);
}

void robot_ui_set_running(void)
{
    state = ROBOT_UI_RUNNING;
    feedback_until = 0;
    display_icon(DISPLAY_ICON_RUNNING);
}

bool robot_ui_set_user_pattern(const uint8_t columns[13])
{
    if (state != ROBOT_UI_RUNNING && state != ROBOT_UI_USER_PATTERN) return false;
    state = ROBOT_UI_USER_PATTERN;
    display_columns(columns);
    return true;
}

robot_ui_state_t robot_ui_state(void) { return state; }
