#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "protocol.h"
#include "usb_serial.h"
#include "user_program.h"
#include "robot_ui.h"
#include "external_ports.h"


#include "voice_link.h"
#include "voice_control.h"
static unsigned voice_arm_on_calls;
static uint8_t shutdown_test;
void voice_control_init(void){}
void voice_control_poll(void){}
uint8_t voice_control_active(void){return 0u;}
void voice_control_cancel(void){}
void voice_control_set_enabled(uint8_t v){if(v)++voice_arm_on_calls;}
void voice_control_sound(uint16_t id){(void)id;}
void voice_control_get_status(voice_control_status_t *s){memset(s,0,sizeof(*s));}
uint8_t voice_link_query_version(void){return 1u;}
uint8_t voice_link_play(uint32_t id){(void)id;return 1u;}
void voice_link_get_status(voice_link_status_t *s){memset(s,0,sizeof(*s));}
uint8_t power_key_shutdown_pending(void){return shutdown_test;}

#define XOPEN 0x0040U
#define XFER 0x0041U
#define XPOLL 0x0042U
#define XCLOSE 0x0043U
#define TOKEN 0x12345678UL
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("USB bridge failed line %u: %s\n",__LINE__,#x); exit(1); } } while (0)

volatile uint32_t board_ms,usb_reset_count;
uint8_t mock_app[32],mock_meta[32];
static uint8_t input[8192],sent[8192],reset_seen,owner,result_state,result_reason;
static uint8_t reset_queue[200],inject_reset;
static uint16_t reset_queue_size;
static uint32_t in_size,in_pos,sent_size,owner_token;
static unsigned writes,open_calls,submit_calls,result_calls,close_calls,cancel_calls,stop_calls;
static unsigned begin_calls,data_calls,end_calls,run_calls,display_calls,finish_calls,reset_calls,boot_requests;
static uint8_t selected,motor_owner;
static uint16_t expected_board,expected_hw,submitted_size;
static uint8_t submitted[148];
static user_program_state_t user_state;
static klq_port_status_t cached_ports[3];
static uint8_t cached_motor_state[3];
static int16_t cached_motor_goal[3],cached_motor_ack[3];
static unsigned snapshot_reads;
const klq_port_status_t *external_port_status(unsigned i){++snapshot_reads;return i<3u?cached_ports+i:0;}
uint8_t external_port_motor_state(unsigned i){++snapshot_reads;return i<3u?cached_motor_state[i]:KLQ_MOTOR_ERROR;}
int16_t external_port_motor_goal_pwm(unsigned i){++snapshot_reads;return i<3u?cached_motor_goal[i]:0;}
int16_t external_port_motor_ack_pwm(unsigned i){++snapshot_reads;return i<3u?cached_motor_ack[i]:0;}


int usb_serial_read(void) { return in_pos<in_size ? input[in_pos++] : -1; }
bool usb_serial_write(const uint8_t *p,uint32_t n)
{
    CHECK(n<=sizeof(sent)-sent_size);memcpy(sent+sent_size,p,n);sent_size+=n;++writes;
    if(inject_reset) {
        inject_reset=0U;reset_seen=1U;
        memcpy(input,reset_queue,reset_queue_size);in_pos=0U;in_size=reset_queue_size;
    }
    return true;
}
bool usb_serial_reset_seen(void) { uint8_t r=reset_seen; reset_seen=0; return r; }
void delay_ms(uint32_t ms) { board_ms+=ms; }
void display_icon(unsigned icon) { (void)icon; }
void display_columns(const uint8_t *p) { (void)p; }
void usb_disconnect(void) {}
void board_request_bootloader(void) { ++boot_requests; }
void board_jump(uint32_t address) { (void)address; }
void NVIC_SystemReset(void) { ++reset_calls; }

uint32_t external_upgrade_open(uint8_t port,uint32_t token,uint16_t board,uint16_t hw)
{
    ++open_calls;
    if(port>=3U || !token || !((board==1U && hw==1U) || (board==3U && hw==1U) || (board==2U && hw==2U))) return ERR_RANGE;
    if(owner && (owner_token!=token || selected!=port || expected_board!=board || expected_hw!=hw)) return ERR_STATE;
    owner=1U;owner_token=token;selected=port;expected_board=board;expected_hw=hw;
    return OK;
}
uint32_t external_upgrade_submit(uint32_t token,const uint8_t *p,uint16_t n)
{
    ++submit_calls;
    if(!owner || token!=owner_token) return ERR_STATE;
    if(n<16U || n>148U) return ERR_LENGTH;
    memcpy(submitted,p,n);submitted_size=n;return OK;
}
uint32_t external_upgrade_result(uint32_t token,uint8_t *out,uint16_t *n)
{
    ++result_calls;
    if(!owner || token!=owner_token) return ERR_STATE;
    out[0]=result_state;*n=1U;
    if(result_state==1U) { out[1]=0x4BU;out[2]=0x4CU;*n=3U; }
    else if(result_state==2U) { out[1]=result_reason;*n=2U; }
    return OK;
}
uint32_t external_upgrade_close(uint32_t token)
{
    ++close_calls;
    if(!owner || token!=owner_token) return ERR_STATE;
    owner=0U;return OK;
}
void external_upgrade_cancel(void) { ++cancel_calls;owner=0U; }
uint8_t external_upgrade_active(void) { return owner; }
static bool motors_stopped=true;
bool external_ports_motor_stopped(void){return motors_stopped;}
static unsigned motor_stop_calls;
void klq_runtime_stop_all(void){++motor_stop_calls;}
uint8_t external_motor_control_active(void) { return (uint8_t)(owner && motor_owner); }

void user_program_init(void) {}
bool user_program_begin(uint32_t n,uint32_t crc) { (void)n;(void)crc;++begin_calls;return true; }
bool user_program_write(uint32_t off,const uint8_t *p,uint32_t n) { (void)off;(void)p;(void)n;++data_calls;return true; }
bool user_program_finish_download(void) { ++end_calls;return true; }
bool user_program_start(void) { ++run_calls;return true; }
void user_program_stop(void) { ++stop_calls;user_state=USER_PROGRAM_READY; }
void user_program_finish_execution(void) { ++finish_calls; }
bool user_program_set_display(const uint8_t *p) { (void)p;++display_calls;return true; }
user_program_state_t user_program_state(void) { return user_state; }
bool user_program_valid(void) { return true; }
uint32_t user_program_length(void) { return 1U; }
uint32_t user_program_received(void) { return 1U; }
uint8_t user_program_error(void) { return 0U; }
uint16_t user_program_error_line(void) { return 0U; }
uint8_t power_key_raw_level(void) { return 1U; }
uint8_t power_key_pressed(void) { return 0U; }
uint16_t power_key_short_count(void) { return 0U; }
robot_ui_state_t robot_ui_state(void) { return ROBOT_UI_STANDBY; }

/* Unchanged production parser; runner copies it so only headers are substituted. */
#include "protocol_source.c"

static uint32_t rd32(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static void wr32(uint8_t *p,uint32_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24); }
static void wr16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8); }
static void setup(void)
{
    memset(input,0,sizeof(input));memset(sent,0,sizeof(sent));memset(mock_meta,0,sizeof(mock_meta));
    in_size=in_pos=sent_size=0U;board_ms=100U;usb_reset_count=0U;
    reset_seen=owner=result_state=result_reason=motor_owner=shutdown_test=0U;motors_stopped=true;voice_arm_on_calls=motor_stop_calls=0;owner_token=0U;
    inject_reset=0U;reset_queue_size=0U;
    writes=open_calls=submit_calls=result_calls=close_calls=cancel_calls=stop_calls=0U;
    begin_calls=data_calls=end_calls=run_calls=display_calls=finish_calls=reset_calls=boot_requests=0U;
    user_state=USER_PROGRAM_RUNNING;used=last_byte=last_seq=last_crc=response_len=0U;cached=false;last_cmd=last_len=0U;
    submitted_size=0U;
    memset(cached_ports,0,sizeof(cached_ports));memset(cached_motor_state,0,sizeof(cached_motor_state));
    memset(cached_motor_goal,0,sizeof(cached_motor_goal));memset(cached_motor_ack,0,sizeof(cached_motor_ack));snapshot_reads=0;
}
static uint32_t append(uint32_t seq,uint16_t cmd,const uint8_t *p,uint16_t n)
{
    uint32_t start=in_size,crc;
    CHECK(in_size+16U+n<=sizeof(input));
    memcpy(input+start,"KLQ1",4U);wr32(input+start+4U,seq);wr16(input+start+8U,cmd);wr16(input+start+10U,n);
    if(n)memcpy(input+start+16U,p,n);
    crc=crc32_update(crc32_update(0U,input+start,12U),input+start+16U,n);wr32(input+start+12U,crc);
    in_size+=16U+n;return start;
}
static const uint8_t *expect(uint32_t seq,uint16_t cmd,uint32_t status,uint16_t n)
{
    CHECK(sent_size==20U+n && !memcmp(sent,"KLQ1",4U));
    CHECK(rd32(sent+4U)==seq && rd16(sent+8U)==(uint16_t)(cmd|0x8000U));
    CHECK(rd16(sent+10U)==n+4U && rd32(sent+16U)==status);
    CHECK(rd32(sent+12U)==crc32_update(crc32_update(0U,sent,12U),sent+16U,n+4U));
    return sent+20U;
}
static const uint8_t *command(uint32_t seq,uint16_t cmd,const uint8_t *p,uint16_t n,uint32_t status,uint16_t out_n)
{
    in_size=in_pos=sent_size=0U;append(seq,cmd,p,n);protocol_poll();return expect(seq,cmd,status,out_n);
}
static void open_session(void)
{
    uint8_t d[9];const uint8_t *out;
    d[0]=1U;wr32(d+1U,TOKEN);wr16(d+5U,3U);wr16(d+7U,1U);
    out=command(10U,XOPEN,d,9U,OK,4U);
    CHECK(rd32(out)==TOKEN && owner && selected==1U && expected_board==3U && expected_hw==1U);
}
static void open_and_forward(void)
{
    uint8_t p[164],f[16];unsigned before;
    setup();open_session();CHECK(stop_calls==1U);
    /* Repeated OPEN is handled by core idempotence, never a USB cached stale result. */
    p[0]=1U;wr32(p+1U,TOKEN);wr16(p+5U,3U);wr16(p+7U,1U);
    before=open_calls;command(10U,XOPEN,p,9U,OK,4U);CHECK(open_calls==before+1U && stop_calls==1U);
    wr32(p,TOKEN);memcpy(f,"KLQ1",4U);wr32(f+4U,1U);wr16(f+8U,1U);wr16(f+10U,0U);wr32(f+12U,crc32_update(0U,f,12U));
    memcpy(p+4U,f,16U);command(11U,XFER,p,20U,OK,0U);
    CHECK(submit_calls==1U && submitted_size==16U && !memcmp(submitted,f,16U));
    command(11U,XFER,p,20U,OK,0U);CHECK(submit_calls==2U);
    command(12U,XPOLL,p,4U,OK,1U);CHECK(sent[20U]==0U);
    result_state=1U;before=result_calls;command(12U,XPOLL,p,4U,OK,3U);CHECK(result_calls==before+1U && sent[20U]==1U);
    result_state=2U;result_reason=3U;command(12U,XPOLL,p,4U,OK,2U);CHECK(sent[20U]==2U && sent[21U]==3U);
    command(13U,XCLOSE,p,4U,OK,0U);CHECK(!owner && close_calls==1U);
}
static void malformed_requests(void)
{
    uint8_t p[164];uint32_t start;unsigned before;
    setup();memset(p,0,sizeof(p));
    command(1U,XOPEN,p,8U,ERR_LENGTH,0U);CHECK(!open_calls && !stop_calls);
    p[0]=3U;wr32(p+1U,TOKEN);wr16(p+5U,1U);wr16(p+7U,1U);
    command(2U,XOPEN,p,9U,ERR_RANGE,0U);CHECK(!owner && !stop_calls);
    p[0]=0U;wr32(p+1U,0U);command(3U,XOPEN,p,9U,ERR_RANGE,0U);CHECK(!owner && !stop_calls);
    in_size=in_pos=sent_size=0U;wr32(p+1U,TOKEN);start=append(4U,XOPEN,p,9U);input[start+12U]^=1U;
    before=open_calls;protocol_poll();expect(4U,XOPEN,ERR_CRC,0U);CHECK(open_calls==before && !owner && !stop_calls);
    setup();open_session();before=submit_calls;
    wr32(p,TOKEN);command(20U,XFER,p,19U,ERR_LENGTH,0U);CHECK(submit_calls==before);
    command(21U,XFER,p,153U,ERR_LENGTH,0U);CHECK(submit_calls==before);
    command(22U,XPOLL,p,3U,ERR_LENGTH,0U);CHECK(!result_calls);
    command(23U,XCLOSE,p,5U,ERR_LENGTH,0U);CHECK(!close_calls && owner);
    wr32(p,TOKEN+1U);command(24U,XPOLL,p,4U,ERR_STATE,0U);CHECK(owner);
    in_size=in_pos=sent_size=0U;wr32(p,TOKEN);start=append(25U,XFER,p,20U);input[start+12U]^=1U;
    protocol_poll();expect(25U,XFER,ERR_CRC,0U);CHECK(submit_calls==before);
    in_size=in_pos=sent_size=0U;start=append(26U,XCLOSE,p,4U);input[start+12U]^=1U;
    protocol_poll();expect(26U,XCLOSE,ERR_CRC,0U);CHECK(!close_calls && owner);
    command(27U,CMD_RESET,p,1U,ERR_LENGTH,0U);CHECK(!cancel_calls && !reset_calls && owner);
    p[0]=1U;wr32(p+1U,TOKEN+1U);wr16(p+5U,3U);wr16(p+7U,1U);
    command(28U,XOPEN,p,9U,ERR_STATE,0U);CHECK(owner && owner_token==TOKEN && stop_calls==1U);
}
static void ownership_and_resets(void)
{
    uint8_t p[16];unsigned before;
    setup();open_session();memset(p,0,sizeof(p));wr32(p,1U);
    command(30U,CMD_BEGIN,p,8U,ERR_STATE,0U);CHECK(!begin_calls);
    command(31U,CMD_DATA,p,5U,ERR_STATE,0U);CHECK(!data_calls);
    command(32U,CMD_END,0,0U,ERR_STATE,0U);CHECK(!end_calls);
    command(33U,CMD_RUN,0,0U,ERR_STATE,0U);CHECK(!run_calls);
    command(34U,CMD_DISPLAY,p,13U,ERR_STATE,0U);CHECK(!display_calls);
    command(35U,CMD_FINISH,0,0U,ERR_STATE,0U);CHECK(!finish_calls);
    before=stop_calls;command(36U,CMD_STOP,0,0U,OK,0U);CHECK(stop_calls==before+1U);
    command(37U,CMD_RESET,0,0U,OK,0U);CHECK(!owner && cancel_calls && reset_calls==1U && boot_requests==1U);
    setup();open_session();in_size=in_pos=sent_size=0U;append(40U,XPOLL,(const uint8_t *)"1234",4U);
    in_size=8U;protocol_poll();CHECK(used==8U);
    in_size=in_pos=0U;reset_seen=1U;protocol_poll();CHECK(!used && !owner && cancel_calls==1U);
    /* Same sequence can be a new request after USB reset, never an old cached OPEN. */
    open_session();CHECK(owner && open_calls==2U);
}
static void budget_and_info(void)
{
    const uint8_t *out;uint32_t n;unsigned i;
    setup();in_size=in_pos=sent_size=0U;
    for(i=0U;i<20U;++i)append(100U+i,CMD_ECHO,0,0U);
    protocol_poll();CHECK(in_pos>0U && in_pos<=256U && in_pos<in_size && writes<20U);
    protocol_poll();CHECK(in_pos==in_size && writes==20U);
    in_size=in_pos=sent_size=0U;append(200U,CMD_INFO,0,0U);protocol_poll();
    n=rd16(sent+10U)-4U;out=expect(200U,CMD_INFO,OK,(uint16_t)n);
    CHECK(n<1000U);sent[20U+n]=0U;
    CHECK(strstr((const char *)out,"EXT_MOTOR=1"));
    CHECK(strstr((const char *)out,"KLQ ROBOT FW ")==(const char *)out);
    CHECK(strstr((const char *)out,"EXT_UPGRADE=1") && strstr((const char *)out,"EXT_PORTS=3"));
}
static void reset_during_response(void)
{
    uint8_t p[20];
    setup();open_session();
    memset(p,0,sizeof(p));wr32(p,TOKEN);memcpy(p+4U,"KLQ1",4U);
    in_size=in_pos=sent_size=0U;append(301U,XFER,p,20U);
    reset_queue_size=(uint16_t)in_size;memcpy(reset_queue,input,reset_queue_size);
    in_size=in_pos=sent_size=0U;append(300U,XPOLL,p,4U);inject_reset=1U;
    protocol_poll();
    CHECK(!owner && cancel_calls==1U && !submitted_size);
    CHECK(sent_size==41U && rd32(sent+21U+16U)==ERR_STATE);
}
static void rejected_open_no_side_effects(void)
{
    uint8_t p[9]={0};setup();p[0]=3;wr32(p+1,TOKEN);wr16(p+5,1);wr16(p+7,1);in_size=in_pos=sent_size=0;append(750,XOPEN,p,9);protocol_poll();expect(750,XOPEN,ERR_RANGE,0);CHECK(!motor_stop_calls&&!stop_calls&&user_state==USER_PROGRAM_RUNNING);
    setup();shutdown_test=1;p[0]=0;in_size=in_pos=sent_size=0;append(751,XOPEN,p,9);protocol_poll();expect(751,XOPEN,ERR_STATE,0);CHECK(!open_calls&&!motor_stop_calls&&!stop_calls);
}
static void reset_quiesce(void)
{
    setup();motors_stopped=false;in_size=in_pos=sent_size=0;append(701,CMD_RESET,0,0);{uint8_t arm=1;append(703,CMD_VOICE_ARM,&arm,1);}protocol_poll();CHECK(in_pos<in_size&&!voice_arm_on_calls);expect(701,CMD_RESET,OK,0);CHECK(protocol_reset_pending()&&!reset_calls);
    board_ms+=149;protocol_poll();CHECK(protocol_reset_pending()&&!reset_calls);motors_stopped=true;protocol_poll();CHECK(!protocol_reset_pending()&&reset_calls==1);
    setup();motors_stopped=false;in_size=in_pos=sent_size=0;append(702,CMD_RESET,0,0);protocol_poll();CHECK(protocol_reset_pending());board_ms+=150;protocol_poll();CHECK(!protocol_reset_pending()&&reset_calls==1);
}
static void voice_diagnostics(void)
{
    const uint8_t *out;uint8_t p[4];setup();in_size=in_pos=sent_size=0;append(600,CMD_VOICE_STATUS,0,0);protocol_poll();out=expect(600,CMD_VOICE_STATUS,OK,108);CHECK(out[0]==1);
    in_size=in_pos=sent_size=0;append(601,CMD_VOICE_QUERY,0,0);protocol_poll();expect(601,CMD_VOICE_QUERY,OK,0);
    wr32(p,4999);in_size=in_pos=sent_size=0;append(602,CMD_VOICE_PLAY,p,4);protocol_poll();expect(602,CMD_VOICE_PLAY,ERR_RANGE,0);
    wr32(p,5002);in_size=in_pos=sent_size=0;append(603,CMD_VOICE_PLAY,p,4);protocol_poll();expect(603,CMD_VOICE_PLAY,OK,0);CHECK(user_state==USER_PROGRAM_RUNNING);
    p[0]=1;in_size=in_pos=sent_size=0;append(604,CMD_VOICE_ARM,p,1);protocol_poll();expect(604,CMD_VOICE_ARM,ERR_STATE,0);
    p[0]=2;in_size=in_pos=sent_size=0;append(605,CMD_VOICE_ARM,p,1);protocol_poll();expect(605,CMD_VOICE_ARM,ERR_RANGE,0);
    p[0]=0;in_size=in_pos=sent_size=0;append(606,CMD_VOICE_ARM,p,1);protocol_poll();expect(606,CMD_VOICE_ARM,OK,0);
}
static void motor_snapshot(void)
{
    const uint8_t *out; uint8_t bad=0; unsigned before;
    setup();cached_ports[0].online=cached_ports[1].online=1;
    cached_motor_state[1]=KLQ_MOTOR_ACTIVE;cached_motor_goal[1]=cached_motor_ack[1]=-1000;
    cached_motor_state[2]=KLQ_MOTOR_ERROR;cached_motor_ack[2]=333;
    out=command(900,CMD_MOTOR_STATUS,0,0,OK,19);
    CHECK(out[0]==1 && out[1]==KLQ_MOTOR_IDLE && out[2]==1 && !rd16(out+3) && !rd16(out+5));
    CHECK(out[7]==KLQ_MOTOR_ACTIVE && out[8]==1 && (int16_t)rd16(out+9)==-1000 && (int16_t)rd16(out+11)==-1000);
    CHECK(out[13]==KLQ_MOTOR_ERROR && !out[14] && !rd16(out+15) && rd16(out+17)==333 && snapshot_reads==12);
    CHECK(user_state==USER_PROGRAM_RUNNING && !stop_calls && !motor_stop_calls && !open_calls && !cancel_calls && !submit_calls);
    before=snapshot_reads;command(901,CMD_MOTOR_STATUS,&bad,1,ERR_LENGTH,0);CHECK(snapshot_reads==before);
    cached_motor_goal[1]=0;cached_motor_state[1]=KLQ_MOTOR_STOPPING;
    out=command(902,CMD_MOTOR_STATUS,0,0,OK,19);
    CHECK(out[7]==KLQ_MOTOR_STOPPING && !rd16(out+9) && (int16_t)rd16(out+11)==-1000);
    shutdown_test=1;out=command(903,CMD_MOTOR_STATUS,0,0,OK,19);
    CHECK(!rd16(out+9) && !stop_calls && !motor_stop_calls && !cancel_calls);
    setup();open_session();before=stop_calls;
    out=command(904,CMD_MOTOR_STATUS,0,0,OK,19);
    CHECK(owner && owner_token==TOKEN && stop_calls==before && !motor_stop_calls && !submit_calls && !cancel_calls && !result_calls);
    CHECK(user_state==USER_PROGRAM_READY && out[0]==1);
}
static void motor_stop_cancel(void)
{
    uint8_t invalid=0;
    setup();open_session();motor_owner=1U;
    in_size=in_pos=sent_size=0U;append(400U,CMD_STOP,&invalid,1U);protocol_poll();expect(400U,CMD_STOP,ERR_LENGTH,0U);
    CHECK(owner && !cancel_calls);
    in_size=in_pos=sent_size=0U;append(401U,CMD_STOP,0,0U);protocol_poll();expect(401U,CMD_STOP,OK,0U);
    CHECK(!owner && cancel_calls==1U && stop_calls==2U);
}
int main(void)
{
    open_and_forward();malformed_requests();ownership_and_resets();budget_and_info();reset_during_response();motor_stop_cancel();voice_diagnostics();motor_snapshot();reset_quiesce();rejected_open_no_side_effects();
    printf("USB bridge integration: %u assertions passed\n",checks);return 0;
}
