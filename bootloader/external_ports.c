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
#define IR_SAMPLE_MS 20u
#define RETRIES 2u
#define CMD_MOTOR_SET 0x0300u
#define CMD_MOTOR_STOP 0x0302u
#define MOTOR_REFRESH_MS 60u
#define MOTOR_LEASE_MS 150u

typedef struct {
    USART_T *uart;
    volatile uint8_t rx[RX_N], rh, rt;
    uint8_t frame[FRAME_N], used, expected, pending, retry, bad;
    uint16_t command;
    uint32_t sequence, deadline, next_at, last_byte, request_at;
    int16_t motor_goal, motor_sent, motor_ack;
    uint32_t motor_refresh, motor_lease, motor_uart_errors;
    uint8_t motor_stop, motor_fault, motor_need_set, motor_tx_n, motor_tx_pos, motor_tx[18];
    klq_port_status_t s;
} Port;

static Port p[KLQ_EXTERNAL_PORT_COUNT];
static uint8_t active;
static USART_T * const uart[KLQ_EXTERNAL_PORT_COUNT] = {USART1, USART2, USART3};

enum { UP_IDLE, UP_RAW, UP_STOP, UP_HOLD, UP_RESET };
typedef struct {
    uint32_t token, activity, deadline, drain_at, uid[3], uart_errors;
    uint16_t board, hw, raw_n, reply_n, pos, image_n;
    uint8_t active, port, result, reason, stage, waiting, retry;
    uint8_t pinned, boot_id, layout, mode, hold_for, draining, motor_control, pre_stop;
    uint8_t raw[FRAME_N], reply[56], internal[16];
} Upgrade;
static Upgrade up;
static uint32_t closed_token;

static uint16_t g16(const uint8_t *q) { return q[0] | ((uint16_t)q[1] << 8); }
static uint32_t g32(const uint8_t *q) { return q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24); }
static void p16(uint8_t *q, uint16_t v) { q[0]=v; q[1]=v>>8; }
static void p32(uint8_t *q, uint32_t v) { q[0]=v; q[1]=v>>8; q[2]=v>>16; q[3]=v>>24; }

static uint8_t motor_identity(const Port *x)
{
    return x->s.online && x->s.mode==1u && x->s.device_class==2u &&
           x->s.board_id==2u && x->s.hw_compat==2u && x->s.sensor_type==2u &&
           (x->s.capabilities&1u) && (x->s.uid[0] || x->s.uid[1] || x->s.uid[2]);
}

static uint8_t sample_supported(const Port *x)
{
    return x->s.online && x->s.mode==1u && x->s.device_class==1u &&
           (x->s.sensor_type==KLQ_SENSOR_CS100A || x->s.sensor_type==KLQ_SENSOR_IR_DUAL) &&
           (x->s.capabilities&1u);
}

static uint32_t sample_period(const Port *x)
{
    return x->s.sensor_type==KLQ_SENSOR_IR_DUAL ? IR_SAMPLE_MS : (active?SAMPLE_ACTIVE_MS:SAMPLE_IDLE_MS);
}

static void clear_sample(Port *x)
{
    x->s.sample_valid=x->s.sample_status=x->s.quality_flags=x->s.driver_state=0;
    x->s.sample_sequence=x->s.capture_time_ms=x->s.sample_host_ms=0;
    x->s.echo_ticks=x->s.distance_mm=0;
    x->s.ir_left_delta=x->s.ir_right_delta=0;
    x->s.ir_left=x->s.ir_right=0;
    x->s.ir_left_ambient=x->s.ir_left_active=x->s.ir_right_ambient=x->s.ir_right_active=0;
}

static void unbind(Port *x)
{
    x->s.online=x->s.mode=x->s.device_class=0;
    x->s.board_id=x->s.hw_compat=x->s.sensor_type=0;
    x->s.uid[0]=x->s.uid[1]=x->s.uid[2]=0;
    x->s.fw_version=x->s.capabilities=0;
    clear_sample(x);
}

static void motor_stop_queue(Port *x,uint8_t fault)
{
    uint8_t partial;
    if (x->motor_stop) {
        x->motor_goal=0; x->motor_need_set=0u; x->motor_fault=fault;
        x->motor_uart_errors=x->s.uart_error_count; return;
    }
    partial=(uint8_t)(x->motor_tx_n && x->motor_tx_pos);
    x->motor_goal=0; x->motor_stop=1u; x->motor_need_set=0u; x->motor_fault=fault;
    x->motor_tx_n=x->motor_tx_pos=x->pending=x->retry=x->used=x->expected=0u;
    x->rt=x->rh; x->motor_refresh=board_ms+(partial?25u:0u);
    x->motor_uart_errors=x->s.uart_error_count;
}

static void motor_fail(Port *x)
{
    if (x->pending && x->command==CMD_MOTOR_STOP) {
        x->motor_goal=x->motor_stop=x->motor_tx_n=x->pending=0u; x->motor_fault=1u;
    } else motor_stop_queue(x,1u);
    unbind(x); x->next_at=board_ms+DISCOVER_MS;
}

static uint8_t motor_health(Port *x)
{
    if (x->motor_goal && (!motor_identity(x) || (int32_t)(board_ms-x->motor_lease)>=0 ||
        x->s.uart_error_count!=x->motor_uart_errors)) motor_fail(x);
    return (uint8_t)!x->motor_fault;
}

static void up_fail(uint8_t reason)
{
    if (up.stage==UP_STOP) { up.pre_stop=0u; p[up.port].motor_fault=1u; }
    if (reason==3u || (up.raw_n && g16(up.raw+8u)==1u)) {
        up.boot_id=up.layout=0u; up.mode=2u; up.image_n=0u;
    }
    up.result=2u; up.reason=reason; up.stage=UP_IDLE; up.waiting=0u; up.reply_n=0u;
    p[up.port].used=p[up.port].expected=0u;
}

static void up_done(void)
{
    up.result=1u; up.stage=UP_IDLE; up.waiting=0u;
}

static void up_start(uint8_t stage)
{
    Port *x=&p[up.port];
    up.stage=stage; up.waiting=up.retry=0u; up.pos=0u; up.deadline=board_ms+250u;
    x->used=x->expected=x->bad=0u;
    if (stage!=UP_RAW) {
        p32(up.internal,MAGIC); p32(up.internal+4,++x->sequence);
        p16(up.internal+8,(uint16_t)(stage==UP_STOP ? 0x0302u : 0x0009u)); p16(up.internal+10,0u);
        p32(up.internal+12,crc32_update(0,up.internal,12u));
    }
}

static void up_synth_enter(void)
{
    memcpy(up.reply,up.raw,12u); p16(up.reply+8,0x81f0u); p16(up.reply+10,4u);
    p32(up.reply+16,0u);
    p32(up.reply+12,crc32_update(crc32_update(0,up.reply,12u),up.reply+16,4u));
    up.reply_n=20u;
}

static void up_valid(Port *x)
{
    const uint8_t *f=x->frame, *d=f+20u;
    const uint8_t *request=up.stage==UP_RAW ? up.raw : up.internal;
    uint16_t n=g16(f+10u), cmd=g16(request+8u), total=(uint16_t)(16u+n);
    uint32_t status;
    uint8_t i;
    if (!up.waiting || up.stage==UP_IDLE || up.stage==UP_RESET ||
        g32(f+4u)!=g32(request+4u) || g16(f+8u)!=(uint16_t)(cmd|RESP)) return;
    if (n<4u || total>56u) { up_fail(4u); return; }
    status=g32(f+16u);
    if (up.stage!=UP_RAW) {
        if (n!=4u || status) { up_fail(4u); return; }
        if (up.stage==UP_STOP) { up.pre_stop=0u; x->motor_ack=0; x->motor_fault=0u; up_start(UP_RAW); return; }
        up.mode=0u; up.layout=0u;
        if (up.hold_for==2u) up.boot_id=0u;
        if (up.hold_for==2u) up_synth_enter();
        up_done(); return;
    }
    if (!status) {
        if (cmd==1u) {
            uint8_t cls=up.board==2u ? 2u : 1u;
            uint16_t type=up.board==1u ? 1u : 2u;
            if (n!=40u || g16(d)!=1u || d[2]>1u) { up_fail(4u); return; }
            if (d[3]!=cls || g16(d+4u)!=up.board || g16(d+6u)!=up.hw || g16(d+8u)!=type) {
                up.layout=0u; up_fail(3u); return;
            }
            for (i=0u;i<3u;i++) {
                uint32_t value=g32(d+24u+4u*i);
                if (up.pinned && up.uid[i]!=value) { up.layout=0u; up_fail(3u); return; }
            }
            if (!up.pinned) for (i=0u;i<3u;i++) up.uid[i]=g32(d+24u+4u*i);
            up.pinned=1u; up.mode=d[2]; up.boot_id=(uint8_t)(up.mode==0u);
            if (up.mode) up.layout=0u;
        } else if (cmd==0x0204u) {
            if (n!=20u || g32(d)!=0x08001800u || g16(d+4u)!=18304u ||
                g16(d+6u)!=128u || g32(d+8u)!=0x08005f80u || g32(d+12u)!=6144u) {
                up.layout=0u; up_fail(4u); return;
            }
            up.layout=1u;
        } else if (cmd==0x0200u || cmd==0x0201u) {
            uint16_t size=cmd==0x0200u ? g16(up.raw+20u) : up.image_n;
            if (n!=6u || g16(d)>size || (g16(d)!=size && (g16(d)&127u))) { up_fail(4u); return; }
            if (cmd==0x0200u) up.image_n=size;
        } else if (cmd==0x0300u) {
            if (n!=6u || g16(d)!=g16(up.raw+16u)) { up_fail(4u); return; }
        } else if (cmd==0x0301u) {
            if (n!=12u || (int16_t)g16(d+4u)<-1000 || (int16_t)g16(d+4u)>1000 ||
                d[6] || d[7]>1u) { up_fail(4u); return; }
        } else if (n!=4u) { up_fail(4u); return; }
    }
    memcpy(up.reply,f,total); up.reply_n=total;
    if (!status && cmd==1u && up.mode==0u) {
        up.hold_for=1u; up_start(UP_HOLD); return;
    }
    if (!status && cmd==0x01f0u) {
        up.mode=2u; up.boot_id=up.layout=0u; up.image_n=0u; up.hold_for=2u;
        up.stage=UP_RESET; up.waiting=0u; up.deadline=board_ms+10u; return;
    }
    if (cmd==0x0203u) { up.mode=2u; up.boot_id=up.layout=0u; up.image_n=0u; }
    if (status && cmd==1u) { up.boot_id=up.layout=0u; up.mode=2u; up.image_n=0u; }
    if (status && cmd==0x0204u) up.layout=0u;
    up_done();
}

static void up_poll(void)
{
    Port *x=&p[up.port];
    const uint8_t *f;
    uint16_t n,cmd;
    uint32_t timeout;
    if (up.draining) {
        x->rt=x->rh; x->used=x->expected=0u;
        if ((int32_t)(board_ms-up.drain_at)<0) return;
        up.draining=0u; up.deadline=board_ms+250u;
    }
    if (up.stage==UP_IDLE) return;
    if (x->s.uart_error_count!=up.uart_errors) {
        up.uart_errors=x->s.uart_error_count; up_fail(2u); return;
    }
    if (up.stage==UP_RESET) {
        if ((int32_t)(board_ms-up.deadline)>=0) up_start(UP_HOLD);
        return;
    }
    f=up.stage==UP_RAW ? up.raw : up.internal;
    n=up.stage==UP_RAW ? up.raw_n : 16u; cmd=g16(f+8u);
    if (!up.waiting) {
        if (x->uart->STS_B.TXBEFLG) {
            x->uart->DATA=f[up.pos++];
            if (up.pos==n) {
                up.waiting=1u;
                timeout=cmd==0x0300u ? 100u : (cmd==0x0200u ? 10000u : (cmd==0x01f0u ? 150u : (cmd==9u ? 100u : 500u)));
                up.deadline=board_ms+timeout; ++x->s.request_count;
            }
        } else if ((int32_t)(board_ms-up.deadline)>=0) { ++x->s.uart_error_count; up.uart_errors=x->s.uart_error_count; up_fail(2u); }
        return;
    }
    if ((int32_t)(board_ms-up.deadline)<0) return;
    ++x->s.timeout_count; x->used=x->expected=0u;
    if (up.stage==UP_RAW && cmd==0x01f0u) {
        up.hold_for=2u; up.mode=2u; up.boot_id=up.layout=0u; up.image_n=0u; up_start(UP_HOLD);
    } else if (up.stage==UP_RAW && cmd==0x0300u) {
        /* A late SET retry could restart a motor already stopped by its 250 ms deadman. */
        up_fail(1u);
    } else if (up.retry<2u) {
        ++up.retry; up.pos=0u; up.waiting=0u; up.deadline=board_ms+250u;
    } else up_fail(1u);
}

uint8_t external_upgrade_active(void) { return up.active; }
uint8_t external_motor_control_active(void) { return up.active && up.motor_control; }

void external_upgrade_cancel(void)
{
    Port *x;
    uint8_t stop;
    if (!up.active) return;
    stop=(uint8_t)(up.board==2u && (up.motor_control || up.pre_stop || p[up.port].motor_fault));
    /* Discard queued control; never replay SET_PWM. Receiver's 250 ms deadman remains the fallback. */
    x=&p[up.port]; closed_token=up.token;
    x->pending=x->retry=x->bad=x->used=x->expected=0u; x->rt=x->rh;
    unbind(x); x->next_at=board_ms+50u;
    memset(&up,0,sizeof(up));
    if (stop) { motor_stop_queue(x,0u); x->motor_refresh=board_ms+50u; }
}

uint32_t external_upgrade_open(uint8_t port0,uint32_t token,uint16_t board,uint16_t hw)
{
    Port *x;
    uint8_t stop_first;
    if (port0>=KLQ_EXTERNAL_PORT_COUNT || !token ||
        !((board==1u && hw==1u) || (board==3u && hw==1u) || (board==2u && hw==2u))) return 5u;
    if (up.active) {
        if (up.token!=token || up.port!=port0 || up.board!=board || up.hw!=hw) return 4u;
        up.activity=board_ms; return 0u;
    }
    if (token==closed_token) return 4u;
    x=&p[port0];
    stop_first=(uint8_t)(x->motor_goal || x->motor_ack || x->motor_stop ||
                        (x->pending && x->command==CMD_MOTOR_SET));
    external_ports_motor_stop_all();
    memset(&up,0,sizeof(up)); up.active=1u; up.port=port0; up.token=token; up.board=board; up.hw=hw;
    up.pre_stop=stop_first;
    up.activity=board_ms; up.draining=1u; up.drain_at=board_ms+50u; up.mode=2u;
    x=&p[port0]; x->pending=x->retry=x->bad=x->used=x->expected=0u; x->rt=x->rh;
    x->motor_goal=x->motor_ack=0; x->motor_stop=x->motor_fault=x->motor_need_set=x->motor_tx_n=x->motor_tx_pos=0u;
    unbind(x); up.uart_errors=x->s.uart_error_count;
    return 0u;
}

uint32_t external_upgrade_submit(uint32_t token,const uint8_t *f,uint16_t length)
{
    uint16_t n,cmd,size,offset;
    uint32_t crc;
    if (!up.active || up.token!=token) return 4u;
    up.activity=board_ms;
    if (!f || length<16u || length>FRAME_N || g32(f)!=MAGIC) return 2u;
    n=g16(f+10u); cmd=g16(f+8u);
    if (n!=length-16u || n>132u) return 2u;
    crc=crc32_update(crc32_update(0,f,12u),f+16u,n);
    if (crc!=g32(f+12u)) return 3u;
    if (up.raw_n==length && !memcmp(up.raw,f,length)) return 0u;
    if ((up.raw_n && !up.result) || (up.raw_n && g32(up.raw+4u)==g32(f+4u))) return 4u;
    switch (cmd) {
    case 1u: case 9u: case 0x01f0u: case 0x0301u: case 0x0302u: case 0x0202u: case 0x0203u: case 0x0204u:
        if (n) return 2u;
        break;
    case 0x0300u:
        if (n!=2u) return 2u;
        if ((int16_t)g16(f+16u)<-1000 || (int16_t)g16(f+16u)>1000) return 5u;
        break;
    case 0x0200u:
        if (n!=10u) return 2u;
        size=g16(f+20u);
        if (g16(f+16u)!=up.board || g16(f+18u)!=up.hw || size<208u || size>18304u || (size&1u)) return 5u;
        break;
    case 0x0201u:
        if (n<4u || n>130u || (n&1u)) return 2u;
        size=(uint16_t)(n-2u); offset=g16(f+16u);
        if ((offset&127u) || !up.image_n || (uint32_t)offset+size>up.image_n ||
            (size!=128u && (uint32_t)offset+size!=up.image_n)) return 5u;
        break;
    default: return 5u;
    }
    if (cmd!=1u && !up.pinned) return 4u;
    if ((cmd==0x0200u || cmd==0x0201u || cmd==0x0202u) && (!up.boot_id || !up.layout || up.mode!=0u)) return 4u;
    if (cmd==0x0204u && !up.boot_id) return 4u;
    if (cmd==0x01f0u && up.mode>1u) return 4u;
    if ((cmd==9u || cmd==0x0204u || cmd==0x0203u) && up.mode!=0u) return 4u;
    if ((cmd==0x0300u || cmd==0x0301u || cmd==0x0302u) &&
        (up.board!=2u || up.hw!=2u || up.mode!=1u)) return 5u;
    if (cmd==0x0300u || cmd==0x0301u) up.motor_control=1u;
    if (cmd==0x0200u) up.image_n=0u;
    memcpy(up.raw,f,length); up.raw_n=length; up.reply_n=0u; up.result=up.reason=0u;
    up.uart_errors=p[up.port].s.uart_error_count;
    up_start((uint8_t)(up.pre_stop || (cmd==0x01f0u && up.board==2u && up.mode==1u) ? UP_STOP : UP_RAW));
    return 0u;
}

uint32_t external_upgrade_result(uint32_t token,uint8_t *out,uint16_t *length)
{
    if (!up.active || up.token!=token) return 4u;
    if (!out || !length) return 2u;
    up.activity=board_ms; out[0]=up.result;
    *length=1u;
    if (up.result==1u) { memcpy(out+1u,up.reply,up.reply_n); *length=(uint16_t)(1u+up.reply_n); }
    else if (up.result==2u) { out[1]=up.reason; *length=2u; }
    return 0u;
}

uint32_t external_upgrade_close(uint32_t token)
{
    if (!token) return 5u;
    if (!up.active) return token==closed_token ? 0u : 4u;
    if (up.token!=token) return 4u;
    external_upgrade_cancel(); return 0u;
}

static void bad_frame(Port *x)
{
    ++x->s.frame_error_count;
    if (up.active && x==&p[up.port]) {
        if (up.stage && ++x->bad>=3u) up_fail(4u);
        return;
    }
    if (x->motor_goal || x->motor_stop || x->motor_tx_n ||
        (x->pending && (x->command==CMD_MOTOR_SET || x->command==CMD_MOTOR_STOP))) {
        motor_fail(x); return;
    }
    if (++x->bad>=3u) {
        unbind(x); x->pending=x->bad=x->used=x->expected=0;
        x->next_at=board_ms+DISCOVER_MS;
    }
}

static void send(Port *x, uint8_t retry)
{
    uint8_t f[16];
    unsigned i;
    x->request_at=board_ms;
    if (x->command==CMD_SAMPLE) x->next_at=board_ms+sample_period(x);
    p32(f,MAGIC); p32(f+4,x->sequence); p16(f+8,x->command); p16(f+10,0);
    p32(f+12,crc32_update(0,f,12));
    for (i=0;i<16;i++) {
        uint32_t t=board_ms;
        while (!x->uart->STS_B.TXBEFLG && board_ms-t<2u) {}
        if (!x->uart->STS_B.TXBEFLG) {
            ++x->s.uart_error_count; unbind(x);
            x->pending=x->bad=x->used=x->expected=0;
            x->next_at=board_ms+DISCOVER_MS; return;
        }
        x->uart->DATA=(uint8_t)f[i];
    }
    x->pending=1; x->retry=retry; x->deadline=board_ms+REQ_TIMEOUT; ++x->s.request_count;
}

static void request(Port *x, uint16_t command)
{
    x->command=command; ++x->sequence; x->used=x->expected=0; send(x,0);
}

static void motor_tx_start(Port *x,uint16_t command,uint8_t retry)
{
    uint8_t *f=x->motor_tx;
    uint16_t n=command==CMD_MOTOR_SET?2u:0u;
    x->command=command; ++x->sequence; x->used=x->expected=0u;
    x->pending=1u; x->retry=retry; x->motor_tx_n=(uint8_t)(16u+n); x->motor_tx_pos=0u;
    x->motor_sent=command==CMD_MOTOR_SET?x->motor_goal:0;
    p32(f,MAGIC); p32(f+4u,x->sequence); p16(f+8u,command); p16(f+10u,n);
    p16(f+16u,(uint16_t)x->motor_sent);
    p32(f+12u,crc32_update(crc32_update(0,f,12u),f+16u,n));
    x->deadline=board_ms+REQ_TIMEOUT; x->motor_uart_errors=x->s.uart_error_count;
}

static void motor_poll(Port *x)
{
    motor_health(x);
    if ((x->motor_goal || x->motor_stop || x->motor_tx_n) &&
        x->s.uart_error_count!=x->motor_uart_errors) motor_fail(x);
    if (x->motor_tx_n) {
        if (x->uart->STS_B.TXBEFLG) {
            x->uart->DATA=x->motor_tx[x->motor_tx_pos++];
            if (x->motor_tx_pos==x->motor_tx_n) {
                x->motor_tx_n=0u; x->deadline=board_ms+REQ_TIMEOUT; ++x->s.request_count;
                if (x->command==CMD_MOTOR_SET) {
                    x->motor_lease=board_ms+MOTOR_LEASE_MS; x->motor_refresh=board_ms+MOTOR_REFRESH_MS;
                }
            }
        }
        return;
    }
    if (x->pending) return;
    if (x->motor_stop) {
        if ((int32_t)(board_ms-x->motor_refresh)>=0) motor_tx_start(x,CMD_MOTOR_STOP,0u);
    } else if (x->motor_goal && !up.active) {
        if (x->motor_need_set) { x->motor_need_set=0u; motor_tx_start(x,CMD_MOTOR_SET,0u); }
        else if ((int32_t)(board_ms-x->motor_refresh)>=0) request(x,CMD_INFO);
    }
}

static void valid(Port *x)
{
    const uint8_t *d=x->frame+20;
    uint16_t n=g16(x->frame+10);
    if (up.active && x==&p[up.port]) { up_valid(x); return; }
    if (!x->pending || g32(x->frame+4)!=x->sequence ||
        g16(x->frame+8)!=(uint16_t)(x->command|RESP)) return;
    if (n<4u || !x->pending || g32(x->frame+16) || g32(x->frame+4)!=x->sequence ||
        g16(x->frame+8)!=(uint16_t)(x->command|RESP)) goto bad;
    if (x->command==CMD_INFO) {
        if (n!=40u || g16(d)!=1u) goto bad;
        if (x->motor_goal && (d[2]!=1u || d[3]!=2u || g16(d+4u)!=2u || g16(d+6u)!=2u ||
            g16(d+8u)!=2u || !(g32(d+16u)&1u) || g32(d+24u)!=x->s.uid[0] ||
            g32(d+28u)!=x->s.uid[1] || g32(d+32u)!=x->s.uid[2])) { motor_fail(x); return; }
        clear_sample(x);
        x->s.mode=d[2]; x->s.device_class=d[3];
        x->s.board_id=g16(d+4); x->s.hw_compat=g16(d+6); x->s.sensor_type=g16(d+8);
        x->s.fw_version=g32(d+12); x->s.capabilities=g32(d+16);
        x->s.uid[0]=g32(d+24); x->s.uid[1]=g32(d+28); x->s.uid[2]=g32(d+32);
        x->s.online=1;
    } else if (x->command==CMD_SAMPLE) {
        uint8_t fresh;
        if (!sample_supported(x) || n!=(x->s.sensor_type==KLQ_SENSOR_CS100A?20u:32u)) goto bad;
        fresh=(uint8_t)(!x->s.sample_valid ||
            (g32(d)!=x->s.sample_sequence && g32(d+4)!=x->s.capture_time_ms));
        if (x->s.sensor_type==KLQ_SENSOR_CS100A) {
            if (n!=20u) goto bad;
            if (fresh) {
                x->s.echo_ticks=g16(d+8); x->s.distance_mm=g16(d+10); x->s.sample_status=d[12];
                x->s.quality_flags=d[13]; x->s.driver_state=d[14];
            }
        } else {
            int16_t left, right;
            if (n!=32u) goto bad;
            left=(int16_t)g16(d+8); right=(int16_t)g16(d+10);
            if (left < -4095 || left > 4095 || right < -4095 || right > 4095 ||
                g16(d+12)>1000u || g16(d+14)>1000u || g16(d+16)>4095u ||
                g16(d+18)>4095u || g16(d+20)>4095u || g16(d+22)>4095u ||
                d[24]>3u || (d[25]&0xc0u) || d[26]>3u || d[27]) goto bad;
            if (fresh) {
            x->s.ir_left_delta=left; x->s.ir_right_delta=right;
            x->s.ir_left=g16(d+12); x->s.ir_right=g16(d+14);
            x->s.ir_left_ambient=g16(d+16); x->s.ir_left_active=g16(d+18);
            x->s.ir_right_ambient=g16(d+20); x->s.ir_right_active=g16(d+22);
            x->s.sample_status=d[24]; x->s.quality_flags=d[25]; x->s.driver_state=d[26];
            }
        }
        if (fresh) {
            x->s.sample_sequence=g32(d); x->s.capture_time_ms=g32(d+4);
            x->s.sample_host_ms=board_ms; x->s.sample_valid=1u;
        }
    } else if (x->command==CMD_MOTOR_SET) {
        if (n!=6u || (int16_t)g16(d)!=x->motor_sent) goto bad;
        x->motor_ack=x->motor_sent;
        if (x->motor_goal!=x->motor_sent) x->motor_refresh=board_ms;
    } else if (x->command==CMD_MOTOR_STOP) {
        if (n!=4u) goto bad;
        x->motor_ack=0; x->motor_stop=0u;
    } else goto bad;
    if (x->command==CMD_INFO && x->motor_goal) x->motor_need_set=1u;
    x->pending=x->bad=0; ++x->s.response_count;
    if (!sample_supported(x)) x->next_at=board_ms+DISCOVER_MS;
    else if (x->command==CMD_INFO) x->next_at=board_ms;
    else if ((int32_t)(board_ms-x->next_at)>0) x->next_at=board_ms+sample_period(x);
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
    memset(&up,0,sizeof(up)); closed_token=0u;
    for (i=0;i<KLQ_EXTERNAL_PORT_COUNT;i++) {
        p[i].uart=uart[i]; p[i].sequence=i<<24; p[i].next_at=board_ms+50u*i;
        USART_EnableInterrupt(uart[i],USART_INT_RXBNE);
        USART_EnableInterrupt(uart[i],USART_INT_ERR);
        USART_EnableInterrupt(uart[i],USART_INT_PE);
        NVIC_SetPriority((IRQn_Type)(USART1_IRQn+i),1);
        NVIC_EnableIRQ((IRQn_Type)(USART1_IRQn+i));
    }
}

void external_ports_set_active(bool value)
{
    unsigned i;
    if (value && !active) for (i=0;i<KLQ_EXTERNAL_PORT_COUNT;i++) if (sample_supported(&p[i])) {
        Port *x=&p[i];
        x->next_at=x->pending ? x->request_at+(x->s.sensor_type==KLQ_SENSOR_IR_DUAL?IR_SAMPLE_MS:SAMPLE_ACTIVE_MS) : board_ms;
    }
    active=value;
}

static void expire(Port *x)
{
    if (!x->pending || (int32_t)(board_ms-x->deadline)<0) return;
    ++x->s.timeout_count; x->used=x->expected=0; x->rt=x->rh;
    if (x->command==CMD_MOTOR_SET || (x->motor_goal && x->command==CMD_INFO)) motor_fail(x);
    else if (x->command==CMD_MOTOR_STOP) {
        if (x->retry<RETRIES) motor_tx_start(x,CMD_MOTOR_STOP,(uint8_t)(x->retry+1u));
        else { x->pending=x->motor_tx_n=x->motor_stop=0u; x->motor_fault=1u; unbind(x); x->next_at=board_ms+DISCOVER_MS; }
    } else if (x->retry<RETRIES) { ++x->sequence; send(x,(uint8_t)(x->retry+1u)); }
    else { x->pending=0; unbind(x); x->next_at=board_ms+DISCOVER_MS; }
}

void external_ports_poll(void)
{
    unsigned i;
    if (up.active && board_ms-up.activity>=30000u) external_upgrade_cancel();
    for (i=0;i<KLQ_EXTERNAL_PORT_COUNT;i++) {
        Port *x=&p[i];
        uint8_t budget=64u;
        if (!(up.active && i==up.port)) { motor_health(x); expire(x); }
        if (up.active && i==up.port && up.draining) { up_poll(); continue; }
        if (up.active && i==up.port && up.waiting && (int32_t)(board_ms-up.deadline)>=0) {
            x->rt=x->rh; x->used=x->expected=0u; up_poll(); continue;
        }
        while (x->rt!=x->rh && budget--) { uint8_t t=x->rt; x->rt=(uint8_t)((t+1u)&(RX_N-1u)); byte_in(x,x->rx[t]); }
        if (up.active && i==up.port) {
            if (x->used && board_ms-x->last_byte>20u) { x->used=x->expected=0u; ++x->s.frame_error_count; }
            up_poll(); continue;
        }
        if (x->used && board_ms-x->last_byte>20u) { x->used=x->expected=0; bad_frame(x); }
        motor_poll(x);
        if (!x->pending && !x->motor_goal && !x->motor_stop && (int32_t)(board_ms-x->next_at)>=0) {
            request(x,sample_supported(x) ? CMD_SAMPLE : CMD_INFO);
        }
    }
}

const klq_port_status_t *external_port_status(unsigned port)
{
    return port<KLQ_EXTERNAL_PORT_COUNT ? &p[port].s : 0;
}

bool external_ports_motor_stopped(void)
{
    unsigned i;
    if (up.active && (up.motor_control || up.pre_stop)) return false;
    for (i=0;i<KLQ_EXTERNAL_PORT_COUNT;i++) {
        const Port *x=&p[i];
        if (x->motor_fault || x->motor_goal || x->motor_stop || x->motor_tx_n || x->motor_ack ||
            (x->pending && (x->command==CMD_MOTOR_SET || x->command==CMD_MOTOR_STOP))) return false;
    }
    return true;
}

bool external_port_motor_ready(unsigned port)
{
    return port<KLQ_EXTERNAL_PORT_COUNT && !up.active && motor_health(&p[port]) && motor_identity(&p[port]);
}

bool external_port_motor_command(unsigned port,int16_t pwm)
{
    Port *x;
    if (port>=KLQ_EXTERNAL_PORT_COUNT || (up.active && port==up.port)) return false;
    x=&p[port]; motor_health(x);
    if (pwm<-1000 || pwm>1000) {
        if (motor_identity(x) || x->motor_goal || x->motor_ack) motor_stop_queue(x,0u);
        return false;
    }
    if (pwm && (up.active || x->motor_fault || !motor_identity(x))) return false;
    if (!pwm) {
        if (!motor_identity(x) && !x->motor_goal && !x->motor_ack && !x->motor_stop) return false;
        motor_stop_queue(x,0u); return true;
    }
    if (x->motor_goal==pwm && !x->motor_fault) return true;
    x->motor_goal=pwm; x->motor_fault=x->motor_need_set=0u;
    x->motor_refresh=board_ms; x->motor_lease=board_ms+MOTOR_LEASE_MS;
    x->motor_uart_errors=x->s.uart_error_count;
    return true;
}

uint8_t external_port_motor_state(unsigned port)
{
    Port *x;
    if (port>=KLQ_EXTERNAL_PORT_COUNT) return KLQ_MOTOR_ERROR;
    x=&p[port]; motor_health(x);
    if (x->motor_fault) return KLQ_MOTOR_ERROR;
    if (x->motor_stop) return KLQ_MOTOR_STOPPING;
    if (x->motor_goal) return x->motor_ack==x->motor_goal?KLQ_MOTOR_ACTIVE:KLQ_MOTOR_PENDING;
    return KLQ_MOTOR_IDLE;
}

int16_t external_port_motor_ack_pwm(unsigned port)
{
    return port<KLQ_EXTERNAL_PORT_COUNT?p[port].motor_ack:0;
}

int16_t external_port_motor_goal_pwm(unsigned port)
{
    return port<KLQ_EXTERNAL_PORT_COUNT?p[port].motor_goal:0;
}

bool external_port_motor(unsigned port,int direction,bool run,unsigned power)
{
    static const uint16_t duty[3]={333u,666u,1000u};
    if (!run) return external_port_motor_command(port,0);
    if ((direction!=-1 && direction!=1) || power<1u || power>3u) {
        external_port_motor_command(port,0); return false;
    }
    return external_port_motor_command(port,(int16_t)(direction*(int)duty[power-1u]));
}

void external_ports_motor_stop_all(void)
{
    unsigned i;
    for (i=0;i<KLQ_EXTERNAL_PORT_COUNT;i++) {
        Port *x=&p[i];
        if (!(up.active && i==up.port) && (motor_identity(x) || x->motor_goal || x->motor_ack || x->motor_stop))
            motor_stop_queue(x,0u);
    }
}
