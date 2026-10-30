#define main motor_baseline_main
#include "test_motor_business.c"
#undef main

static void sensor(unsigned type)
{
    setup();p[0].s.online=p[0].s.mode=p[0].s.device_class=1;
    p[0].s.sensor_type=(uint16_t)type;p[0].s.capabilities=1;
}
static void sample_data(uint8_t d[28],uint32_t seq,uint32_t capture)
{
    memset(d,0,28);p32(d,seq);p32(d+4,capture);p16(d+8,300);p16(d+10,100);d[12]=1;
}
static void sample_reply(uint32_t seq,uint32_t capture)
{
    uint8_t d[28];sample_data(d,seq,capture);normal_reply(0,d,16,0);
}
static void queue_sample(uint32_t seq,uint32_t capture)
{
    uint8_t f[36],d[28];unsigned i;
    memcpy(f,"KLQ1",4);p32(f+4,p[0].sequence);p16(f+8,RESP|CMD_SAMPLE);p16(f+10,20);p32(f+16,0);
    sample_data(d,seq,capture);memcpy(f+20,d,16);
    p32(f+12,crc32_update(crc32_update(0,f,12),f+16,20));
    for(i=0;i<sizeof(f);i++){test_uart[0].DATA=f[i];test_uart[0].STS=0x20;USART1_IRQHandler();}
}
static void freshness(void)
{
    uint32_t responses,old_seq;
    sensor(KLQ_SENSOR_CS100A);board_ms=100;request(&p[0],CMD_SAMPLE);sample_reply(7,123);
    CHECK(p[0].s.sample_valid&&p[0].s.sample_host_ms==100);responses=p[0].s.response_count;
    board_ms=1000;request(&p[0],CMD_SAMPLE);sample_reply(7,123);
    CHECK(p[0].s.response_count==responses+1&&p[0].s.sample_host_ms==100);
    CHECK(board_ms-p[0].s.sample_host_ms==900);
    request(&p[0],CMD_SAMPLE);sample_reply(8,123);CHECK(p[0].s.sample_host_ms==100);
    request(&p[0],CMD_SAMPLE);sample_reply(7,124);CHECK(p[0].s.sample_host_ms==100);
    request(&p[0],CMD_SAMPLE);sample_reply(8,124);CHECK(p[0].s.sample_host_ms==1000);
    clear_sample(&p[0]);CHECK(!p[0].s.sample_valid);
    board_ms=0;request(&p[0],CMD_SAMPLE);sample_reply(0,0);CHECK(p[0].s.sample_valid&&p[0].s.sample_host_ms==0);
    sensor(KLQ_SENSOR_CS100A);board_ms=100;request(&p[0],CMD_SAMPLE);old_seq=p[0].sequence;
    board_ms=101;queue_sample(1,1);CHECK(p[0].rt!=p[0].rh);
    poll_at(1000);CHECK(!p[0].s.sample_valid&&p[0].s.response_count==0&&p[0].s.timeout_count==1);
    CHECK(p[0].pending&&p[0].sequence!=old_seq&&p[0].retry==1&&p[0].rt==p[0].rh);
    {uint8_t d[28];sample_data(d,1,1);inject(0,CMD_SAMPLE,old_seq,d,16,0);}
    CHECK(!p[0].s.sample_valid&&p[0].pending&&p[0].s.response_count==0);
    sample_reply(2,2);CHECK(p[0].s.sample_valid&&p[0].s.sample_sequence==2&&p[0].s.sample_host_ms==1000);
    sensor(KLQ_SENSOR_CS100A);board_ms=100;request(&p[0],CMD_SAMPLE);board_ms=139;queue_sample(1,1);
    poll_at(140);CHECK(!p[0].s.sample_valid&&p[0].s.timeout_count==1);
    unbind(&p[0]);CHECK(!p[0].s.sample_valid&&!p[0].s.sample_host_ms);
}
static void short_sample_payload(void)
{
    uint8_t d[28];unsigned type,n;
    sample_data(d,1,1);
    for(type=1;type<=2;type++)for(n=0;n<8;n++) {
        sensor(type);request(&p[0],CMD_SAMPLE);normal_reply(0,d,(uint16_t)n,0);
        CHECK(!p[0].s.sample_valid&&!p[0].s.sample_host_ms&&!p[0].s.sample_sequence);
        CHECK(!p[0].s.response_count&&p[0].s.frame_error_count==1);
    }
}
static void ir_freshness(void)
{
    uint8_t d[28];uint32_t responses;
    sensor(KLQ_SENSOR_IR_DUAL);memset(d,0,sizeof(d));p16(d+12,10);d[24]=1;
    board_ms=0;request(&p[0],CMD_SAMPLE);normal_reply(0,d,28,0);
    CHECK(p[0].s.sample_valid&&p[0].s.sample_host_ms==0&&p[0].s.ir_left==10);
    responses=p[0].s.response_count;board_ms=100;request(&p[0],CMD_SAMPLE);p16(d+12,900);normal_reply(0,d,28,0);
    CHECK(p[0].s.response_count==responses+1&&p[0].s.sample_host_ms==0&&p[0].s.ir_left==10);
    p32(d,1);p32(d+4,1);request(&p[0],CMD_SAMPLE);normal_reply(0,d,28,0);
    CHECK(p[0].s.sample_host_ms==100&&p[0].s.ir_left==900);
    clear_sample(&p[0]);p32(d,0xFFFFFFFFUL);p32(d+4,0xFFFFFFFFUL);
    request(&p[0],CMD_SAMPLE);normal_reply(0,d,28,0);board_ms=101;p32(d,0);p32(d+4,0);
    request(&p[0],CMD_SAMPLE);normal_reply(0,d,28,0);CHECK(p[0].s.sample_host_ms==101&&p[0].s.sample_valid);
}
static void cadence(void)
{
    uint8_t d[28];uint32_t count;
    sensor(KLQ_SENSOR_CS100A);board_ms=100;request(&p[0],CMD_SAMPLE);
    board_ms=109;sample_reply(1,1);CHECK(p[0].next_at==200);
    poll_at(199);CHECK(!p[0].pending);poll_at(200);CHECK(p[0].pending&&p[0].request_at==200);
    board_ms=205;sample_reply(2,2);external_ports_set_active(true);CHECK(p[0].next_at==205);
    external_ports_poll();CHECK(p[0].pending&&p[0].request_at==205&&p[0].next_at==245);
    board_ms=214;sample_reply(3,3);CHECK(p[0].next_at==245);
    sensor(KLQ_SENSOR_CS100A);board_ms=100;request(&p[0],CMD_SAMPLE);board_ms=110;external_ports_set_active(true);
    CHECK(p[0].next_at==140);sample_reply(1,1);CHECK(p[0].next_at==140);
    sensor(KLQ_SENSOR_IR_DUAL);sample_data(d,1,1);p16(d+8,0);p16(d+10,0);d[24]=1;
    board_ms=100;request(&p[0],CMD_SAMPLE);board_ms=105;normal_reply(0,d,28,0);CHECK(p[0].next_at==120);
    poll_at(120);CHECK(p[0].request_at==120&&p[0].pending);
    board_ms=150;p32(d,2);p32(d+4,2);normal_reply(0,d,28,0);CHECK(p[0].next_at==170&&!p[0].pending);
    count=p[0].s.request_count;external_ports_poll();CHECK(!p[0].pending&&p[0].s.request_count==count);
    poll_at(170);CHECK(p[0].pending&&p[0].s.request_count==count+1);
    sensor(KLQ_SENSOR_CS100A);board_ms=0xFFFFFFF0UL;request(&p[0],CMD_SAMPLE);sample_reply(1,1);
    CHECK(p[0].next_at==84);poll_at(83);CHECK(!p[0].pending);poll_at(84);CHECK(p[0].pending);
}
static void queued_motor_ack(void)
{
    uint8_t f[22];unsigned i;
    setup();discover(0);CHECK(external_port_motor_command(0,800));external_ports_poll();info_reply(0,0x1000);finish_tx(0,CMD_MOTOR_SET);
    memcpy(f,"KLQ1",4);p32(f+4,p[0].sequence);p16(f+8,RESP|CMD_MOTOR_SET);p16(f+10,6);p32(f+16,0);p16(f+20,800);
    p32(f+12,crc32_update(crc32_update(0,f,12),f+16,6));++board_ms;
    for(i=0;i<sizeof(f);i++){test_uart[0].DATA=f[i];test_uart[0].STS=0x20;USART1_IRQHandler();}
    poll_at(p[0].motor_lease+300);
    CHECK(p[0].motor_fault&&!p[0].motor_goal&&!p[0].motor_ack&&p[0].motor_stop&&!p[0].s.online);
    CHECK(external_port_motor_state(0)==KLQ_MOTOR_ERROR&&!external_port_motor_command(0,-800));
}
static void independent_ports(void)
{
    uint8_t d[36];
    setup();info_payload(d,0,0x1234);request(&p[0],CMD_INFO);normal_reply(0,d,36,0);
    request(&p[1],CMD_INFO);normal_reply(1,d,36,0);
    CHECK(external_port_motor_ready(0)&&external_port_motor_ready(1)&&!external_port_motor_ready(2));
    CHECK(external_port_motor_command(0,800)&&external_port_motor_command(1,-800));
    ++p[2].s.uart_error_count;CHECK(external_port_motor_ready(0)&&external_port_motor_ready(1));
    CHECK(p[0].motor_goal==800&&p[1].motor_goal==-800&&!p[2].s.online);
    external_ports_motor_stop_all();stop_ack(0);stop_ack(1);CHECK(external_ports_motor_stopped());
}
static void lease_entrypoints(void)
{
    uint32_t lease;unsigned n;
    setup();discover(0);start(0,800);lease=p[0].motor_lease;
    for(n=0;n<1000;n++)CHECK(external_port_motor_command(0,800));
    CHECK(p[0].motor_lease==lease&&!p[0].motor_fault);
    board_ms=lease;CHECK(!external_port_motor_ready(0));CHECK(p[0].motor_fault&&!p[0].motor_goal&&p[0].motor_stop);
    CHECK(!external_port_motor_command(0,-800));CHECK(external_port_motor_state(0)==KLQ_MOTOR_ERROR);
    stop_ack(0);request(&p[0],CMD_INFO);info_reply(0,0x1000);CHECK(!external_port_motor_ready(0));CHECK(!external_port_motor_command(0,-800));
    CHECK(external_port_motor_command(0,0));stop_ack(0);start(0,-800);
    setup();discover(0);start(0,800);board_ms=p[0].motor_lease+300;
    CHECK(!external_port_motor_command(0,-800)&&p[0].motor_fault&&!p[0].motor_goal);
    setup();discover(0);start(0,800);board_ms=p[0].motor_lease+300;
    CHECK(external_port_motor_state(0)==KLQ_MOTOR_ERROR&&!p[0].motor_goal);
    setup();discover(0);CHECK(external_port_motor_command(0,800));external_ports_poll();info_reply(0,0x1000);finish_tx(0,CMD_MOTOR_SET);
    board_ms=p[0].deadline;set_ack(0,800);CHECK(p[0].motor_fault&&!p[0].motor_goal&&p[0].motor_ack==0);
}
int main(void)
{
    freshness();short_sample_payload();ir_freshness();cadence();queued_motor_ack();independent_ports();lease_entrypoints();
    printf("Port freshness/cadence/lease: %u assertions passed\n",checks);return 0;
}
