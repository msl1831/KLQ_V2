#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apm32e10x.h"
#include "board.h"
volatile uint32_t board_ms;
USART_T test_uart[3];
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("bridge failed line %u: %s\n",__LINE__,#x); exit(1); } } while(0)
uint32_t crc32_update(uint32_t crc,const void *data,uint32_t n)
{
    const uint8_t *p=(const uint8_t *)data;
    unsigned b; crc=~crc;
    while(n--) { crc^=*p++; for(b=0;b<8U;b++) crc=(crc>>1)^(0xedb88320UL&(0UL-(crc&1UL))); }
    return ~crc;
}
#include "external_ports_source.c"
static uint8_t req[148],output[64];
static uint16_t req_n,out_n;
static uint32_t serial_seq;
#define TOKEN 0x12345678UL
static void setup(void)
{
    unsigned i;
    board_ms=0U; memset(test_uart,0,sizeof(test_uart));
    for(i=0;i<3U;i++) test_uart[i].STS_B.TXBEFLG=1U;
    external_ports_init(); serial_seq=100U;
}
static void poll_at(uint32_t t) { board_ms=t; external_ports_poll(); }
static void make_request(uint16_t cmd,const uint8_t *data,uint16_t n)
{
    memcpy(req,"KLQ1",4U); p32(req+4U,++serial_seq); p16(req+8U,cmd); p16(req+10U,n);
    if(n) memcpy(req+16U,data,n);
    p32(req+12U,crc32_update(crc32_update(0,req,12U),req+16U,n)); req_n=(uint16_t)(16U+n);
}
static void submit(uint16_t cmd,const uint8_t *data,uint16_t n)
{ make_request(cmd,data,n); CHECK(external_upgrade_submit(TOKEN,req,req_n)==0U); }
static uint16_t wire_cmd(void) { return g16((up.stage==UP_RAW ? up.raw : up.internal)+8U); }
static void wait_tx(uint16_t cmd)
{
    unsigned i=0U;
    while(!up.waiting && !up.result && i++<400U) poll_at(board_ms+1U);
    CHECK(up.waiting && !up.result && wire_cmd()==cmd);
}
static void response_raw(const uint8_t *f,uint16_t n)
{
    uint16_t i;
    for(i=0U;i<n;i++) {
        test_uart[up.port].DATA=f[i]; test_uart[up.port].STS=0x20U;
        if(up.port==0U) USART1_IRQHandler(); else if(up.port==1U) USART2_IRQHandler(); else USART3_IRQHandler();
    }
    external_ports_poll();
}
static void response(const uint8_t *data,uint16_t n,uint32_t status)
{
    uint8_t f[100];
    const uint8_t *wire=up.stage==UP_RAW ? up.raw : up.internal;
    memcpy(f,wire,12U); p16(f+8U,(uint16_t)(g16(wire+8U)|0x8000U)); p16(f+10U,(uint16_t)(n+4U)); p32(f+16U,status);
    if(n) memcpy(f+20U,data,n);
    p32(f+12U,crc32_update(crc32_update(0,f,12U),f+16U,n+4U));
    response_raw(f,(uint16_t)(20U+n));
}
static void info(uint16_t board,uint16_t hw,uint8_t mode,uint32_t uid)
{
    uint8_t d[36]; memset(d,0,sizeof(d));
    p16(d,1U); d[2U]=mode; d[3U]=(uint8_t)(board==2U ? 2U : 1U);
    p16(d+4U,board); p16(d+6U,hw); p16(d+8U,(uint16_t)(board==1U ? 1U : 2U));
    p32(d+12U,0x10000UL); p32(d+16U,mode ? 15U : 8U); p16(d+20U,132U); p16(d+22U,18304U);
    p32(d+24U,uid); p32(d+28U,0x55667788UL); p32(d+32U,0x99AABBCCUL);
    response(d,36U,0U);
}
static void open_board(uint16_t board,uint16_t hw,uint8_t mode)
{
    CHECK(external_upgrade_open(0U,TOKEN,board,hw)==0U);
    submit(1U,0,0U); wait_tx(1U); info(board,hw,mode,0x11223344UL);
    if(!mode) { CHECK(!up.result && up.stage==UP_HOLD); wait_tx(9U); response(0,0U,0U); }
    CHECK(up.result==1U && up.pinned && up.mode==mode);
}
static void layout(void)
{
    uint8_t d[16]; p32(d,0x08001800UL); p16(d+4U,18304U); p16(d+6U,128U); p32(d+8U,0x08005F80UL); p32(d+12U,6144U);
    submit(0x0204U,0,0U); wait_tx(0x0204U); response(d,16U,0U); CHECK(up.result==1U && up.layout);
}
static void ownership(void)
{
    setup(); p[0].s.online=1U; p[0].s.ir_left=100U; p[0].pending=1U;
    CHECK(external_upgrade_open(3U,TOKEN,1U,1U)==5U);
    CHECK(external_upgrade_open(0U,0U,1U,1U)==5U);
    CHECK(external_upgrade_open(0U,TOKEN,2U,1U)==5U);
    CHECK(external_upgrade_open(0U,TOKEN,1U,1U)==0U);
    CHECK(!p[0].s.online && !p[0].s.ir_left && !p[0].pending);
    CHECK(external_upgrade_open(0U,TOKEN,1U,1U)==0U);
    CHECK(external_upgrade_open(1U,TOKEN,1U,1U)==4U);
    CHECK(external_upgrade_open(0U,TOKEN+1U,1U,1U)==4U);
    CHECK(external_upgrade_close(TOKEN+1U)==4U);
    CHECK(external_upgrade_result(TOKEN+1U,output,&out_n)==4U);
    CHECK(external_upgrade_result(TOKEN,output,&out_n)==0U && out_n==1U && output[0]==0U);
    submit(1U,0,0U); poll_at(49U); CHECK(!up.pos && !up.waiting);
    poll_at(50U); CHECK(up.pos==1U && !p[0].pending && p[1].pending);
    CHECK(external_upgrade_submit(TOKEN,req,req_n)==0U && up.pos==1U);
    CHECK(external_upgrade_close(TOKEN)==0U && !up.active);
    CHECK(external_upgrade_close(TOKEN)==0U);
    CHECK(external_upgrade_open(0U,TOKEN,1U,1U)==4U);
    CHECK(external_upgrade_open(0U,TOKEN+1U,1U,1U)==0U);
    CHECK(external_upgrade_close(TOKEN)==4U && up.active);
    external_upgrade_cancel(); CHECK(external_upgrade_close(TOKEN+1U)==0U);
}
static void identity_and_idempotence(void)
{
    uint32_t count;
    uint8_t saved[56];
    setup(); open_board(1U,1U,0U);
    CHECK(external_upgrade_result(TOKEN,output,&out_n)==0U && out_n==57U && output[0]==1U);
    memcpy(saved,output+1U,56U); count=p[0].s.request_count;
    CHECK(external_upgrade_submit(TOKEN,req,req_n)==0U);
    poll_at(board_ms+1U); CHECK(p[0].s.request_count==count);
    CHECK(external_upgrade_result(TOKEN,output,&out_n)==0U && !memcmp(saved,output+1U,56U));
    p16(req+8U,9U); p32(req+12U,crc32_update(0,req,12U));
    CHECK(external_upgrade_submit(TOKEN,req,req_n)==4U);
    submit(1U,0,0U); wait_tx(1U); info(1U,1U,0U,0x11223345UL);
    CHECK(up.result==2U && up.reason==3U && !up.boot_id && !up.layout && up.mode==2U);
    make_request(0x0204U,0,0U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==4U);
    setup(); CHECK(external_upgrade_open(0U,TOKEN,1U,1U)==0U);
    submit(1U,0,0U); wait_tx(1U); info(3U,1U,0U,0x11223344UL);
    CHECK(up.result==2U && up.reason==3U && !up.pinned);
}
static void gate_and_transfer(void)
{
    uint8_t b[132],d[16];
    unsigned i;
    setup(); open_board(3U,1U,0U);
    p16(b,3U); p16(b+2U,1U); p16(b+4U,240U); p32(b+6U,0x1234UL);
    make_request(0x0200U,b,10U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==4U);
    layout();
    p16(b,1U); make_request(0x0200U,b,10U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==5U); p16(b,3U);
    make_request(0x0200U,b,10U); req[12U]^=1U; CHECK(external_upgrade_submit(TOKEN,req,req_n)==3U);
    submit(0x0200U,b,10U); wait_tx(0x0200U); p16(d,0U); response(d,2U,0U); CHECK(up.image_n==240U);
    memset(b,0xA5,sizeof(b)); p16(b,0U); submit(0x0201U,b,130U); wait_tx(0x0201U); p16(d,128U); response(d,2U,0U);
    CHECK(up.result==1U); i=p[0].s.request_count; CHECK(external_upgrade_submit(TOKEN,req,req_n)==0U); CHECK(p[0].s.request_count==i);
    p16(b,1U); make_request(0x0201U,b,130U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==5U);
    p16(b,128U); submit(0x0201U,b,114U); wait_tx(0x0201U); p16(d,240U); response(d,2U,0U);
    submit(0x0202U,0,0U); wait_tx(0x0202U); response(0,0U,0U); CHECK(up.result==1U);
    submit(0x0203U,0,0U); wait_tx(0x0203U); response(0,0U,0U); CHECK(up.mode==2U && !up.layout && up.pinned);
    submit(1U,0,0U); wait_tx(1U); info(3U,1U,1U,0x11223344UL); CHECK(up.result==1U && up.mode==1U);
    make_request(0x0200U,b,10U); CHECK(external_upgrade_submit(TOKEN,req,req_n)!=0U);
}
static void motor_enter(void)
{
    uint32_t t;
    setup(); open_board(2U,2U,1U);
    submit(0x01F0U,0,0U); CHECK(up.stage==UP_STOP); wait_tx(0x0302U); response(0,0U,0U);
    wait_tx(0x01F0U); response(0,0U,0U); CHECK(up.stage==UP_RESET && !up.result);
    t=up.deadline; poll_at(t-1U); CHECK(up.stage==UP_RESET); poll_at(t); CHECK(up.stage==UP_HOLD);
    wait_tx(9U); response(0,0U,0U);
    CHECK(!up.boot_id);
    make_request(0x0204U,0,0U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==4U);
    CHECK(up.result==1U && up.reply_n==20U && g16(up.reply+8U)==0x81F0U && !g32(up.reply+16U));
    CHECK(g32(up.reply+12U)==crc32_update(crc32_update(0,up.reply,12U),up.reply+16U,4U));
    setup(); open_board(1U,1U,1U); submit(0x01F0U,0,0U); wait_tx(0x01F0U);
    poll_at(up.deadline); CHECK(up.stage==UP_HOLD && !up.result); wait_tx(9U); response(0,0U,0U); CHECK(up.result==1U && up.mode==0U);
    setup(); open_board(2U,2U,1U); submit(0x01F0U,0,0U); wait_tx(0x0302U); response(0,0U,5U);
    CHECK(up.result==2U && up.reason==4U && up.stage==UP_IDLE);
}
static void errors_and_recovery(void)
{
    uint8_t f[20]; unsigned i;
    setup(); CHECK(external_upgrade_open(0U,TOKEN,1U,1U)==0U); submit(1U,0,0U); wait_tx(1U);
    make_request(9U,0,0U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==4U);
    for(i=0;i<3U;i++) { poll_at(up.deadline); if(i<2U) wait_tx(1U); }
    CHECK(up.result==2U && up.reason==1U && up.active);
    submit(1U,0,0U); wait_tx(1U); test_uart[0].STS=1U; USART1_IRQHandler(); external_ports_poll(); CHECK(up.result==2U && up.reason==2U);
    submit(1U,0,0U); wait_tx(1U); memcpy(f,up.raw,12U); p16(f+8U,0x8001U); p16(f+10U,4U); p32(f+16U,0U);
    p32(f+12U,crc32_update(crc32_update(0,f,12U),f+16U,4U)^1U);
    for(i=0;i<3U;i++) response_raw(f,20U);
    CHECK(up.result==2U && up.reason==4U);
    setup(); CHECK(external_upgrade_open(0U,TOKEN,1U,1U)==0U); submit(1U,0,0U); test_uart[0].STS_B.TXBEFLG=0U;
    poll_at(50U); poll_at(up.deadline); CHECK(up.result==2U && up.reason==2U);
}
static void failed_info_and_data_retry(void)
{
    uint8_t b[130],d[2]; unsigned i;
    setup(); open_board(1U,1U,0U); layout();
    submit(1U,0,0U); wait_tx(1U); response(0,0U,5U);
    CHECK(!up.boot_id && !up.layout && up.mode==2U);
    make_request(0x0204U,0,0U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==4U);
    submit(1U,0,0U); wait_tx(1U); info(1U,1U,0U,0x11223344UL); wait_tx(9U); response(0,0U,0U); layout();
    p16(b,1U); p16(b+2U,1U); p16(b+4U,240U); p32(b+6U,0x1234UL);
    submit(0x0200U,b,10U); wait_tx(0x0200U); p16(d,0U); response(d,2U,0U);
    memset(b,0xA5,sizeof(b)); p16(b,0U); submit(0x0201U,b,130U); wait_tx(0x0201U);
    for(i=0;i<3U;i++) { poll_at(up.deadline); if(i<2U) wait_tx(0x0201U); }
    CHECK(up.result==2U && up.reason==1U && up.boot_id && up.layout && up.image_n==240U);
    submit(0x0201U,b,130U); wait_tx(0x0201U); p16(d,128U); response(d,2U,0U); CHECK(up.result==1U);
}
static void wrap_lease_and_close(void)
{
    setup(); board_ms=0xFFFFFFF0UL; CHECK(external_upgrade_open(0U,TOKEN,1U,1U)==0U);
    submit(1U,0,0U); poll_at(0xFFFFFFF1UL); CHECK(!up.pos); poll_at(40U); CHECK(up.pos==1U);
    CHECK(external_upgrade_result(TOKEN,output,&out_n)==0U);
    poll_at(30039U); CHECK(up.active); poll_at(30040U); CHECK(!up.active);
    CHECK(external_upgrade_close(TOKEN)==0U); CHECK(external_upgrade_result(TOKEN,output,&out_n)==4U);
    CHECK(!p[0].s.online && !p[0].used && !p[0].expected && !p[0].pending);
    poll_at(p[0].next_at); CHECK(p[0].pending && p[0].command==1U);
}
static void motor_control_gate(void)
{
    uint8_t b[3]={0,0,0}; unsigned i;
    setup(); CHECK(external_upgrade_open(0U,TOKEN,2U,2U)==0U);
    make_request(0x0300U,b,2U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==4U);
    make_request(0x0301U,0,0U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==4U);
    CHECK(!external_motor_control_active());
    for(i=1U;i<=3U;i+=2U) {
        setup(); open_board((uint16_t)i,1U,1U);
        make_request(0x0300U,b,2U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==5U);
        make_request(0x0301U,0,0U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==5U);
        CHECK(!external_motor_control_active());
    }
    setup(); open_board(2U,2U,0U);
    make_request(0x0300U,b,2U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==5U);
    make_request(0x0301U,0,0U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==5U);
    setup(); open_board(2U,2U,1U);
    up.hw=1U; make_request(0x0300U,b,2U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==5U);
    up.hw=2U;
    for(i=0U;i<=3U;i++) if(i!=2U) {
        make_request(0x0300U,b,(uint16_t)i); CHECK(external_upgrade_submit(TOKEN,req,req_n)==2U);
    }
    make_request(0x0301U,b,1U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==2U);
    p16(b,1001U); make_request(0x0300U,b,2U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==5U);
    p16(b,(uint16_t)-1001); make_request(0x0300U,b,2U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==5U);
    CHECK(!external_motor_control_active());
    submit(1U,0,0U); wait_tx(1U); info(2U,2U,1U,0x11223345UL);
    CHECK(up.result==2U && up.reason==3U && up.mode==2U);
    p16(b,100U); make_request(0x0300U,b,2U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==5U);
    make_request(0x0301U,0,0U); CHECK(external_upgrade_submit(TOKEN,req,req_n)==5U);
}
static void motor_control_responses(void)
{
    static const int16_t duty[]={-1000,-1,0,1,1000};
    uint8_t b[3]={0,0,0},d[9]; unsigned i; uint32_t count;
    setup(); open_board(2U,2U,1U); memset(d,0,sizeof(d));
    submit(0x0301U,0,0U); CHECK(external_motor_control_active()); wait_tx(0x0301U); response(d,8U,0U);
    CHECK(up.result==1U);
    CHECK(external_upgrade_close(TOKEN)==0U && !external_motor_control_active());
    setup(); open_board(2U,2U,1U);
    for(i=0U;i<sizeof(duty)/sizeof(duty[0]);i++) {
        p16(b,(uint16_t)duty[i]); submit(0x0300U,b,2U);
        CHECK(external_motor_control_active()); wait_tx(0x0300U);
        CHECK(up.deadline-board_ms==100U); response(b,2U,0U);
        CHECK(up.result==1U && up.reply_n==22U);
    }
    count=p[0].s.request_count;
    CHECK(external_upgrade_submit(TOKEN,req,req_n)==0U);
    poll_at(board_ms+150U); CHECK(p[0].s.request_count==count && up.stage==UP_IDLE);
    p16(b,200U);
    for(i=0U;i<=3U;i++) if(i!=2U) {
        submit(0x0300U,b,2U); wait_tx(0x0300U); response(b,(uint16_t)i,0U);
        CHECK(up.result==2U && up.reason==4U);
    }
    submit(0x0300U,b,2U); wait_tx(0x0300U); p16(d,201U); response(d,2U,0U);
    CHECK(up.result==2U && up.reason==4U);
    memset(d,0,sizeof(d)); p32(d,0xFFFFFFFEUL); p16(d+4U,(uint16_t)-1000); d[7]=1U;
    submit(0x0301U,0,0U); wait_tx(0x0301U); response(d,8U,0U);
    CHECK(up.result==1U && up.reply_n==28U);
    for(i=0U;i<5U;i++) {
        memset(d,0,sizeof(d));
        if(i==0U) p16(d+4U,1001U);
        else if(i==1U) p16(d+4U,(uint16_t)-1001);
        else if(i==2U) d[6]=1U;
        else if(i==3U) d[7]=2U;
        submit(0x0301U,0,0U); wait_tx(0x0301U); response(d,(uint16_t)(i==4U ? 7U : 8U),0U);
        CHECK(up.result==2U && up.reason==4U);
    }
    submit(0x0301U,0,0U); wait_tx(0x0301U); response(d,9U,0U);
    CHECK(up.result==2U && up.reason==4U);
    CHECK(external_motor_control_active());
    submit(0x0302U,0,0U); wait_tx(0x0302U); response(0,0U,0U);
    CHECK(up.result==1U);
    CHECK(external_upgrade_close(TOKEN)==0U && !external_motor_control_active());
}
static void motor_control_timeout_cancel(void)
{
    uint8_t b[2]; uint32_t t,count;
    setup(); open_board(2U,2U,1U); board_ms=0xFFFFFFF0UL;
    p16(b,200U); submit(0x0300U,b,2U); wait_tx(0x0300U);
    count=p[0].s.request_count; t=up.deadline;
    poll_at(t-1U); CHECK(up.waiting && !up.result);
    poll_at(t); CHECK(up.result==2U && up.reason==1U && !up.retry && !up.waiting);
    CHECK(up.stage==UP_IDLE && external_motor_control_active());
    poll_at(t+600U); CHECK(p[0].s.request_count==count);
    CHECK(external_upgrade_submit(TOKEN,req,req_n)==0U);
    poll_at(t+900U); CHECK(p[0].s.request_count==count && up.result==2U);
    submit(0x0302U,0,0U); wait_tx(0x0302U); response(0,0U,0U);
    CHECK(up.result==1U);
    submit(0x0300U,b,2U); wait_tx(0x0300U);
    external_upgrade_cancel();
    CHECK(!up.active && !up.raw_n && !external_motor_control_active());
    CHECK(external_upgrade_submit(TOKEN,req,req_n)==4U);
    CHECK(external_upgrade_open(0U,TOKEN,2U,2U)==4U);
    poll_at(board_ms+600U); CHECK(!up.active && p[0].pending && p[0].command==1U);
}

int main(void)
{
    ownership(); identity_and_idempotence(); gate_and_transfer(); motor_enter(); errors_and_recovery(); failed_info_and_data_retry(); wrap_lease_and_close();
    motor_control_gate(); motor_control_responses(); motor_control_timeout_cancel();
    printf("USB port upgrade bridge: %u assertions passed\n",checks); return 0;
}
