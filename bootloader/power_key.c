#include "power_key.h"
#include "board.h"
#include "user_program.h"
#include "robot_ui.h"
#include "klq_runtime.h"
#include "apm32e10x_gpio.h"
#include "apm32e10x_rcm.h"

static uint32_t changed_at, pressed_at;
static uint16_t short_count;
static uint8_t raw, stable, armed, action;

uint8_t power_key_raw_level(void) { return GPIO_ReadInputBit(GPIOC,GPIO_PIN_4)!=0; }
uint8_t power_key_pressed(void) { return stable; }
uint16_t power_key_short_count(void) { return short_count; }

void power_key_init(void)
{
    GPIO_Config_T io;
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_GPIOC);
    io.pin=GPIO_PIN_4;
    io.mode=GPIO_MODE_IN_PU;
    io.speed=GPIO_SPEED_2MHz;
    GPIO_Config(GPIOC,&io);
    raw=stable=(uint8_t)!power_key_raw_level();
    armed=action=0;
    changed_at=pressed_at=board_ms;
    short_count=0;
}

void power_key_poll(void)
{
    uint8_t v=(uint8_t)!power_key_raw_level();
    uint32_t now=board_ms;
    if (v!=raw) { raw=v; changed_at=now; }
    if (v!=stable && now-changed_at>=POWER_KEY_DEBOUNCE_MS) {
        stable=v;
        if (v) {
            pressed_at=now;
            if (user_program_state()==USER_PROGRAM_RUNNING) action=2;
            else if (user_program_state()==USER_PROGRAM_READY &&
                     robot_ui_state()==ROBOT_UI_STANDBY) action=1;
            else if (user_program_state()==USER_PROGRAM_EMPTY) action=3;
            else action=0;
        } else {
            if (armed && now-pressed_at<POWER_KEY_HOLD_MS) {
                ++short_count;
                if (action==2 && user_program_state()==USER_PROGRAM_RUNNING) user_program_stop();
                else if (action==1 && user_program_state()==USER_PROGRAM_READY) user_program_start();
                else if (action==3 && user_program_state()==USER_PROGRAM_EMPTY) robot_ui_set_error();
            }
            armed=1;
            action=0;
        }
    }
    if (!stable && !armed && now-changed_at>=POWER_KEY_DEBOUNCE_MS) armed=1;
    if (stable && armed && now-pressed_at>=POWER_KEY_HOLD_MS) {
        action=0;
        user_program_stop();
        klq_runtime_stop_all();
        board_power_off(true);
    }
}
