#include "external_ports.h"
#include "board.h"
#include "protocol.h"
#include "apm32e10x.h"
#include "apm32e10x_usart.h"
#include <string.h>

#define MAGIC 0x31514c4bu
#define CMD_INFO 0x0001u
#define CMD_SAMPLE 0x0101u
#define RESP 0x8000u
#define RX_N 128u
#define FRAME_N 148u
#define REQ_TIMEOUT 40u
#define DISCOVER_MS 500u
#define SAMPLE_IDLE_MS 100u
#define SAMPLE_ACTIVE_MS 40u
#define RETRIES 2u

typedef struct {
    USART_T *uart;
    volatile uint8_t rx[RX_N], rh, rt;
    uint8_t frame[FRAME_N], used, expected, pending, retry, bad;
    uint16_t command;
    uint32_t sequence, deadline, next_at, last_byte;
    klq_port_status_t s;
} Port;

static Port p[KLQ_EXTERNAL_PORT_COUNT];
static uint8_t active;
static USART_T * const uart[KLQ_EXTERNAL_PORT_COUNT] = {USART1, USART2, USART3};

static uint16_t g16(const uint8_t *q) { return q[0] | ((uint16_t)q[1] << 8); }
static uint32_t g32(const uint8_t *q) { return q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24); }
static void p16(uint8_t *q, uint16_t v) { q[0]=v; q[1]=v>>8; }
static void p32(uint8_t *q, uint32_t v) { q[0]=v; q[1]=v>>8; q[2]=v>>16; q[3]=v>>24; }

static void unbind(Port *x)
{
    x->s.online=x->s.mode=x->s.device_class=0;
    x->s.board_id=x->s.hw_compat=x->s.sensor_type=0;
    x->s.uid[0]=x->s.uid[1]=x->s.uid[2]=0;
}

static void bad_frame(Port *x)
{
    ++x->s.frame_error_count;
    if (++x->bad>=3u) {
        unbind(x); x->pending=x->bad=x->used=x->expected=0;
        x->next_at=board_ms+DISCOVER_MS;
    }
}

static void send(Port *x, uint8_t retry)
{
    uint8_t f[16];
    unsigned i;
    p32(f,MAGIC); p32(f+4,x->sequence); p16(f+8,x->command); p16(f+10,0);
    p32(f+12,crc32_update(0,f,12));
    for (i=0;i<16;i++) {
        uint32_t t=board_ms;
        while (!x->uart->STS_B.TXBEFLG && board_ms-t<2u) {}
        if (!x->uart->STS_B.TXBEFLG) {
            ++x->s.uart_error_count; x->pending=0; x->next_at=board_ms+DISCOVER_MS; return;
        }
        x->uart->DATA=(uint8_t)f[i];
    }
    x->pending=1; x->retry=retry; x->deadline=board_ms+REQ_TIMEOUT; ++x->s.request_count;
}

static void request(Port *x, uint16_t command)
{
    x->command=command; ++x->sequence; x->used=x->expected=0; send(x,0);
}

static void valid(Port *x)
{
    const uint8_t *d=x->frame+20;
    uint16_t n=g16(x->frame+10);
    if (!x->pending || g32(x->frame+16) || g32(x->frame+4)!=x->sequence ||
        g16(x->frame+8)!=(uint16_t)(x->command|RESP)) goto bad;
    if (x->command==CMD_INFO) {
        if (n!=40u || g16(d)!=1u) goto bad;
        x->s.mode=d[2]; x->s.device_class=d[3];
        x->s.board_id=g16(d+4); x->s.hw_compat=g16(d+6); x->s.sensor_type=g16(d+8);
        x->s.fw_version=g32(d+12); x->s.capabilities=g32(d+16);
        x->s.uid[0]=g32(d+24); x->s.uid[1]=g32(d+28); x->s.uid[2]=g32(d+32);
        x->s.online=1;
    } else if (x->command==CMD_SAMPLE) {
        if (n!=20u) goto bad;
        x->s.sample_sequence=g32(d); x->s.capture_time_ms=g32(d+4);
        x->s.sample_host_ms=board_ms;
        x->s.echo_ticks=g16(d+8); x->s.distance_mm=g16(d+10); x->s.sample_status=d[12];
        x->s.quality_flags=d[13]; x->s.driver_state=d[14];
    } else goto bad;
    x->pending=x->bad=0; ++x->s.response_count;
    x->next_at=board_ms+((x->s.device_class==1u && x->s.sensor_type==1u &&
                         (x->s.capabilities&1u)) ? (active?SAMPLE_ACTIVE_MS:SAMPLE_IDLE_MS) : DISCOVER_MS);
    return;
bad:
    bad_frame(x);
}

static void byte_in(Port *x, uint8_t b)
{
    static const uint8_t m[4]={'K','L','Q','1'};
    uint16_t n;
    x->last_byte=board_ms;
    if (x->used<4u && b!=m[x->used]) { x->used=(b=='K'); if (x->used) x->frame[0]=b; return; }
    x->frame[x->used++]=b;
    if (x->used==16u) {
        n=g16(x->frame+10);
        if (n>132u) { bad_frame(x); x->used=0; return; }
        x->expected=(uint8_t)(16u+n);
    }
    if (x->expected && x->used==x->expected) {
        uint32_t c=crc32_update(0,x->frame,12);
        c=crc32_update(c,x->frame+16,x->expected-16u);
        if (c==g32(x->frame+12)) valid(x); else bad_frame(x);
        x->used=x->expected=0;
    }
}

static void irq(unsigned i)
{
    Port *x=&p[i]; uint32_t s=x->uart->STS; uint8_t b;
    if (!(s&0x2fu)) return;
    b=(uint8_t)x->uart->DATA;
    if (s&0x0fu) { ++x->s.uart_error_count; return; }
    if (s&USART_FLAG_RXBNE) {
        uint8_t h=x->rh, next=(uint8_t)((h+1u)&(RX_N-1u));
        if (next==x->rt) ++x->s.uart_error_count;
        else { x->rx[h]=b; x->rh=next; }
    }
}

void USART1_IRQHandler(void) { irq(0); }
void USART2_IRQHandler(void) { irq(1); }
void USART3_IRQHandler(void) { irq(2); }

void external_ports_init(void)
{
    unsigned i;
    memset(p,0,sizeof(p)); active=0;
    for (i=0;i<KLQ_EXTERNAL_PORT_COUNT;i++) {
        p[i].uart=uart[i]; p[i].sequence=i<<24; p[i].next_at=board_ms+50u*i;
        USART_EnableInterrupt(uart[i],USART_INT_RXBNE);
        USART_EnableInterrupt(uart[i],USART_INT_ERR);
        USART_EnableInterrupt(uart[i],USART_INT_PE);
        NVIC_SetPriority((IRQn_Type)(USART1_IRQn+i),1);
        NVIC_EnableIRQ((IRQn_Type)(USART1_IRQn+i));
    }
}

void external_ports_set_active(bool value) { active=value; }

void external_ports_poll(void)
{
    unsigned i;
    for (i=0;i<KLQ_EXTERNAL_PORT_COUNT;i++) {
        Port *x=&p[i];
        while (x->rt!=x->rh) { uint8_t t=x->rt; byte_in(x,x->rx[t]); x->rt=(uint8_t)((t+1u)&(RX_N-1u)); }
        if (x->used && board_ms-x->last_byte>20u) { x->used=x->expected=0; bad_frame(x); }
        if (x->pending && (int32_t)(board_ms-x->deadline)>=0) {
            ++x->s.timeout_count; x->used=x->expected=0;
            if (x->retry<RETRIES) send(x,(uint8_t)(x->retry+1u));
            else { x->pending=0; unbind(x); x->next_at=board_ms+DISCOVER_MS; }
        } else if (!x->pending && (int32_t)(board_ms-x->next_at)>=0) {
            request(x,(x->s.online && x->s.device_class==1u && x->s.sensor_type==1u &&
                       (x->s.capabilities&1u)) ? CMD_SAMPLE : CMD_INFO);
        }
    }
}

const klq_port_status_t *external_port_status(unsigned port)
{
    return port<KLQ_EXTERNAL_PORT_COUNT ? &p[port].s : 0;
}

/* Motor device class/type and command IDs are intentionally not guessed. */
bool external_port_motor(unsigned port, int direction, bool run, unsigned power)
{
    (void)port; (void)direction; (void)run; (void)power;
    return false;
}

void external_ports_motor_stop_all(void)
{
    unsigned i;
    for (i=0;i<KLQ_EXTERNAL_PORT_COUNT;i++) external_port_motor(i,0,false,0);
}
