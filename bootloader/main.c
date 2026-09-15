#include "board.h"
#include "usb_serial.h"
#include "protocol.h"
#ifdef KLQ_DEMO_APP
#include "peripherals.h"
#include "robot_ui.h"
#include "sc7a20.h"
#include "user_program.h"
#include <stdio.h>
#endif

#ifdef KLQ_DEMO_APP
static void format_angle(char *output, int32_t centidegrees)
{
    uint32_t magnitude = centidegrees < 0 ? (uint32_t)(-centidegrees) : (uint32_t)centidegrees;
    sprintf(output, "%s%lu.%02lu", centidegrees < 0 ? "-" : "",
            (unsigned long)(magnitude / 100u), (unsigned long)(magnitude % 100u));
}
#endif

int main(void)
{
    board_init();
#ifdef KLQ_DEMO_APP
    {
        bool sensor_ready;
        bool sensor_announced;
        uint32_t report_at;
        uint32_t error_at;
        char line[160];

        user_program_init();
        robot_ui_init();
        robot_ui_startup_animation();
        peripherals_uart_init();
        sensor_ready = sc7a20_init();
        sensor_announced = false;
        usb_serial_init();
        report_at = board_ms + 100u;
        error_at = board_ms;
        for (;;) {
            protocol_poll();
            robot_ui_poll();
            if (sensor_ready && (int32_t)(board_ms - report_at) >= 0) {
                sc7a20_sample_t sample;
                report_at = board_ms + 100u;
                if (!sensor_announced) {
                    int length = sprintf(line, "SC7A20 READY ADDR=0x%02X ID=0x%02X VER=0x%02X\r\n",
                                         sc7a20_address(), sc7a20_identity(), sc7a20_version());
                    sensor_announced = usb_serial_write((const uint8_t *)line, (uint32_t)length);
                }
                if (sc7a20_read(&sample)) {
                    char roll[20], pitch[20];
                    int length;
                    format_angle(roll, sample.roll_cdeg);
                    format_angle(pitch, sample.pitch_cdeg);
                    length = sprintf(line, "X=%dmg Y=%dmg Z=%dmg ROLL=%sdeg PITCH=%sdeg\r\n",
                                     sample.x_mg, sample.y_mg, sample.z_mg, roll, pitch);
                    usb_serial_write((const uint8_t *)line, (uint32_t)length);
                }
            } else if (!sensor_ready && (int32_t)(board_ms - error_at) >= 0) {
                static const char error[] = "SC7A20 ERROR: sensor not detected at 0x19 or 0x18\r\n";
                error_at += 1000u;
                usb_serial_write((const uint8_t *)error, sizeof(error) - 1u);
            }
        }
    }
#else
    if (!board_consume_bootloader_request() && protocol_system_firmware_valid())
        board_jump(APP_BASE);
    display_icon(DISPLAY_ICON_DOWNLOAD);
    usb_serial_init();
    /* Stay after an explicit software request, or when system firmware is invalid. */
    for (;;) { protocol_poll(); }
#endif
}
