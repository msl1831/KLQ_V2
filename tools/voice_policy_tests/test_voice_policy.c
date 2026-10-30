#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "voice_control.h"
#include "voice_link.h"
#include "external_ports.h"
#include "user_program.h"
#include "robot_ui.h"

volatile uint32_t board_ms;
static unsigned checks,commands,sets,stop_commands,stop_all,drop_calls,played;
static uint16_t last_sound;
static uint8_t have,play_ok,up,shutdown,resetting,error,valid_flag,fail_command,events;
static uint8_t state[3];
static int16_t ack[3],goal[3];
static klq_port_status_t ports[3];
static voice_link_status_t link;
static voice_link_intent_t incoming;
static user_program_state_t user;
static robot_ui_state_t ui;
#define CHECK(x) do {++checks;if(!(x)){printf("voice policy line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
user_program_state_t user_program_state(void){return user;}
bool user_program_valid(void){return valid_flag!=0;}
uint8_t user_program_error(void){return error;}
robot_ui_state_t robot_ui_state(void){return ui;}
uint8_t external_upgrade_active(void){return up;}
uint8_t power_key_shutdown_pending(void){return shutdown;}
uint8_t protocol_reset_pending(void){return resetting;}
const klq_port_status_t *external_port_status(unsigned p){return p<3?&ports[p]:0;}
bool external_port_motor_ready(unsigned p){return p<3&&ports[p].online&&!up;}
uint8_t external_port_motor_state(unsigned p){return p<3?state[p]:KLQ_MOTOR_ERROR;}
int16_t external_port_motor_ack_pwm(unsigned p){return p<3?ack[p]:0;}
bool external_port_motor_command(unsigned p,int16_t pwm)
{
    CHECK(p<3);++commands;
    if(fail_command)return false;
    goal[p]=pwm;
    if(pwm){++sets;state[p]=KLQ_MOTOR_PENDING;}else{++stop_commands;state[p]=KLQ_MOTOR_STOPPING;}
    return true;
}
void external_ports_motor_stop_all(void){unsigned i;++stop_all;for(i=0;i<3;i++){goal[i]=0;if(ports[i].online)state[i]=KLQ_MOTOR_STOPPING;}}
void voice_link_get_status(voice_link_status_t *p){*p=link;}
uint8_t voice_link_take_events(void){uint8_t e=events;events=0;return e;}
uint8_t voice_link_take_intent(voice_link_intent_t *p){if(!have)return 0;*p=incoming;have=0;return 1;}
void voice_link_drop_intents(void){have=0;++drop_calls;}
uint8_t voice_link_play(uint32_t id){if(!play_ok)return 0;last_sound=(uint16_t)id;++played;return 1;}
static voice_control_status_t status(void){voice_control_status_t s;voice_control_get_status(&s);return s;}
static void motor(unsigned p){ports[p].online=ports[p].mode=1;ports[p].device_class=2;ports[p].board_id=ports[p].hw_compat=ports[p].sensor_type=2;ports[p].capabilities=1;ports[p].fw_version=0x20000;ports[p].uid[0]=0x1234+p;ports[p].uid[1]=0x5678;ports[p].uid[2]=0x9abc;}
static void setup(void)
{
    memset(ports,0,sizeof(ports));memset(state,0,sizeof(state));memset(ack,0,sizeof(ack));memset(goal,0,sizeof(goal));memset(&link,0,sizeof(link));
    commands=sets=stop_commands=stop_all=drop_calls=played=0;last_sound=0;have=up=shutdown=resetting=error=fail_command=events=0;
    play_ok=valid_flag=1;user=USER_PROGRAM_EMPTY;ui=ROBOT_UI_STANDBY;board_ms=100;
    link.awake=link.version_valid=1;link.version[1]=2;motor(2);voice_control_init();
}
static void push(uint32_t semantic,uint8_t seq){incoming.semantic=semantic;incoming.seq=seq;incoming.score=90;incoming.received_ms=board_ms;have=1;}
static void send(uint32_t semantic,uint8_t seq){push(semantic,seq);voice_control_poll();}
static void enable(void){voice_control_set_enabled(1);CHECK(status().enabled);}
static void accepted(unsigned p,int16_t pwm){state[p]=KLQ_MOTOR_ACTIVE;ack[p]=pwm;voice_control_poll();CHECK(status().phase==VOICE_CTL_RUNNING&&status().accepted==1);}
static void initial_gates(void)
{
    unsigned u,k;user_program_state_t states[2]={USER_PROGRAM_EMPTY,USER_PROGRAM_READY};
    setup();send(0x4b510101UL,1);CHECK(!sets&&!status().active&&status().last_reason==VOICE_CTL_DISABLED&&last_sound==5007);
    for(u=0;u<2;u++){setup();user=states[u];enable();send(0x4b510101UL,1);CHECK(sets==1&&!status().accepted&&status().phase==VOICE_CTL_WAIT_START);}
    for(u=0;u<4;u++)for(k=0;k<7;k++){
        setup();user=(user_program_state_t)u;ui=(robot_ui_state_t)k;enable();voice_control_poll();send(0x4b510101UL,1);
        CHECK(sets==((u==USER_PROGRAM_EMPTY||u==USER_PROGRAM_READY)&&k==ROBOT_UI_STANDBY?1U:0U));
        CHECK(user_program_valid());
    }
    for(k=0;k<4;k++){
        setup();if(k==0)up=1;if(k==1)shutdown=1;if(k==2)resetting=1;if(k==3)error=2;enable();voice_control_poll();send(0x4b510101UL,1);
        CHECK(!sets&&status().last_reason==VOICE_CTL_BUSY);
    }
    setup();link.awake=0;enable();send(0x4b510101UL,1);CHECK(!sets&&status().last_reason==VOICE_CTL_ASLEEP);
    setup();link.version_valid=0;enable();send(0x4b510101UL,1);CHECK(!sets&&status().last_reason==VOICE_CTL_VERSION);
    setup();link.version[1]=1;enable();send(0x4b510101UL,1);CHECK(!sets&&status().last_reason==VOICE_CTL_VERSION);
    setup();enable();send(0x4b510001UL,1);CHECK(!commands&&!status().rejected);
    puts("PASS default-disabled, exact version, awake and all host/UI gates");
}
static void unique_identity(void)
{
    unsigned i;
    setup();ports[2].online=0;enable();send(0x4b510101UL,1);CHECK(!commands&&status().last_reason==VOICE_CTL_NO_MOTOR&&last_sound==5007);
    setup();motor(0);enable();send(0x4b510101UL,1);CHECK(!commands&&status().last_reason==VOICE_CTL_MULTIPLE);
    for(i=0;i<7;i++){
        setup();if(i==0)ports[2].mode=0;if(i==1)ports[2].device_class=1;if(i==2)ports[2].board_id=1;if(i==3)ports[2].hw_compat=1;if(i==4)ports[2].sensor_type=1;if(i==5)ports[2].capabilities=0;if(i==6)ports[2].fw_version=0x10000;
        enable();send(0x4b510101UL,1);CHECK(!commands&&status().last_reason==VOICE_CTL_NO_MOTOR);
    }
    setup();ports[2].online=0;motor(0);enable();send(0x4b510102UL,1);CHECK(sets==1&&goal[0]==-800&&status().port==1);
    setup();state[2]=KLQ_MOTOR_ACTIVE;enable();send(0x4b510101UL,1);CHECK(!sets&&status().last_reason==VOICE_CTL_BUSY);
    puts("PASS unique supported motor on dynamic port and no commandeering");
}
static void finite_ack_and_stop(void)
{
    setup();enable();send(0x4b510101UL,1);CHECK(sets==1&&goal[2]==800&&!status().accepted&&last_sound!=5002);
    board_ms+=20;accepted(2,800);CHECK(last_sound==5002);
    board_ms=1099;voice_control_poll();CHECK(!stop_commands);
    board_ms=1100;voice_control_poll();CHECK(stop_commands==1&&status().phase==VOICE_CTL_WAIT_STOP&&!status().completed);
    state[2]=KLQ_MOTOR_IDLE;ack[2]=0;voice_control_poll();CHECK(!status().active&&status().completed==1&&status().accepted==1);
    setup();enable();send(0x4b510101UL,1);send(0x4b510103UL,2);CHECK(sets==1&&stop_commands==1&&!status().accepted);
    state[2]=KLQ_MOTOR_IDLE;ack[2]=0;voice_control_poll();CHECK(!status().active&&status().accepted==1&&last_sound==5002);
    setup();enable();send(0x4b510103UL,1);CHECK(stop_commands==1&&!status().accepted);state[2]=KLQ_MOTOR_IDLE;voice_control_poll();CHECK(status().accepted==1);
    puts("PASS actual ACK before accept sound, strict finite deadline and STOP ACK");
}
static void no_replay_and_cancellation(void)
{
    unsigned before;
    setup();enable();send(0x4b510101UL,1);board_ms=249;voice_control_poll();CHECK(!status().errors);board_ms=250;voice_control_poll();CHECK(status().errors==1&&!status().active&&stop_all==1&&last_sound==5003);
    send(0x4b510101UL,1);CHECK(sets==1&&status().last_reason==VOICE_CTL_DUPLICATE);board_ms+=500;voice_control_poll();CHECK(sets==1);
    setup();enable();user=USER_PROGRAM_RUNNING;voice_control_poll();send(0x4b510101UL,5);CHECK(!sets&&last_sound==5007);
    push(0x4b510101UL,6);before=drop_calls;user=USER_PROGRAM_READY;voice_control_poll();CHECK(!sets&&drop_calls==before+1&&!have);
    send(0x4b510101UL,5);CHECK(!sets&&status().last_reason==VOICE_CTL_DUPLICATE);send(0x4b510101UL,7);CHECK(sets==1);
    accepted(2,800);before=stop_all;voice_control_cancel();CHECK(stop_all==before+1&&!status().active);
    voice_control_sound(5004);user=USER_PROGRAM_RUNNING;voice_control_poll();CHECK(last_sound==5004);
    before=stop_all;state[2]=KLQ_MOTOR_ACTIVE;link.awake=0;voice_control_poll();CHECK(stop_all==before);
    send(0x4b510103UL,8);CHECK(stop_all==before&&last_sound==5007&&user==USER_PROGRAM_RUNNING);
    setup();enable();send(0x4b510101UL,1);accepted(2,800);link.awake=0;voice_control_poll();CHECK(stop_all==1&&!status().active);
    setup();enable();send(0x4b510101UL,1);voice_control_set_enabled(0);CHECK(stop_all==1&&!status().enabled&&!status().active);send(0x4b510101UL,2);CHECK(sets==1);
    setup();enable();push(0x4b510101UL,9);voice_control_cancel();user=USER_PROGRAM_RUNNING;user=USER_PROGRAM_READY;voice_control_poll();CHECK(!sets&&!have);
    setup();enable();play_ok=0;send(0x4b510101UL,1);accepted(2,800);CHECK(status().pending_sound==5002);voice_control_cancel();voice_control_sound(5004);user=USER_PROGRAM_RUNNING;play_ok=1;voice_control_poll();CHECK(last_sound==5004&&!status().pending_sound);
    setup();enable();send(0x4b510101UL,1);accepted(2,800);events=6;link.awake=1;push(0x4b510102UL,2);voice_control_poll();CHECK(stop_all==1&&!status().active&&!have&&sets==1);
    puts("PASS no stale retry/resume, busy-edge flush, owned-only cancellation and wake exit");
}
static void errors_and_sounds(void)
{
    setup();enable();send(0x4b510101UL,1);ports[2].uid[0]++;voice_control_poll();CHECK(status().errors==1&&stop_all==1&&last_sound==5003);
    setup();enable();send(0x4b510101UL,1);motor(0);voice_control_poll();CHECK(status().errors==1&&stop_all==1);
    setup();enable();send(0x4b510101UL,1);state[2]=KLQ_MOTOR_ERROR;voice_control_poll();CHECK(status().errors==1&&!status().active);
    setup();enable();send(0x4b510103UL,1);board_ms+=250;voice_control_poll();CHECK(status().errors==1&&!status().accepted);
    setup();enable();fail_command=1;send(0x4b510101UL,1);CHECK(!status().active&&!sets&&last_sound==5003);
    setup();enable();push(0x4b510101UL,1);incoming.received_ms=board_ms-101;voice_control_poll();CHECK(!sets&&status().last_reason==VOICE_CTL_STALE);
    setup();enable();send(0xdeadbeefUL,1);CHECK(!sets&&status().last_reason==VOICE_CTL_UNSUPPORTED&&last_sound==5003);
    setup();user=USER_PROGRAM_RUNNING;play_ok=0;voice_control_sound(5004);voice_control_poll();CHECK(status().pending_sound==5004&&!played);play_ok=1;voice_control_poll();CHECK(!status().pending_sound&&last_sound==5004&&!sets);
    setup();board_ms=0xfffffff0UL;enable();send(0x4b510101UL,1);accepted(2,800);board_ms=0x3d7UL;voice_control_poll();CHECK(!stop_commands);++board_ms;voice_control_poll();CHECK(stop_commands==1);
    puts("PASS identity/driver errors, stale input, serialized feedback while running and clock wrap");
}
int main(void){initial_gates();unique_identity();finite_ack_and_stop();no_replay_and_cancellation();errors_and_sounds();printf("Voice policy: %u assertions passed\n",checks);return 0;}
