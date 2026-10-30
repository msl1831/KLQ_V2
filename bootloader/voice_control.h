#ifndef KLQ_VOICE_CONTROL_H
#define KLQ_VOICE_CONTROL_H
#include <stdint.h>
enum { VOICE_CTL_IDLE, VOICE_CTL_WAIT_START, VOICE_CTL_RUNNING, VOICE_CTL_WAIT_STOP };
enum { VOICE_CTL_OK, VOICE_CTL_DISABLED, VOICE_CTL_BUSY, VOICE_CTL_ASLEEP,
       VOICE_CTL_VERSION, VOICE_CTL_NO_MOTOR, VOICE_CTL_MULTIPLE, VOICE_CTL_DRIVER,
       VOICE_CTL_IDENTITY, VOICE_CTL_TIMEOUT, VOICE_CTL_STALE, VOICE_CTL_DUPLICATE,
       VOICE_CTL_UNSUPPORTED, VOICE_CTL_CANCELLED };
typedef struct {
    uint32_t last_semantic, accepted, rejected, completed, errors, cancelled;
    uint16_t pending_sound;
    int16_t pwm;
    uint8_t enabled, active, phase, port, last_reason, last_seq, last_score;
} voice_control_status_t;
void voice_control_init(void);
void voice_control_poll(void);
uint8_t voice_control_active(void);
void voice_control_cancel(void);
void voice_control_set_enabled(uint8_t enabled);
void voice_control_sound(uint16_t voice_id);
void voice_control_get_status(voice_control_status_t *out);
#endif
