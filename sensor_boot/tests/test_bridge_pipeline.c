#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include "board.h"
#include "protocol.h"
#include "usb_serial.h"
#include "user_program.h"
#include "robot_ui.h"
#include "external_ports.h"
#include "apm32e10x.h"
#include "apm32e10x_usart.h"

volatile uint32_t board_ms,usb_reset_count;
uint8_t mock_app[32],mock_meta[32];
USART_T test_uart[3];
static uint8_t usb_input[1044],wire[148],wire_used,selected;
static uint16_t usb_size,usb_pos,wire_size;
static unsigned stopped;
static uint8_t reset_seen;

static void send_record(uint8_t type,const uint8_t *data,uint16_t n)
{
    uint8_t header[3];
    header[0]=type;header[1]=(uint8_t)n;header[2]=(uint8_t)(n>>8);
    fwrite(header,1,3,stdout);
    if(n)fwrite(data,1,n,stdout);
    fflush(stdout);
}
int usb_serial_read(void) { return usb_pos<usb_size ? usb_input[usb_pos++] : -1; }
bool usb_serial_write(const uint8_t *data,uint32_t n)
{ if(n>1048U)exit(20);send_record('B',data,(uint16_t)n);return true; }
bool usb_serial_reset_seen(void) { uint8_t value=reset_seen;reset_seen=0U;return value; }
void delay_ms(uint32_t n) { board_ms+=n; }
void display_icon(unsigned icon) { (void)icon; }
void display_columns(const uint8_t *p) { (void)p; }
void usb_disconnect(void) { exit(21); }
void board_request_bootloader(void) { exit(22); }
void board_jump(uint32_t address) { (void)address;exit(23); }
void NVIC_SystemReset(void) { exit(24); }
void user_program_init(void) {}
bool user_program_begin(uint32_t n,uint32_t c) { (void)n;(void)c;return false; }
bool user_program_write(uint32_t o,const uint8_t *p,uint32_t n) { (void)o;(void)p;(void)n;return false; }
bool user_program_finish_download(void) { return false; }
bool user_program_start(void) { return false; }
void user_program_stop(void) { ++stopped; }
void user_program_finish_execution(void) {}
bool user_program_set_display(const uint8_t *p) { (void)p;return false; }
user_program_state_t user_program_state(void) { return USER_PROGRAM_READY; }
bool user_program_valid(void) { return false; }
uint32_t user_program_length(void) { return 0U; }
uint32_t user_program_received(void) { return 0U; }
uint8_t user_program_error(void) { return 0U; }
uint16_t user_program_error_line(void) { return 0U; }
uint8_t power_key_raw_level(void) { return 1U; }
uint8_t power_key_pressed(void) { return 0U; }
uint16_t power_key_short_count(void) { return 0U; }
robot_ui_state_t robot_ui_state(void) { return ROBOT_UI_STANDBY; }

/* Runner copies both production sources unchanged so hardware headers can be mocked. */
#include "protocol_source.c"
#include "external_ports_source.c"

static void uart_reply(void)
{
    uint8_t header[3],data[148];
    uint16_t n,i;
    send_record('U',wire,wire_size);
    if(fread(header,1,3,stdin)!=3U || header[0]!='R')exit(25);
    n=(uint16_t)header[1]|((uint16_t)header[2]<<8);
    if(n>sizeof(data) || fread(data,1,n,stdin)!=n)exit(26);
    for(i=0U;i<n;++i) {
        test_uart[selected].DATA=data[i];test_uart[selected].STS=USART_FLAG_RXBNE;
        if(selected==0U)USART1_IRQHandler();
        else if(selected==1U)USART2_IRQHandler();
        else USART3_IRQHandler();
    }
    test_uart[selected].DATA=0x100U;test_uart[selected].STS=0U;
    wire_used=0U;wire_size=0U;
}

static void capture(uint8_t value)
{
    static const uint8_t magic[4]={'K','L','Q','1'};
    if(wire_used<4U && value!=magic[wire_used]) {
        wire_used=value=='K'?1U:0U;if(wire_used)wire[0]='K';return;
    }
    if(wire_used>=sizeof(wire))exit(27);
    wire[wire_used++]=value;
    if(wire_used==16U) {
        wire_size=(uint16_t)(16U+wire[10]+((uint16_t)wire[11]<<8));
        if(wire_size>sizeof(wire))exit(28);
    }
    if(wire_size && wire_used==wire_size)uart_reply();
}

static void pump(unsigned milliseconds)
{
    unsigned ms,round,i;
    for(ms=0U;ms<milliseconds;++ms) {
        ++board_ms;
        for(round=0U;round<20U;++round) {
            for(i=0U;i<3U;++i)test_uart[i].DATA=0x100U;
            external_ports_poll();
            if(external_upgrade_active() && test_uart[selected].DATA<256U)
                capture((uint8_t)test_uart[selected].DATA);
        }
    }
}

int main(int argc,char **argv)
{
    uint8_t header[3];
    uint16_t n;
    unsigned i;
    if(argc!=2)return 30;
    selected=(uint8_t)atoi(argv[1]);if(selected>2U)return 31;
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
    memset(test_uart,0,sizeof(test_uart));
    for(i=0U;i<3U;++i)test_uart[i].STS_B.TXBEFLG=1U;
    external_ports_init();
    while(fread(header,1,3,stdin)==3U) {
        n=(uint16_t)header[1]|((uint16_t)header[2]<<8);
        if(header[0]=='W') {
            if(n>sizeof(usb_input) || fread(usb_input,1,n,stdin)!=n)return 32;
            usb_size=n;usb_pos=0U;
            do { protocol_poll(); } while(usb_pos<usb_size);
            pump(10U);
        } else if(header[0]=='T')pump(n);
        else if(header[0]=='Z') { reset_seen=1U;++usb_reset_count;protocol_poll();pump(1U); }
        else return 33;
        send_record('D',0,0U);
    }
    return 0;
}
