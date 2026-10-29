#include "audio_link.h"
#include "board.h"
#include "apm32e10x.h"
#include "apm32e10x_usart.h"
#include <string.h>

#define RX_SIZE 64u
#define FRAME_SIZE 30u
#define PLAY_DELAY_MS 5000u
#define PLAY_RETRY_MS 1500u
#define PLAY_MAX_ATTEMPTS 3u
#define VMUP_HEAD0 0xA5u
#define VMUP_HEAD1 0xFCu
#define VMUP_TAIL 0xFBu
#define VMUP_CMD_UP 0xA0u
#define VMUP_CMD_DOWN 0xA1u
#define VMUP_ACK 0xA2u
#define VMUP_NOTIFY 0xA3u
#define VMUP_ASR_RESULT 0x91u
#define VMUP_PLAY_VOICE 0x92u
#define VMUP_NOTIFY_STATUS 0x9Au
#define VMUP_WAKEUP_ENTER 0xB1u
#define VMUP_PLAY_START 0x80u
#define VMUP_PLAY_BY_CMD_ID 0x92u
#define TEST_PROMPT_CMD_ID 199u

static volatile uint8_t rx[RX_SIZE], rx_head, rx_tail;
static uint8_t frame[FRAME_SIZE], used, expected, sequence;
static uint32_t last_byte_ms, play_at;
static audio_link_status_t status;

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint16_t checksum(const uint8_t *p, uint8_t length)
{
    uint16_t sum = 0;
    while (length--) sum = (uint16_t)(sum + *p++);
    return sum;
}

static uint8_t send_frame(uint8_t type, uint8_t command, uint8_t seq,
                          const uint8_t *data, uint8_t length)
{
    uint8_t tx[FRAME_SIZE];
    uint16_t sum;
    uint8_t i, total = (uint8_t)(length + 10u);

    if (length > 20u) return 0;
    tx[0] = VMUP_HEAD0;
    tx[1] = VMUP_HEAD1;
    put16(tx + 2, length);
    tx[4] = type;
    tx[5] = command;
    tx[6] = seq;
    if (length) memcpy(tx + 7, data, length);
    sum = checksum(tx + 4, (uint8_t)(length + 3u));
    put16(tx + 7 + length, sum);
    tx[9 + length] = VMUP_TAIL;

    for (i = 0; i < total; ++i) {
        uint32_t start = board_ms;
        while (!UART4->STS_B.TXBEFLG && board_ms - start < 2u) {}
        if (!UART4->STS_B.TXBEFLG) {
            ++status.uart_errors;
            return 0;
        }
        UART4->DATA = tx[i];
    }
    return 1;
}

static void send_asr_ack(uint8_t seq)
{
    static const uint8_t ok = 0;
    send_frame(VMUP_ACK, VMUP_ASR_RESULT, seq, &ok, 1);
}

static void send_test_play(void)
{
    uint8_t data[6];
    data[0] = VMUP_PLAY_START;
    data[1] = VMUP_PLAY_BY_CMD_ID;
    put32(data + 2, TEST_PROMPT_CMD_ID);
    if (send_frame(VMUP_CMD_DOWN, VMUP_PLAY_VOICE, sequence++, data, sizeof(data)))
        ++status.test_play_attempts;
    play_at = board_ms + PLAY_RETRY_MS;
}

static void process_frame(void)
{
    uint8_t length = (uint8_t)get16(frame + 2);
    uint8_t type = frame[4], command = frame[5], seq = frame[6];
    const uint8_t *data = frame + 7;

    ++status.rx_frames;
    if (type == VMUP_NOTIFY && command == VMUP_NOTIFY_STATUS && length == 2u) {
        status.last_notify = data[1];
        if (data[1] == VMUP_WAKEUP_ENTER) ++status.wake_events;
    } else if (type == VMUP_CMD_UP && command == VMUP_ASR_RESULT && length == 7u) {
        status.asr_semantic_id = get32(data);
        status.asr_score = data[6];
        send_asr_ack(seq);
    } else if (type == VMUP_ACK && command == VMUP_PLAY_VOICE && length == 1u) {
        status.play_ack = data[0];
    }
}

static void byte_in(uint8_t value)
{
    uint8_t length;

    last_byte_ms = board_ms;
    if (!used) {
        if (value == VMUP_HEAD0) frame[used++] = value;
        return;
    }
    if (used == 1u && value != VMUP_HEAD1) {
        used = (value == VMUP_HEAD0);
        return;
    }
    frame[used++] = value;
    if (used == 4u) {
        length = (uint8_t)get16(frame + 2);
        if (get16(frame + 2) > 20u) {
            used = 0;
            ++status.frame_errors;
            return;
        }
        expected = (uint8_t)(length + 10u);
    }
    if (expected && used == expected) {
        length = (uint8_t)get16(frame + 2);
        if (frame[expected - 1u] == VMUP_TAIL &&
            get16(frame + 7u + length) == checksum(frame + 4, (uint8_t)(length + 3u)))
            process_frame();
        else
            ++status.frame_errors;
        used = expected = 0;
    }
}

void UART4_IRQHandler(void)
{
    uint32_t flags = UART4->STS;
    uint8_t value;
    if (!(flags & 0x2fu)) return;
    value = (uint8_t)UART4->DATA;
    if (flags & 0x0fu) {
        ++status.uart_errors;
        return;
    }
    if (flags & USART_FLAG_RXBNE) {
        uint8_t head = rx_head;
        uint8_t next = (uint8_t)((head + 1u) & (RX_SIZE - 1u));
        if (next == rx_tail) ++status.uart_errors;
        else {
            rx[head] = value;
            rx_head = next;
        }
    }
}

void audio_link_init(void)
{
    memset(&status, 0, sizeof(status));
    rx_head = rx_tail = used = expected = sequence = 0;
    play_at = board_ms + PLAY_DELAY_MS;
    USART_EnableInterrupt(UART4, USART_INT_RXBNE);
    USART_EnableInterrupt(UART4, USART_INT_ERR);
    USART_EnableInterrupt(UART4, USART_INT_PE);
    NVIC_SetPriority(UART4_IRQn, 1);
    NVIC_EnableIRQ(UART4_IRQn);
}

void audio_link_poll(void)
{
    while (rx_tail != rx_head) {
        uint8_t tail = rx_tail;
        byte_in(rx[tail]);
        rx_tail = (uint8_t)((tail + 1u) & (RX_SIZE - 1u));
    }
    if (used && board_ms - last_byte_ms > 20u) {
        used = expected = 0;
        ++status.frame_errors;
    }
    if (status.play_ack != VMUP_PLAY_START &&
        status.test_play_attempts < PLAY_MAX_ATTEMPTS &&
        (int32_t)(board_ms - play_at) >= 0)
        send_test_play();
}

const audio_link_status_t *audio_link_status(void)
{
    return &status;
}
