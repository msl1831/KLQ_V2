#include "board.h"
#include "usb_serial.h"
#include "protocol.h"
#ifdef KLQ_DEMO_APP
#include "audio_link.h"
#include "external_ports.h"
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
        uint32_t port_at;
        uint32_t voice_frames;
        uint32_t voice_errors;
        uint8_t voice_test;
        char line[160];

        user_program_init();
        robot_ui_init();
        robot_ui_startup_animation();
        peripherals_uart_init();
        audio_link_init();
        external_ports_init();
        sensor_ready = sc7a20_init();
        sensor_announced = false;
        usb_serial_init();
        report_at = board_ms + 100u;
        error_at = board_ms;
        port_at = board_ms + 500u;
        voice_frames = voice_errors = 0;
        voice_test = 0;
        for (;;) {
            protocol_poll();
            robot_ui_poll();
            audio_link_poll();
            external_ports_poll();
            if (external_upgrade_active()) {
                report_at=board_ms+100u;
                error_at=board_ms+1000u;
                port_at=board_ms+500u;
                continue;
            }
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
            if ((int32_t)(board_ms - port_at) >= 0) {
                unsigned i;
                const audio_link_status_t *voice = audio_link_status();
                port_at += 500u;
                for (i=0;i<KLQ_EXTERNAL_PORT_COUNT;i++) {
                    const klq_port_status_t *s=external_port_status(i);
                    int length;
                    if (s->online && s->device_class==1u && s->sensor_type==KLQ_SENSOR_IR_DUAL)
                        length=sprintf(line,"P%u ON C=1 T=2 UID=%08lX/%08lX/%08lX L=%u R=%u ST=%u Q=%u SEQ=%lu TO=%lu FE=%lu UE=%lu\r\n",
                            i+1u,(unsigned long)s->uid[0],(unsigned long)s->uid[1],
                            (unsigned long)s->uid[2],s->ir_left,s->ir_right,s->sample_status,
                            s->quality_flags,(unsigned long)s->sample_sequence,
                            (unsigned long)s->timeout_count,(unsigned long)s->frame_error_count,
                            (unsigned long)s->uart_error_count);
                    else if (s->online)
                        length=sprintf(line,"P%u ON C=%u T=%u UID=%08lX/%08lX/%08lX D=%umm TICKS=%u ST=%u SEQ=%lu TO=%lu FE=%lu UE=%lu\r\n",
                            i+1u,s->device_class,s->sensor_type,(unsigned long)s->uid[0],
                            (unsigned long)s->uid[1],(unsigned long)s->uid[2],s->distance_mm,
                            s->echo_ticks,s->sample_status,(unsigned long)s->sample_sequence,
                            (unsigned long)s->timeout_count,(unsigned long)s->frame_error_count,
                            (unsigned long)s->uart_error_count);
                    else
                        length=sprintf(line,"P%u OFF TO=%lu FE=%lu UE=%lu REQ=%lu RESP=%lu\r\n",i+1u,
                            (unsigned long)s->timeout_count,(unsigned long)s->frame_error_count,
                            (unsigned long)s->uart_error_count,(unsigned long)s->request_count,
                            (unsigned long)s->response_count);
                    usb_serial_write((const uint8_t *)line,(uint32_t)length);
                }
                if (voice->rx_frames != voice_frames ||
                    voice->frame_errors + voice->uart_errors != voice_errors ||
                    voice->test_play_attempts != voice_test) {
                    int length = sprintf(line,
                        "VOICE RX=%lu FE=%lu UE=%lu WAKE=%lu ASR=%08lX SCORE=%u NOTIFY=%02X PLAY_ACK=%02X TEST=%u\r\n",
                        (unsigned long)voice->rx_frames, (unsigned long)voice->frame_errors,
                        (unsigned long)voice->uart_errors, (unsigned long)voice->wake_events,
                        (unsigned long)voice->asr_semantic_id, voice->asr_score,
                        voice->last_notify, voice->play_ack, voice->test_play_attempts);
                    usb_serial_write((const uint8_t *)line, (uint32_t)length);
                    voice_frames = voice->rx_frames;
                    voice_errors = voice->frame_errors + voice->uart_errors;
                    voice_test = voice->test_play_attempts;
                }
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
