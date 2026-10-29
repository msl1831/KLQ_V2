#include "../usb_host/board.h"
#define DISPLAY_ICON_STANDBY 3u
#define DISPLAY_ICON_RUNNING 4u
#define DISPLAY_ICON_STANDBY_BLINK 5u
#define DISPLAY_ICON_STANDBY_LOOK_LEFT 6u
#define DISPLAY_ICON_STANDBY_LOOK_RIGHT 7u
#define POWER_KEY_DEBOUNCE_MS 25u
#define POWER_KEY_HOLD_MS 1500u
void display_clear(void);
void board_power_off(bool usb_active);
