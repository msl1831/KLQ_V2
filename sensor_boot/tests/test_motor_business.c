#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apm32e10x.h"
#include "board.h"
volatile uint32_t board_ms;
USART_T test_uart[3];
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("motor business failed line %u: %s\n",__LINE__,#x); exit(1); } } while(0)
uint32_t crc32_update(uint32_t crc,const void *data,uint32_t n)
{
    const uint8_t *q=(const uint8_t *)data; unsigned b; crc=~crc;
    while(n--) {crc^=*q++;for(b=0;b<8;b++)crc=(crc>>1)^(0xedb88320UL&(0UL-(crc&1UL)));}return ~crc;
}
#include "external_ports_source.c"
static void setup(void)
{
    unsigned i;board_ms=0;memset(test_uart,0,sizeof(test_uart));
    for(i=0;i<3;i++)test_uart[i].STS_B.TXBEFLG=1;
    external_ports_init();for(i=0;i<3;i++)p[i].next_at=10000;
}
static void poll_at(uint32_t now){board_ms=now;external_ports_poll();}
static void inject(unsigned port,uint16_t cmd,uint32_t seq,const uint8_t *data,uint16_t n,uint32_t status)
{
    uint8_t f[80];unsigned i;memcpy(f,"KLQ1",4);p32(f+4,seq);p16(f+8,(uint16_t)(cmd|RESP));p16(f+10,(uint16_t)(n+4));p32(f+16,status);
    if(n)memcpy(f+20,data,n);p32(f+12,crc32_update(crc32_update(0,f,12),f+16,n+4));
    for(i=0;i<(unsigned)n+20U;i++){test_uart[port].DATA=f[i];test_uart[port].STS=0x20;if(!port)USART1_IRQHandler();else if(port==1)USART2_IRQHandler();else USART3_IRQHandler();}
    external_ports_poll();
}
static void normal_reply(unsigned port,const uint8_t *data,uint16_t n,uint32_t status)
{inject(port,p[port].command,p[port].sequence,data,n,status);}
static void info_payload(uint8_t d[36],unsigned port,uint32_t uid)
{
    memset(d,0,36);p16(d,1);d[2]=1;d[3]=2;p16(d+4,2);p16(d+6,2);p16(d+8,2);p32(d+12,0x20000);p32(d+16,9);p16(d+20,132);p16(d+22,18304);p32(d+24,uid);p32(d+28,0x2222);p32(d+32,0x3333+port);
}
static void info_reply(unsigned port,uint32_t uid)
{uint8_t d[36];info_payload(d,port,uid);normal_reply(port,d,36,0);}
static void discover(unsigned port)
{request(&p[port],CMD_INFO);info_reply(port,0x1000+port);CHECK(external_port_motor_ready(port));}
static void finish_tx(unsigned port,uint16_t cmd)
{
    unsigned n=0;while(p[port].motor_tx_n&&n++<30)poll_at(board_ms+1);
    CHECK(n<30&&p[port].pending&&p[port].command==cmd&&!p[port].motor_tx_n);
    CHECK(g16(p[port].motor_tx+8)==cmd);
    CHECK(g32(p[port].motor_tx+12)==crc32_update(crc32_update(0,p[port].motor_tx,12),p[port].motor_tx+16,cmd==CMD_MOTOR_SET?2:0));
}
static void set_ack(unsigned port,int16_t duty)
{uint8_t d[2];p16(d,(uint16_t)duty);normal_reply(port,d,2,0);}
static void start(unsigned port,int16_t duty)
{
    CHECK(external_port_motor_command(port,duty));CHECK(external_port_motor_state(port)==KLQ_MOTOR_PENDING);
    external_ports_poll();CHECK(p[port].pending&&p[port].command==CMD_INFO);
    info_reply(port,0x1000+port);finish_tx(port,CMD_MOTOR_SET);CHECK(p[port].motor_sent==duty);
    set_ack(port,duty);CHECK(external_port_motor_state(port)==KLQ_MOTOR_ACTIVE);CHECK(external_port_motor_ack_pwm(port)==duty);
}
static void stop_ack(unsigned port)
{external_ports_poll();finish_tx(port,CMD_MOTOR_STOP);normal_reply(port,0,0,0);CHECK(external_port_motor_ack_pwm(port)==0);}
static void identities_and_bounds(void)
{
    unsigned i;setup();CHECK(!external_port_motor_ready(3));CHECK(!external_port_motor_command(3,800));
    for(i=0;i<3;i++){
        CHECK(!external_port_motor_command(i,800));discover(i);
        p[i].s.hw_compat=1;CHECK(!external_port_motor_ready(i)&&!external_port_motor_command(i,800));p[i].s.hw_compat=2;
        p[i].s.capabilities=8;CHECK(!external_port_motor_ready(i));p[i].s.capabilities=9;
        p[i].s.mode=0;CHECK(!external_port_motor_ready(i));p[i].s.mode=1;
        p[i].s.board_id=3;CHECK(!external_port_motor_ready(i));p[i].s.board_id=2;
        p[i].s.device_class=1;CHECK(!external_port_motor_ready(i));p[i].s.device_class=2;
        p[i].s.sensor_type=1;CHECK(!external_port_motor_ready(i));p[i].s.sensor_type=2;
        p[i].s.online=0;CHECK(!external_port_motor_ready(i));p[i].s.online=1;
        CHECK(!external_port_motor_command(i,1001));CHECK(p[i].motor_goal==0&&p[i].motor_stop);stop_ack(i);
        CHECK(!external_port_motor(i,1,true,4));CHECK(!external_port_motor(i,0,true,1));stop_ack(i);
    }
}
static void normal_and_refresh(void)
{
    unsigned port,j;uint32_t due,count;setup();
    for(port=0;port<3;port++)discover(port);
    for(port=0;port<3;port++){
        start(port,(int16_t)(port==1?-800:800));count=p[port].s.request_count;due=p[port].motor_refresh;
        CHECK(external_port_motor_command(port,p[port].motor_goal));CHECK(p[port].s.request_count==count);
        poll_at(due-1);CHECK(!p[port].pending);poll_at(due);CHECK(p[port].command==CMD_INFO&&p[port].pending);
        info_reply(port,0x1000+port);finish_tx(port,CMD_MOTOR_SET);set_ack(port,(int16_t)(port==1?-800:800));
        CHECK(p[port].s.request_count==count+2);CHECK(p[port].retry==0);
        CHECK(external_port_motor_command(port,0));CHECK(external_port_motor_state(port)==KLQ_MOTOR_STOPPING);stop_ack(port);
        CHECK(external_port_motor_state(port)==KLQ_MOTOR_IDLE);
        for(j=0;j<3;j++)p[j].next_at=board_ms+10000;
    }
    CHECK(external_ports_motor_stopped());
}
static void three_port_refresh(void)
{
    uint32_t last[3]={0,0,0};unsigned seen[3]={0,0,0};unsigned i,j;setup();
    for(i=0;i<3;i++){discover(i);CHECK(external_port_motor_command(i,(int16_t)(i==1?-800:800)));}
    for(j=0;j<400;j++){
        poll_at(board_ms+1);
        for(i=0;i<3;i++)if(p[i].pending&&!p[i].motor_tx_n){
            if(p[i].command==CMD_INFO)info_reply(i,0x1000+i);
            else if(p[i].command==CMD_MOTOR_SET){
                if(seen[i])CHECK(board_ms-last[i]<=100U);
                last[i]=board_ms;++seen[i];set_ack(i,p[i].motor_sent);
            }
        }
    }
    for(i=0;i<3;i++)CHECK(seen[i]>=4&&external_port_motor_state(i)==KLQ_MOTOR_ACTIVE);
    external_ports_motor_stop_all();
    for(j=0;j<40;j++){
        poll_at(board_ms+1);
        for(i=0;i<3;i++)if(p[i].pending&&!p[i].motor_tx_n&&p[i].command==CMD_MOTOR_STOP)normal_reply(i,0,0,0);
    }
    CHECK(external_ports_motor_stopped());
}
static void timeout_no_resurrection(void)
{
    uint32_t count;setup();discover(0);CHECK(external_port_motor_command(0,800));external_ports_poll();info_reply(0,0x1000);finish_tx(0,CMD_MOTOR_SET);
    count=p[0].s.request_count;poll_at(p[0].deadline);CHECK(!p[0].motor_goal&&p[0].motor_fault&&p[0].motor_stop&&!p[0].pending);
    external_ports_poll();finish_tx(0,CMD_MOTOR_STOP);normal_reply(0,0,0,0);CHECK(p[0].s.request_count==count+1);CHECK(external_port_motor_state(0)==KLQ_MOTOR_ERROR);
    poll_at(board_ms+1000);CHECK(p[0].command==CMD_INFO&&!p[0].motor_goal);info_reply(0,0x1000);CHECK(!p[0].motor_goal&&external_port_motor_state(0)==KLQ_MOTOR_ERROR);
    CHECK(external_port_motor_command(0,0));stop_ack(0);CHECK(external_port_motor_state(0)==KLQ_MOTOR_IDLE);
    start(0,800);poll_at(p[0].motor_lease);CHECK(p[0].motor_fault&&!p[0].motor_goal);CHECK(p[0].command!=CMD_MOTOR_SET||p[0].motor_tx_n==0);
}
static void changed_identity_and_bad_ack(void)
{
    setup();discover(0);start(0,800);poll_at(p[0].motor_refresh);CHECK(p[0].command==CMD_INFO);info_reply(0,0x9000);
    CHECK(p[0].motor_fault&&!p[0].motor_goal&&!p[0].s.online);stop_ack(0);CHECK(external_port_motor_state(0)==KLQ_MOTOR_ERROR);
    setup();discover(1);CHECK(external_port_motor_command(1,-800));external_ports_poll();info_reply(1,0x1001);finish_tx(1,CMD_MOTOR_SET);set_ack(1,-799);
    CHECK(p[1].motor_fault&&!p[1].motor_goal&&!p[1].s.online);stop_ack(1);
    setup();discover(2);start(2,800);++p[2].s.uart_error_count;external_ports_poll();CHECK(p[2].motor_fault&&!p[2].motor_goal);stop_ack(2);
}
static void stop_partial_and_fault_budget(void)
{
    unsigned i;uint32_t due;setup();discover(0);CHECK(external_port_motor_command(0,800));external_ports_poll();info_reply(0,0x1000);
    poll_at(board_ms+1);CHECK(p[0].motor_tx_pos&&p[0].motor_tx_n);external_ports_motor_stop_all();due=p[0].motor_refresh;
    CHECK(!p[0].motor_goal&&!p[0].motor_tx_n&&p[0].motor_stop);external_ports_motor_stop_all();CHECK(p[0].motor_refresh==due);poll_at(due-1);CHECK(!p[0].pending);poll_at(due);stop_ack(0);CHECK(external_ports_motor_stopped());
    CHECK(external_port_motor_command(0,0));external_ports_poll();
    for(i=0;i<3;i++){finish_tx(0,CMD_MOTOR_STOP);poll_at(p[0].deadline);}
    CHECK(p[0].motor_fault&&!p[0].motor_stop&&!p[0].pending&&!p[0].motor_tx_n);CHECK(p[0].retry==2);CHECK(!external_ports_motor_stopped());
}
static void request_frame(uint8_t f[16],uint16_t cmd,uint32_t seq)
{memcpy(f,"KLQ1",4);p32(f+4,seq);p16(f+8,cmd);p16(f+10,0);p32(f+12,crc32_update(0,f,12));}
static void up_ack(unsigned port,const uint8_t *data,uint16_t n)
{const uint8_t *q=up.stage==UP_RAW?up.raw:up.internal;inject(port,g16(q+8),g32(q+4),data,n,0);}
static void wait_up(unsigned port,uint16_t cmd)
{unsigned i=0;while(!up.waiting&&i++<100)poll_at(board_ms+1);CHECK(i<100&&up.waiting&&g16((up.stage==UP_RAW?up.raw:up.internal)+8)==cmd);(void)port;}
static void upgrade_quiesce_and_cancel(void)
{
    uint8_t f[18],d[36];uint32_t due;setup();discover(0);discover(2);start(0,800);start(2,-800);
    CHECK(external_upgrade_open(2,123,2,2)==0);CHECK(up.pre_stop&&!p[0].motor_goal&&p[0].motor_stop&&!p[2].motor_goal);
    CHECK(!external_port_motor_command(0,800)&&!external_port_motor_ready(0));
    request_frame(f,1,99);CHECK(!external_upgrade_submit(123,f,16));wait_up(2,CMD_MOTOR_STOP);CHECK(up.stage==UP_STOP);up_ack(2,0,0);CHECK(!up.pre_stop);wait_up(2,CMD_INFO);
    info_payload(d,2,0x1002);up_ack(2,d,36);CHECK(up.pinned&&up.mode==1&&up.result==1);
    request_frame(f,CMD_MOTOR_SET,100);p16(f+10,2);p16(f+16,800);p32(f+12,crc32_update(crc32_update(0,f,12),f+16,2));CHECK(!external_upgrade_submit(123,f,18));wait_up(2,CMD_MOTOR_SET);p16(d,800);up_ack(2,d,2);
    CHECK(up.motor_control);external_upgrade_cancel();CHECK(!up.active&&p[2].motor_stop&&!external_ports_motor_stopped());due=p[2].motor_refresh;poll_at(due-1);CHECK(!p[2].pending);poll_at(due);stop_ack(2);CHECK(external_port_motor_state(2)==KLQ_MOTOR_IDLE);
    CHECK(!p[2].motor_goal);
}
static void power_levels_and_wrap(void)
{
    unsigned i;static const int16_t duty[3]={333,666,1000};
    for(i=0;i<3;i++){
        setup();discover(0);CHECK(external_port_motor(0,-1,true,i+1));CHECK(p[0].motor_goal==-duty[i]);external_ports_motor_stop_all();stop_ack(0);
    }
    setup();board_ms=0xFFFFFFE0UL;discover(0);start(0,800);poll_at(p[0].motor_refresh);CHECK(p[0].pending&&p[0].command==CMD_INFO);info_reply(0,0x1000);finish_tx(0,CMD_MOTOR_SET);set_ack(0,800);CHECK(external_port_motor_state(0)==KLQ_MOTOR_ACTIVE);
}
int main(void)
{
    identities_and_bounds();normal_and_refresh();three_port_refresh();timeout_no_resurrection();changed_identity_and_bad_ack();stop_partial_and_fault_budget();upgrade_quiesce_and_cancel();power_levels_and_wrap();
    printf("Normal motor business: %u assertions passed\n",checks);return 0;
}
