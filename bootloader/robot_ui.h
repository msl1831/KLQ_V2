#ifndef KLQ_ROBOT_UI_H
#define KLQ_ROBOT_UI_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ROBOT_UI_BOOTING = 0,
    ROBOT_UI_STANDBY,
    ROBOT_UI_DOWNLOADING,
    ROBOT_UI_DOWNLOAD_COMPLETE,
    ROBOT_UI_RUNNING,
    ROBOT_UI_USER_PATTERN,
    ROBOT_UI_ERROR
} robot_ui_state_t;

void robot_ui_init(void);
void robot_ui_startup_animation(void);
void robot_ui_poll(void);
void robot_ui_set_standby(void);
void robot_ui_set_downloading(void);
void robot_ui_set_download_complete(void);
void robot_ui_set_error(void);
void robot_ui_set_running(void);
bool robot_ui_set_user_pattern(const uint8_t columns[13]);
robot_ui_state_t robot_ui_state(void);

#endif
