#ifndef KLQ_AUDIO_LINK_H
#define KLQ_AUDIO_LINK_H

#include <stdint.h>

typedef struct {
    uint32_t rx_frames;
    uint32_t frame_errors;
    uint32_t uart_errors;
    uint32_t wake_events;
    uint32_t asr_semantic_id;
    uint8_t asr_score;
    uint8_t last_notify;
    uint8_t play_ack;
    uint8_t test_play_attempts;
} audio_link_status_t;

void audio_link_init(void);
void audio_link_poll(void);
const audio_link_status_t *audio_link_status(void);

#endif
