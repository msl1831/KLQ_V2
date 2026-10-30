#include "voice_control.h"
#include "voice_link.h"
#include "external_ports.h"
#include "user_program.h"
#include "robot_ui.h"
#include "power_key.h"
#include "protocol.h"
#include <string.h>

extern volatile uint32_t board_ms;
static voice_control_status_t s;
static uint32_t uid[3], end_at, ack_at;
static uint8_t host_open, finish, stop_intent;

static uint8_t idle_host(void)
{
    user_program_state_t u=user_program_state();
    return (u==USER_PROGRAM_EMPTY || u==USER_PROGRAM_READY) &&
        robot_ui_state()==ROBOT_UI_STANDBY && !user_program_error() &&
        !external_upgrade_active() && !power_key_shutdown_pending() && !protocol_reset_pending();
}
static uint8_t compatible(const klq_port_status_t *p)
{
    return p && p->online && p->mode==1u && p->device_class==2u &&
        p->board_id==2u && p->hw_compat==2u && p->sensor_type==2u &&
        (p->capabilities&1u) && (p->fw_version>>16)==2u;
}
static uint8_t unique(unsigned *port)
{
    unsigned i; uint8_t n=0u;
    for(i=0u;i<KLQ_EXTERNAL_PORT_COUNT;i++) if(compatible(external_port_status(i))) {
        *port=i; ++n;
    }
    return n;
}
static uint8_t identity(void)
{
    unsigned port=0u;
    const klq_port_status_t *p=external_port_status((unsigned)(s.port-1u));
    return unique(&port)==1u && port==(unsigned)(s.port-1u) && compatible(p) &&
        p->uid[0]==uid[0] && p->uid[1]==uid[1] && p->uid[2]==uid[2];
}
static void phase(uint8_t p) { s.phase=p; s.active=(uint8_t)(p!=VOICE_CTL_IDLE); }
void voice_control_sound(uint16_t id)
{
    if(id>=5000u && id<=5007u) s.pending_sound=id;
}
void voice_control_cancel(void)
{
    /* Cancelling wake/listening must never stop an unrelated user-program motor. */
    if(s.active) {
        external_ports_motor_stop_all(); ++s.cancelled; s.last_reason=VOICE_CTL_CANCELLED;
    }
    phase(VOICE_CTL_IDLE); s.pwm=0; s.pending_sound=0u; finish=stop_intent=0u;
    voice_link_drop_intents();
}
void voice_control_set_enabled(uint8_t enabled)
{
    enabled=(uint8_t)(enabled!=0u);
    if(s.enabled==enabled) return;
    voice_control_cancel(); s.enabled=enabled;
    if(!enabled) s.last_reason=VOICE_CTL_DISABLED;
}
void voice_control_init(void)
{
    memset(&s,0,sizeof(s)); memset(uid,0,sizeof(uid));
    end_at=ack_at=0u; finish=stop_intent=0u;
    host_open=idle_host(); voice_link_drop_intents();
}
uint8_t voice_control_active(void) { return s.active; }
void voice_control_get_status(voice_control_status_t *out) { if(out) *out=s; }
static void reject(uint8_t reason,uint16_t sound)
{
    ++s.rejected; s.last_reason=reason; if(sound) voice_control_sound(sound);
}
static void failure(uint8_t reason)
{
    voice_control_cancel(); ++s.errors; s.last_reason=reason; voice_control_sound(5003u);
}
static uint8_t version(const voice_link_status_t *v)
{
    return v->version_valid && v->version[0]==0u && v->version[1]==2u && v->version[2]==0u;
}
static void intent(const voice_link_intent_t *i,const voice_link_status_t *v,uint8_t open)
{
    unsigned port=0u; uint8_t n,state; const klq_port_status_t *p;
    s.last_semantic=i->semantic; s.last_seq=i->seq; s.last_score=i->score;
    if((uint32_t)(board_ms-i->received_ms)>100u) { reject(VOICE_CTL_STALE,0u); return; }
    if(i->semantic==0x4b510001u) { s.last_reason=VOICE_CTL_OK; return; }
    if(i->semantic<0x4b510101u || i->semantic>0x4b510103u) { reject(VOICE_CTL_UNSUPPORTED,5003u); return; }
    if(!s.enabled) { reject(VOICE_CTL_DISABLED,5007u); return; }
    if(!open) { reject(VOICE_CTL_BUSY,5007u); return; }
    if(!version(v)) { reject(VOICE_CTL_VERSION,5007u); return; }
    if(!v->awake) { reject(VOICE_CTL_ASLEEP,5007u); return; }
    if(s.active && i->semantic!=0x4b510103u) { reject(VOICE_CTL_BUSY,5007u); return; }
    n=unique(&port);
    if(n!=1u) { reject((uint8_t)(n ? VOICE_CTL_MULTIPLE : VOICE_CTL_NO_MOTOR),5007u); return; }
    state=external_port_motor_state(port);
    if(!external_port_motor_ready(port) || state==KLQ_MOTOR_ERROR) { reject(VOICE_CTL_DRIVER,5003u); return; }
    if(s.active && !identity()) { failure(VOICE_CTL_IDENTITY); return; }
    if(i->semantic!=0x4b510103u && state!=KLQ_MOTOR_IDLE) { reject(VOICE_CTL_BUSY,5007u); return; }
    p=external_port_status(port); memcpy(uid,p->uid,sizeof(uid)); s.port=(uint8_t)(port+1u);
    if(i->semantic==0x4b510103u) {
        if(!external_port_motor_command(port,0)) { reject(VOICE_CTL_DRIVER,5003u); return; }
        s.pwm=0; stop_intent=1u; finish=0u; ack_at=board_ms+250u; phase(VOICE_CTL_WAIT_STOP);
    } else {
        s.pwm=(int16_t)(i->semantic==0x4b510101u ? 800 : -800);
        if(!external_port_motor_command(port,s.pwm)) { s.pwm=0; reject(VOICE_CTL_DRIVER,5003u); return; }
        finish=stop_intent=0u; end_at=board_ms+1000u; ack_at=board_ms+150u;
        phase(VOICE_CTL_WAIT_START);
    }
    s.last_reason=VOICE_CTL_OK;
}
static void advance(void)
{
    unsigned port=(unsigned)(s.port-1u); uint8_t state;
    if(!identity()) { failure(VOICE_CTL_IDENTITY); return; }
    state=external_port_motor_state(port);
    if(state==KLQ_MOTOR_ERROR) { failure(VOICE_CTL_DRIVER); return; }
    if(s.phase==VOICE_CTL_WAIT_START) {
        if((int32_t)(board_ms-end_at)>=0) { failure(VOICE_CTL_TIMEOUT); return; }
        if(state==KLQ_MOTOR_ACTIVE && external_port_motor_ack_pwm(port)==s.pwm) {
            phase(VOICE_CTL_RUNNING); ++s.accepted; voice_control_sound(5002u);
        } else if((int32_t)(board_ms-ack_at)>=0) failure(VOICE_CTL_TIMEOUT);
    } else if(s.phase==VOICE_CTL_RUNNING) {
        if((int32_t)(board_ms-end_at)>=0) {
            if(!external_port_motor_command(port,0)) { failure(VOICE_CTL_DRIVER); return; }
            finish=1u; stop_intent=0u; ack_at=board_ms+250u; phase(VOICE_CTL_WAIT_STOP);
        } else if(state==KLQ_MOTOR_IDLE || state==KLQ_MOTOR_STOPPING) failure(VOICE_CTL_DRIVER);
    } else if(s.phase==VOICE_CTL_WAIT_STOP) {
        if(state==KLQ_MOTOR_IDLE && external_port_motor_ack_pwm(port)==0) {
            if(stop_intent) { ++s.accepted; voice_control_sound(5002u); }
            if(finish) ++s.completed;
            finish=stop_intent=0u; s.pwm=0; phase(VOICE_CTL_IDLE);
        } else if((int32_t)(board_ms-ack_at)>=0) failure(VOICE_CTL_TIMEOUT);
    }
}
void voice_control_poll(void)
{
    voice_link_status_t v; voice_link_intent_t i; uint8_t open,events;
    voice_link_get_status(&v); open=idle_host(); events=voice_link_take_events();
    /* B2 followed by B1 in one RX batch still ends the previous motion session. */
    if(events&5u) {
        if(s.active) voice_control_cancel(); else voice_link_drop_intents();
    }
    /* Gate edges invalidate raw RX epochs as well as an already-decoded intent. */
    if(open!=host_open) { voice_link_drop_intents(); host_open=open; }
    if(s.active && (!open || !s.enabled || !version(&v) || !v.awake)) voice_control_cancel();
    if(voice_link_take_intent(&i)) intent(&i,&v,open);
    if(s.active) advance();
    if(s.pending_sound && voice_link_play(s.pending_sound)) s.pending_sound=0u;
}
