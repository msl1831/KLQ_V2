#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "board.h"
#include "protocol.h"
#include "usb_serial.h"
#include "ble_serial.h"
static uint8_t rx[2][4096],tx[2][4096],ts[2]={USB_TX_DONE,USB_TX_DONE},rst[2],hold[2],fail[2];
static uint32_t rn[2],rp[2],tn[2],hn[2];static const uint8_t *hp[2];
static unsigned checks,erases,programs,jumps,resets,requests,fail_program;static uint32_t first_erase;
volatile uint32_t board_ms,usb_reset_count;
#define CHECK(x) do{++checks;if(!(x)){printf("Dual BOOT failed %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static int read_ch(unsigned c){return rp[c]<rn[c]?rx[c][rp[c]++]:-1;}
static bool write_ch(unsigned c,const uint8_t *p,uint32_t n){if(fail[c]){--fail[c];ts[c]=USB_TX_FAILED;return false;}if(hold[c]){hp[c]=p;hn[c]=n;ts[c]=USB_TX_BUSY;return true;}CHECK(n<=sizeof(tx[c])-tn[c]);memcpy(tx[c]+tn[c],p,n);tn[c]+=n;ts[c]=USB_TX_DONE;return true;}
int usb_serial_read(void){return read_ch(0);}int ble_serial_read(void){return read_ch(1);}
bool usb_serial_write(const uint8_t *p,uint32_t n){return write_ch(0,p,n);}bool ble_serial_write(const uint8_t *p,uint32_t n){return write_ch(1,p,n);}
void usb_serial_poll(void){}void ble_serial_poll(void){}
uint8_t usb_serial_tx_state(void){return ts[0];}uint8_t ble_serial_tx_state(void){return ts[1];}
bool usb_serial_reset_seen(void){uint8_t r=rst[0];rst[0]=0;if(r&&ts[0]==USB_TX_BUSY)ts[0]=USB_TX_FAILED;return r;}bool ble_serial_reset_seen(void){uint8_t r=rst[1];rst[1]=0;if(r&&ts[1]==USB_TX_BUSY)ts[1]=USB_TX_FAILED;return r;}
bool ble_serial_ready(void){return true;}bool ble_serial_connected(void){return true;}uint16_t ble_serial_status(uint8_t *p){memset(p,0,88);p[0]=p[1]=p[2]=1;return 88;}
void delay_ms(uint32_t n){board_ms+=n;}void display_icon(unsigned n){(void)n;}void display_columns(const uint8_t *p){(void)p;}void usb_disconnect(void){}
void board_request_bootloader(void){++requests;}void board_jump(uint32_t p){CHECK(p==APP_BASE);++jumps;}void NVIC_SystemReset(void){++resets;}
void FMC_Unlock(void){}void FMC_Lock(void){}void FMC_ClearStatusFlag(unsigned n){(void)n;}
unsigned FMC_ErasePage(uint32_t p){CHECK(p>=APP_BASE&&p<=META_BASE&&!(p%FLASH_PAGE));if(!erases)first_erase=p;++erases;memset((void*)p,0xff,FLASH_PAGE);return FMC_STATUS_COMPLETE;}
unsigned FMC_ProgramHalfWord(uint32_t p,uint16_t v){CHECK(p>=APP_BASE&&p<META_BASE+20&&!(p&1));++programs;if(fail_program){--fail_program;return 1u;}*(uint16_t*)p=v;return FMC_STATUS_COMPLETE;}
#include "protocol_source.c"
static void fresh(void){memset((void*)0x08000000,0xff,0x80000);memset(links,0,sizeof(links));memset(rn,0,sizeof(rn));memset(rp,0,sizeof(rp));memset(tn,0,sizeof(tn));memset(rst,0,sizeof(rst));memset(hold,0,sizeof(hold));memset(fail,0,sizeof(fail));ts[0]=ts[1]=USB_TX_DONE;erases=programs=jumps=resets=requests=fail_program=0;valid_cache=download_owner=downloading=0;image_size=image_crc=written=0;board_ms=100;}
static void frame(unsigned c,uint32_t seq,uint16_t cmd,const uint8_t *p,uint16_t n){rn[c]=16+n;rp[c]=tn[c]=0;put32(rx[c],FRAME_MAGIC);put32(rx[c]+4,seq);put16(rx[c]+8,cmd);put16(rx[c]+10,n);if(n)memcpy(rx[c]+16,p,n);put32(rx[c]+12,crc32_update(crc32_update(0,rx[c],12),rx[c]+16,n));}
static void drain(unsigned c){unsigned i=0;do{protocol_poll();CHECK(++i<24);}while(rp[c]<rn[c]);}
static void command(unsigned c,uint32_t seq,uint16_t cmd,const uint8_t *p,uint16_t n,uint32_t status,uint16_t out){frame(c,seq,cmd,p,n);drain(c);CHECK(tn[c]==20u+out);CHECK(get32(tx[c]+4)==seq&&get16(tx[c]+8)==(cmd|0x8000u));CHECK(get32(tx[c]+16)==status&&get16(tx[c]+10)==out+4);CHECK(get32(tx[c]+12)==crc32_update(crc32_update(0,tx[c],12),tx[c]+16,out+4));}
static void image(uint8_t *p,unsigned n){memset(p,0xa5,n);put32(p,0x20001000);put32(p+4,APP_BASE+9);}
static void begin(unsigned c,uint32_t seq,uint8_t *data,unsigned n){uint8_t p[8];put32(p,n);put32(p+4,crc32_update(0,data,n));command(c,seq,CMD_BEGIN,p,8,OK,0);CHECK(download_owner==c+1&&downloading&&first_erase==META_BASE);}
static void complete(unsigned c,uint32_t seq,uint8_t *data,unsigned n){uint8_t p[80];put32(p,0);memcpy(p+4,data,n);command(c,seq,CMD_DATA,p,(uint16_t)(n+4),OK,4);CHECK(get32(tx[c]+20)==n);command(c,seq+1,CMD_END,0,0,OK,0);CHECK(image_valid()&&!download_owner&&!downloading);}
static void owners_and_crc(void)
{
 uint8_t data[64],p[80],saved[24];unsigned count;
 fresh();image(data,64);begin(0,1,data,64);count=erases;put32(p,64);put32(p+4,123);command(1,2,CMD_BEGIN,p,8,ERR_STATE,0);CHECK(erases==count&&download_owner==1);
 put32(p,0);memcpy(p+4,data,16);command(1,3,CMD_DATA,p,20,ERR_STATE,0);CHECK(!programs);command(0,4,CMD_DATA,p,20,OK,4);count=programs;memcpy(saved,tx[0],24);
 command(1,5,CMD_ECHO,data,64,OK,64);command(0,4,CMD_DATA,p,20,OK,4);CHECK(programs==count&&!memcmp(saved,tx[0],24));rst[1]=1;protocol_poll();CHECK(download_owner==1&&downloading);rst[0]=1;protocol_poll();CHECK(!download_owner&&!downloading&&!image_valid());command(0,6,CMD_DATA,p,20,ERR_STATE,0);
 begin(1,7,data,64);command(0,8,CMD_END,0,0,ERR_STATE,0);complete(1,9,data,64);rst[1]=1;protocol_poll();CHECK(image_valid());
 fresh();image(data,64);begin(1,10,data,64);put32(p,0);memcpy(p+4,data,64);p[24]^=1;command(1,11,CMD_DATA,p,68,OK,4);command(1,12,CMD_END,0,0,ERR_IMAGE,0);CHECK(((Image*)META_BASE)->magic==0xffffffffu&&!image_valid());
 fresh();image(data,64);data[4]=0;data[5]=data[6]=data[7]=0;begin(1,10,data,64);put32(p,0);memcpy(p+4,data,64);command(1,11,CMD_DATA,p,68,OK,4);command(1,12,CMD_END,0,0,ERR_IMAGE,0);CHECK(((Image*)META_BASE)->magic==0xffffffffu&&!image_valid());
 fresh();image(data,64);begin(1,50,data,64);put32(p,0);memcpy(p+4,data,16);fail_program=1;command(1,51,CMD_DATA,p,20,ERR_FLASH,0);CHECK(!download_owner&&!downloading);begin(0,52,data,64);
 puts("PASS dual BOOT writer ownership, first metadata invalidate, exact retry, disconnect and invalid image");
}
static void deferred_and_stop(void)
{
 uint8_t data[64],p[8];unsigned count;
 fresh();image(data,64);begin(0,20,data,64);complete(0,21,data,64);fail[1]=1;frame(1,23,CMD_RUN,0,0);drain(1);CHECK(!jumps&&links[1].action_pending==1);put32(p,64);put32(p+4,crc32_update(0,data,64));command(0,24,CMD_BEGIN,p,8,OK,0);CHECK(!links[1].action_pending&&!links[1].cached&&!image_valid());command(1,23,CMD_RUN,0,0,ERR_STATE,0);CHECK(!jumps);command(0,25,CMD_STOP,0,0,OK,0);command(1,26,CMD_RUN,0,0,ERR_IMAGE,0);CHECK(!jumps);
 fresh();image(data,64);begin(0,30,data,64);complete(0,31,data,64);count=erases;hold[1]=1;frame(1,33,CMD_RUN,0,0);drain(1);CHECK(links[1].action_pending==1&&!jumps);command(0,34,CMD_BEGIN,p,8,ERR_STATE,0);CHECK(erases==count);command(0,35,CMD_STOP,0,0,OK,0);CHECK(!links[1].action_pending);hold[1]=0;ts[1]=USB_TX_DONE;protocol_poll();CHECK(!jumps);command(1,36,CMD_RUN,0,0,OK,0);CHECK(jumps==1);
 fresh();image(data,64);begin(0,40,data,64);hold[1]=1;frame(1,41,CMD_ECHO,data,64);drain(1);frame(1,42,CMD_STOP,0,0);drain(1);CHECK(!downloading&&!download_owner&&links[1].urgent_pending&&!image_valid());memcpy(tx[1],hp[1],hn[1]);tn[1]=hn[1];hold[1]=0;ts[1]=USB_TX_DONE;protocol_poll();CHECK(!links[1].urgent_pending&&get32(tx[1]+hn[1]+4)==42&&get32(tx[1]+hn[1]+16)==OK);
 puts("PASS uncommitted RUN cannot jump replaced image; pending control serializes cross-link mutation; STOP is global");
}
int main(void)
{void *m=VirtualAlloc((void*)0x08000000,0x80000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);CHECK(m==(void*)0x08000000);owners_and_crc();deferred_and_stop();CHECK(VirtualFree(m,0,MEM_RELEASE));printf("Dual BOOT protocol: %u assertions PASS\n",checks);return 0;}
