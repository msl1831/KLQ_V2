#include "robot_ui.h"
#include "board.h"

#define UI_FEEDBACK_MS 800u
#define IDLE_MIN_MS 4500u

static robot_ui_state_t state;
static uint32_t deadline;
static uint16_t rnd = 0xace1u;
static uint8_t phase, gaze;

static void idle_schedule(void)
{
    rnd = (uint16_t)((rnd >> 1) ^ ((0u - (rnd & 1u)) & 0xb400u));
    deadline = board_ms + IDLE_MIN_MS + (rnd & 0xfffu);
    phase = 0;
    gaze = (uint8_t)(rnd & 1u);
}

void robot_ui_init(void)
{
    state = ROBOT_UI_BOOTING;
    deadline = 0;
    phase = gaze = 0;
    display_clear();
}

void robot_ui_startup_animation(void)
{
    static const uint8_t glow[4][13] = {
        {0,0,0,0,0,0,0x08,0,0,0,0,0,0},
        {0,0,0,0,0,0x08,0x1c,0x08,0,0,0,0,0},
        {0,0,0,0,0x08,0x1c,0x3e,0x1c,0x08,0,0,0,0},
        {0,0,0x08,0x1c,0x3e,0x3e,0x7f,0x3e,0x3e,0x1c,0x08,0,0}
    };
    int frame;
    state = ROBOT_UI_BOOTING;
    for (frame = 0; frame < 4; ++frame) { display_columns(glow[frame]); delay_ms(70); }
    for (frame = 2; frame >= 0; --frame) { display_columns(glow[frame]); delay_ms(55); }
    display_clear();
    delay_ms(45);
    display_icon(DISPLAY_ICON_STANDBY_BLINK);
    delay_ms(170);
    robot_ui_set_standby();
}

void robot_ui_poll(void)
{
    if ((int32_t)(board_ms - deadline) < 0) return;
    if (state == ROBOT_UI_DOWNLOAD_COMPLETE || state == ROBOT_UI_ERROR) {
        robot_ui_set_standby();
    } else if (state == ROBOT_UI_STANDBY) {
        if (++phase == 4u) robot_ui_set_standby();
        else {
            display_icon(phase==1u ? (gaze ? DISPLAY_ICON_STANDBY_LOOK_RIGHT :
                                      DISPLAY_ICON_STANDBY_LOOK_LEFT) :
                         phase==2u ? DISPLAY_ICON_STANDBY : DISPLAY_ICON_STANDBY_BLINK);
            deadline = board_ms + (phase==1u ? 320u : phase==2u ? 120u : 90u);
        }
    }
}

void robot_ui_set_standby(void)
{
    state = ROBOT_UI_STANDBY;
    display_icon(DISPLAY_ICON_STANDBY);
    idle_schedule();
}

void robot_ui_set_downloading(void)
{
    state = ROBOT_UI_DOWNLOADING;
    deadline = 0;
    display_icon(DISPLAY_ICON_DOWNLOAD);
}

void robot_ui_set_download_complete(void)
{
    state = ROBOT_UI_DOWNLOAD_COMPLETE;
    deadline = board_ms + UI_FEEDBACK_MS;
    display_icon(DISPLAY_ICON_COMPLETE);
}

void robot_ui_set_error(void)
{
    state = ROBOT_UI_ERROR;
    deadline = board_ms + UI_FEEDBACK_MS;
    display_icon(DISPLAY_ICON_ERROR);
}

void robot_ui_set_running(void)
{
    state = ROBOT_UI_RUNNING;
    deadline = 0;
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
