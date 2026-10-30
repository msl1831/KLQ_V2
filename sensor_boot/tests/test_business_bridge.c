#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "apm32e10x.h"
#include "apm32e10x_gpio.h"
#include "protocol.h"
#include "usb_serial.h"
#include "user_program.h"
#include "mini_python.h"
#include "klq_runtime.h"
#include "robot_ui.h"
#include "power_key.h"
#include "external_ports.h"
#include "voice_link.h"
#include "voice_control.h"

volatile uint32_t board_ms,usb_reset_count;
USART_T test_uart[3];
uint8_t mock_app[32],mock_meta[32];
static uint8_t input[1100],reply[1100],reset_seen,pc3=1,pc4=1,picture[13];
static uint16_t in_n,in_at,reply_n;
static uint32_t sequence;
static unsigned checks,off_calls,last_icon;
#define CHECK(c) do {++checks;if(!(c)){printf("business bridge line %u: %s\n",__LINE__,#c);exit(1);}} while(0)
static void p16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void p32(uint8_t *p,uint32_t v){p16(p,(uint16_t)v);p16(p+2,(uint16_t)(v>>16));}
static uint32_t u32(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
int usb_serial_read(void){return in_at<in_n?input[in_at++]:-1;}
bool usb_serial_write(const uint8_t *p,uint32_t n){CHECK(n<=sizeof(reply));memcpy(reply,p,n);reply_n=(uint16_t)n;return true;}
bool usb_serial_reset_seen(void){uint8_t n=reset_seen;reset_seen=0;return n!=0;}
void RCM_EnableAPB2PeriphClock(unsigned v){(void)v;}
void GPIO_Config(void *p,GPIO_Config_T *io){(void)p;CHECK(io->pin==GPIO_PIN_3||io->pin==GPIO_PIN_4);}
unsigned GPIO_ReadInputBit(void *p,unsigned pin){(void)p;return pin==GPIO_PIN_4?pc4:pc3;}
void delay_ms(uint32_t n){board_ms+=n;}
void display_icon(unsigned n){last_icon=n;}
void display_columns(const uint8_t *p){memcpy(picture,p,13);}
void display_clear(void){memset(picture,0,13);}
void usb_disconnect(void){}
void board_request_bootloader(void){}
void board_jump(uint32_t n){(void)n;CHECK(0);}
void NVIC_SystemReset(void){}
void board_power_off(bool usb_active){CHECK(usb_active&&!external_upgrade_active());++off_calls;}

uint8_t voice_test_tx_ready(void){return 1u;}
void voice_test_tx(uint8_t b){(void)b;}
static void setup(void)
{
    unsigned i;
    board_ms=100;pc3=pc4=1;off_calls=0;in_n=in_at=reply_n=0;
    memset(mock_meta,0,sizeof(mock_meta));memset(test_uart,0,sizeof(test_uart));
    for(i=0;i<3;i++)test_uart[i].STS_B.TXBEFLG=1;
    external_ports_init();user_program_init();klq_runtime_init();robot_ui_init();robot_ui_set_standby();power_key_init();voice_link_init();voice_control_init();
    board_ms+=25;power_key_poll();reset_seen=1;protocol_poll();
}
static void rpc(uint16_t cmd,const uint8_t *p,uint16_t n,uint32_t status)
{
    CHECK(n<=1028);memcpy(input,"KLQ1",4);p32(input+4,++sequence);p16(input+8,cmd);p16(input+10,n);
    if(n)memcpy(input+16,p,n);
    p32(input+12,crc32_update(crc32_update(0,input,12),input+16,n));
    in_n=(uint16_t)(16+n);in_at=reply_n=0;
    while(in_at<in_n)protocol_poll();
    CHECK(reply_n>=20&&u32(reply+4)==sequence&&u32(reply+16)==status);
    CHECK(u32(reply+12)==crc32_update(crc32_update(0,reply,12),reply+16,reply_n-16));
}
static void load(const char *s)
{
    uint8_t data[300];uint16_t n=(uint16_t)strlen(s);
    CHECK(n<sizeof(data)-4);p32(data,n);p32(data+4,crc32_update(0,s,n));rpc(CMD_BEGIN,data,8,OK);
    p32(data,0);memcpy(data+4,s,n);rpc(CMD_DATA,data,(uint16_t)(n+4),OK);rpc(CMD_END,0,0,OK);
    CHECK(user_program_valid()&&robot_ui_state()==ROBOT_UI_DOWNLOAD_COMPLETE);
    board_ms+=800;robot_ui_poll();CHECK(robot_ui_state()==ROBOT_UI_STANDBY);
}
static void open(uint32_t token){uint8_t data[9];data[0]=0;p32(data+1,token);p16(data+5,1);p16(data+7,1);rpc(CMD_EXT_OPEN,data,9,OK);CHECK(external_upgrade_active());}
static void close(uint32_t token,uint32_t status){uint8_t data[4];p32(data,token);rpc(CMD_EXT_CLOSE,data,4,status);}
static void short_press(void){pc4=0;power_key_poll();board_ms+=25;power_key_poll();board_ms+=100;pc4=1;power_key_poll();board_ms+=25;power_key_poll();}

static void ownership(void)
{
    unsigned i;uint32_t n;
    setup();load("import klq\nwhile True:\n    pass\n");n=user_program_length();rpc(CMD_RUN,0,0,OK);
    for(i=0;i<100;i++)user_program_poll();CHECK(user_program_state()==USER_PROGRAM_RUNNING);
    open(11);CHECK(user_program_state()==USER_PROGRAM_READY&&mini_python_status()==MINI_DONE&&user_program_length()==n);
    CHECK(!user_program_start());short_press();CHECK(user_program_state()==USER_PROGRAM_READY&&power_key_short_count()==1);
    rpc(CMD_RUN,0,0,ERR_STATE);rpc(CMD_BEGIN,0,0,ERR_STATE);rpc(CMD_STOP,0,0,OK);CHECK(external_upgrade_active());
    close(12,ERR_STATE);CHECK(external_upgrade_active());close(11,OK);
    CHECK(!external_upgrade_active()&&user_program_state()==USER_PROGRAM_READY&&user_program_valid());
    rpc(CMD_RUN,0,0,OK);CHECK(user_program_state()==USER_PROGRAM_RUNNING);rpc(CMD_STOP,0,0,OK);
    puts("PASS real Python/RAM and upgrade ownership; physical short press cannot restart");
}
static void resets(void)
{
    uint8_t data[8];
    setup();load("while True:\n    pass\n");rpc(CMD_RUN,0,0,OK);
    reset_seen=1;protocol_poll();CHECK(user_program_state()==USER_PROGRAM_RUNNING&&user_program_valid());
    open(22);reset_seen=1;protocol_poll();CHECK(!external_upgrade_active()&&user_program_state()==USER_PROGRAM_READY&&user_program_valid());
    p32(data,8);p32(data+4,0);rpc(CMD_BEGIN,data,8,OK);CHECK(user_program_state()==USER_PROGRAM_DOWNLOADING);
    reset_seen=1;protocol_poll();CHECK(!user_program_valid()&&user_program_state()==USER_PROGRAM_EMPTY&&robot_ui_state()==ROBOT_UI_STANDBY);
    puts("PASS USB reset preserves complete RAM, cancels partial RAM and upgrade");
}
static void power_and_display(void)
{
    setup();load("import klq\nklq.display_number(12)\nklq.wait(100)\n");rpc(CMD_RUN,0,0,OK);
    user_program_poll();user_program_poll();user_program_poll();CHECK(robot_ui_state()==ROBOT_UI_USER_PATTERN);
    short_press();CHECK(user_program_state()==USER_PROGRAM_READY&&robot_ui_state()==ROBOT_UI_STANDBY);
    short_press();CHECK(user_program_state()==USER_PROGRAM_RUNNING&&power_key_short_count()==2);
    open(33);pc4=0;power_key_poll();board_ms+=25;power_key_poll();
    board_ms+=1499;power_key_poll();CHECK(!off_calls&&external_upgrade_active());
    ++board_ms;power_key_poll();CHECK(off_calls==1&&!external_upgrade_active()&&user_program_state()==USER_PROGRAM_READY);
    CHECK(user_program_valid()&&robot_ui_state()==ROBOT_UI_STANDBY);
    puts("PASS custom display, PC4 toggle and 1500ms shutdown cancels upgrade before power off");
}
static void errors_and_expiry(void)
{
    unsigned i;
    setup();load("import klq\nklq.missing()\n");rpc(CMD_RUN,0,0,OK);for(i=0;i<5;i++)user_program_poll();
    CHECK(user_program_error()==2&&user_program_error_line()==2&&user_program_state()==USER_PROGRAM_READY);
    board_ms+=800;robot_ui_poll();open(44);close(44,OK);CHECK(user_program_error()==2&&user_program_valid());
    rpc(CMD_RUN,0,0,OK);for(i=0;i<5;i++)user_program_poll();CHECK(user_program_error()==2);
    rpc(CMD_STOP,0,0,OK);open(55);board_ms+=30000;external_ports_poll();
    CHECK(!external_upgrade_active()&&user_program_state()==USER_PROGRAM_READY&&user_program_valid());
    puts("PASS error retry and lease expiry do not resume old actions");
}
void USART1_IRQHandler(void);
static void motor_control(uint32_t token)
{
    uint8_t wire[56],data[20];unsigned i;
    memset(data,0,sizeof(data));data[0]=0;p32(data+1,token);p16(data+5,2);p16(data+7,2);rpc(CMD_EXT_OPEN,data,9,OK);
    p32(data,token);memcpy(data+4,"KLQ1",4);p32(data+8,7);p16(data+12,1);p16(data+14,0);p32(data+16,crc32_update(0,data+4,12));rpc(CMD_EXT_XFER,data,20,OK);
    board_ms+=50;for(i=0;i<20;i++)external_ports_poll();
    memset(wire,0,sizeof(wire));memcpy(wire,"KLQ1",4);p32(wire+4,7);p16(wire+8,0x8001);p16(wire+10,40);
    p16(wire+20,1);wire[22]=1;wire[23]=2;p16(wire+24,2);p16(wire+26,2);p16(wire+28,2);p32(wire+32,0x20000);p32(wire+36,9);p16(wire+40,132);p16(wire+42,18304);p32(wire+44,0x1234);
    p32(wire+12,crc32_update(crc32_update(0,wire,12),wire+16,40));
    for(i=0;i<56;i++){test_uart[0].STS=0xA0;test_uart[0].DATA=wire[i];USART1_IRQHandler();}test_uart[0].STS=0x80;external_ports_poll();
    p32(data+8,8);p16(data+12,0x301);p32(data+16,crc32_update(0,data+4,12));rpc(CMD_EXT_XFER,data,20,OK);
    CHECK(external_motor_control_active());
}
static void motor_stop_controls(void)
{
    setup();load("while True:\n    pass\n");motor_control(66);short_press();
    CHECK(!external_upgrade_active() && user_program_valid() && user_program_state()==USER_PROGRAM_READY);
    setup();load("while True:\n    pass\n");motor_control(67);rpc(CMD_STOP,0,0,OK);
    CHECK(!external_upgrade_active() && user_program_valid() && user_program_state()==USER_PROGRAM_READY);
    setup();load("while True:\n    pass\n");motor_control(68);reset_seen=1;protocol_poll();CHECK(!external_upgrade_active());
    puts("PASS motor control PC4/USB STOP/USB reset cancel session without running old RAM code");
}
static void voice_frame(uint8_t type,uint8_t cmd,uint8_t seq,const uint8_t *data,unsigned n)
{
    uint8_t f[30];uint16_t sum;unsigned i;f[0]=0xa5;f[1]=0xfc;p16(f+2,(uint16_t)n);f[4]=type;f[5]=cmd;f[6]=seq;sum=(uint16_t)(type+cmd+seq);
    for(i=0;i<n;i++){f[7+i]=data[i];sum=(uint16_t)(sum+data[i]);}p16(f+7+n,sum);f[9+n]=0xfb;
    for(i=0;i<n+10;i++)voice_link_test_rx(f[i]);voice_link_poll();
}
static void voice_ready(void)
{
    unsigned i;uint8_t v[4]={'v',0,2,0},wake[2]={0,0xb1};voice_link_status_t st;
    CHECK(voice_link_query_version());for(i=0;i<20;i++)voice_link_poll();voice_frame(0xa2,0x94,1,v,4);voice_link_get_status(&st);CHECK(st.version_valid&&st.version[1]==2);
    voice_control_set_enabled(1);voice_frame(0xa3,0x9a,2,wake,2);voice_control_poll();
}
static void voice_say(uint32_t semantic,uint8_t seq)
{uint8_t data[7]={0,0,0,0,0,0,80};p32(data,semantic);voice_frame(0xa0,0x91,seq,data,7);voice_control_poll();}
static void voice_key_latch(void)
{
    uint8_t f[56];unsigned i;
    setup();load("while True:\n    pass\n");voice_ready();external_ports_poll();
    memset(f,0,sizeof(f));memcpy(f,"KLQ1",4);p32(f+4,1);p16(f+8,0x8001);p16(f+10,40);p16(f+20,1);f[22]=1;f[23]=2;p16(f+24,2);p16(f+26,2);p16(f+28,2);p32(f+32,0x20000);p32(f+36,9);p16(f+40,132);p16(f+42,18304);p32(f+44,0x1234);p32(f+12,crc32_update(crc32_update(0,f,12),f+16,40));
    for(i=0;i<56;i++){test_uart[0].STS=0xa0;test_uart[0].DATA=f[i];USART1_IRQHandler();}test_uart[0].STS=0x80;external_ports_poll();CHECK(external_port_motor_ready(0));
    voice_say(0x4b510101UL,20);CHECK(voice_control_active());pc4=0;power_key_poll();board_ms+=25;power_key_poll();
    voice_control_cancel();CHECK(!voice_control_active());pc4=1;power_key_poll();board_ms+=25;power_key_poll();
    CHECK(user_program_state()==USER_PROGRAM_READY&&user_program_valid());
    puts("PASS PC4 locks stop intent at keydown; ended voice job cannot start old READY program at keyup");
}
static void voice_business_gates(void)
{
    voice_control_status_t st;
    setup();voice_ready();voice_say(0x4b510101UL,3);voice_control_get_status(&st);CHECK(!st.active&&!st.accepted&&st.last_reason==VOICE_CTL_NO_MOTOR);
    setup();load("while True:\n    pass\n");voice_ready();rpc(CMD_RUN,0,0,OK);voice_control_poll();
    CHECK(user_program_valid()&&user_program_state()==USER_PROGRAM_RUNNING);voice_say(0x4b510103UL,4);voice_control_get_status(&st);
    CHECK(st.last_reason==VOICE_CTL_BUSY&&!st.active&&!st.accepted&&user_program_state()==USER_PROGRAM_RUNNING);
    setup();voice_ready();CHECK(user_program_begin(20,0));voice_control_poll();voice_say(0x4b510101UL,5);voice_control_get_status(&st);CHECK(st.last_reason==VOICE_CTL_BUSY&&!st.active&&user_program_state()==USER_PROGRAM_DOWNLOADING);
    setup();voice_ready();open(91);voice_control_poll();voice_say(0x4b510102UL,6);voice_control_get_status(&st);CHECK(st.last_reason==VOICE_CTL_BUSY&&!st.active&&external_upgrade_active());
    puts("PASS actual UART4 parser/policy/user-program gates: no motor, RUNNING STOP rejected, DOWNLOADING and exclusive rejected");
}
int main(void){ownership();resets();power_and_display();errors_and_expiry();motor_stop_controls();voice_business_gates();voice_key_latch();printf("Business/bridge coexistence: %u assertions passed\n",checks);return 0;}
