#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "protocol.h"
#include "usb_serial.h"
static unsigned checks,jumps,resets,requests,fail_tx;
static uint8_t input[1080],sent[1080];static uint32_t input_n,input_pos,sent_n;
volatile uint32_t board_ms,usb_reset_count;
uint8_t mock_app[32],mock_meta[32];
#define CHECK(x) do {++checks;if(!(x)){printf("USB boot failed %u: %s\n",__LINE__,#x);exit(1);}}while(0)
int usb_serial_read(void){return input_pos<input_n?input[input_pos++]:-1;}
bool usb_serial_write(const uint8_t *p,uint32_t n){if(fail_tx){--fail_tx;return false;}memcpy(sent,p,n);sent_n=n;return true;}
void usb_serial_poll(void){}
uint8_t usb_serial_tx_state(void){return USB_TX_DONE;}
bool usb_serial_reset_seen(void){return false;}
void delay_ms(uint32_t n){board_ms+=n;}
void display_icon(unsigned n){(void)n;}
void display_columns(const uint8_t *p){(void)p;}
void usb_disconnect(void){}
void board_request_bootloader(void){++requests;}
void board_jump(uint32_t a){CHECK(a==APP_BASE);++jumps;}
void NVIC_SystemReset(void){++resets;}
void FMC_Unlock(void){}void FMC_Lock(void){}void FMC_ClearStatusFlag(unsigned n){(void)n;}
unsigned FMC_ErasePage(uint32_t a){memset((void*)a,0xff,FLASH_PAGE);return FMC_STATUS_COMPLETE;}
unsigned FMC_ProgramHalfWord(uint32_t a,uint16_t n){*(uint16_t*)a=n;return FMC_STATUS_COMPLETE;}
#include "protocol_source.c"
static uint32_t command(uint32_t seq,uint16_t cmd,const uint8_t *p,uint16_t n)
{input_pos=0;input_n=16+n;sent_n=0;put32(input,FRAME_MAGIC);put32(input+4,seq);put16(input+8,cmd);put16(input+10,n);if(n)memcpy(input+16,p,n);put32(input+12,crc32_update(crc32_update(0,input,12),input+16,n));protocol_poll();return sent_n?get32(sent+16):99;}
static void valid_image(void)
{Image *m=(Image*)mock_meta;memset(mock_app,0xa5,sizeof(mock_app));put32(mock_app,0x20001000);put32(mock_app+4,APP_BASE+9);m->magic=IMAGE_MAGIC;m->length=16;m->crc=crc32_update(0,mock_app,16);m->base=APP_BASE;m->header_crc=crc32_update(0,&m->length,12);}
int main(void)
{
    uint8_t p[8];valid_image();CHECK(protocol_system_firmware_valid() && valid_cache==2);
    mock_app[12]^=1;CHECK(command(1,CMD_INFO,0,0)==OK && strstr((char*)sent+20,"VALID=1"));
    CHECK(command(2,CMD_RUN,0,0)==ERR_IMAGE && !jumps && valid_cache==1);
    CHECK(command(3,CMD_INFO,0,0)==OK && strstr((char*)sent+20,"VALID=0"));
    valid_image();CHECK(protocol_system_firmware_valid());put32(p,16);put32(p+4,0);
    CHECK(command(4,CMD_BEGIN,p,8)==ERR_FLASH && valid_cache==1);
    valid_image();memset(mock_meta,0,sizeof(mock_meta));image_size=written=16;image_crc=crc32_update(0,mock_app,16);downloading=true;
    CHECK(command(5,CMD_END,0,0)==OK && valid_cache==2 && image_valid());
    fail_tx=1;CHECK(command(6,CMD_RUN,0,0)==99 && !jumps && action_pending==1);
    CHECK(command(6,CMD_RUN,0,0)==OK && jumps==1 && !action_pending);
    CHECK(command(6,CMD_RUN,0,0)==OK && jumps==1);
    fail_tx=1;CHECK(command(7,CMD_RESET,0,0)==99 && !requests && !resets);
    CHECK(command(7,CMD_RESET,0,0)==OK && requests==1 && resets==1);
    CHECK(command(7,CMD_RESET,0,0)==OK && requests==1 && resets==1);
    mock_app[12]^=1;CHECK(!protocol_system_firmware_valid() && valid_cache==1);
    printf("USB boot retry/cache: %u assertions passed\n",checks);return 0;
}
