#ifndef KLQ_VOICE_LINK_H
#define KLQ_VOICE_LINK_H
#include <stdint.h>
typedef struct { uint32_t semantic, received_ms; uint8_t seq, score; } voice_link_intent_t;
typedef struct {
    uint32_t rx_bytes, rx_frames, bad_frames, uart_errors, rx_overflows, tx_frames, tx_errors, timeouts;
    uint32_t last_semantic, play_id, last_rx_ms;
    uint16_t event_count[5], recognition_count, duplicate_count, dropped_intents, play_ack_count, play_done_count;
    uint8_t version[3], version_valid, awake, request, play_state, last_ack, last_error, last_cmd, last_seq;
} voice_link_status_t;
void voice_link_init(void);
void voice_link_poll(void);
uint8_t voice_link_query_version(void);
uint8_t voice_link_play(uint32_t voice_id);
uint8_t voice_link_take_intent(voice_link_intent_t *out);
void voice_link_drop_intents(void);
uint8_t voice_link_take_events(void);
void voice_link_get_status(voice_link_status_t *out);
#ifdef KLQ_VOICE_TEST
void voice_link_test_rx(uint8_t byte);
#endif
#endif
