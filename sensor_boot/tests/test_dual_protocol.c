#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "protocol.h"
#include "usb_serial.h"
#include "ble_serial.h"
static uint8_t bi[8192],bo[8192],breset,bs=BLE_TX_DONE,bfail,bhold;
static uint32_t bn,bp,bon,bheld_n;
static const uint8_t *bheld;
static uint8_t bcopy[1048];
int ble_serial_read(void){return bp<bn?bi[bp++]:-1;}
bool ble_serial_write(const uint8_t *p,uint32_t n){if(bfail){--bfail;bs=BLE_TX_FAILED;return false;}if(bhold){bheld=p;bheld_n=n;memcpy(bcopy,p,n);bs=BLE_TX_BUSY;return true;}memcpy(bo+bon,p,n);bon+=n;bs=BLE_TX_DONE;return true;}
void ble_serial_poll(void){}
uint8_t ble_serial_tx_state(void){return bs;}
bool ble_serial_reset_seen(void){uint8_t r=breset;breset=0;if(r&&bs==BLE_TX_BUSY)bs=BLE_TX_FAILED;return r;}
bool ble_serial_ready(void){return true;}
bool ble_serial_connected(void){return true;}
uint16_t ble_serial_status(uint8_t *out){memset(out,0,88);out[0]=out[1]=out[2]=out[3]=1;memcpy(out+36,"KLQ2",5);return 88;}
#define main usb_baseline_main
#include "test_usb_bridge.c"
#undef main
static void fresh(void){setup();bn=bp=bon=0;breset=bfail=bhold=0;bs=BLE_TX_DONE;bheld=0;bheld_n=0;}
static void bframe(uint32_t seq,uint16_t cmd,const uint8_t *p,uint16_t n)
{bn=16u+n;bp=bon=0;memcpy(bi,"KLQ1",4);wr32(bi+4,seq);wr16(bi+8,cmd);wr16(bi+10,n);if(n)memcpy(bi+16,p,n);wr32(bi+12,crc32_update(crc32_update(0,bi,12),bi+16,n));}
static void drain(void){unsigned i=0;do{protocol_poll();CHECK(++i<20);}while(bp<bn);}
static const uint8_t *bexpect(uint32_t seq,uint16_t cmd,uint32_t status,uint16_t n)
{CHECK(bon==20u+n);CHECK(rd32(bo+4)==seq&&rd16(bo+8)==(uint16_t)(cmd|0x8000u));CHECK(rd16(bo+10)==n+4u&&rd32(bo+16)==status);CHECK(rd32(bo+12)==crc32_update(crc32_update(0,bo,12),bo+16,n+4));return bo+20;}
static const uint8_t *bcmd(uint32_t seq,uint16_t cmd,const uint8_t *p,uint16_t n,uint32_t status,uint16_t out)
{bframe(seq,cmd,p,n);drain();return bexpect(seq,cmd,status,out);}
static void start_ble(uint32_t seq)
{uint8_t p[8];wr32(p,64);wr32(p+4,0x1234);bcmd(seq,CMD_BEGIN,p,8,OK,0);user_state=USER_PROGRAM_DOWNLOADING;CHECK(download_owner==2);}
static void fragment_and_owner(void)
{
 uint8_t p[20],d[12],saved[24];uint32_t total;unsigned before;
 fresh();memset(p,0xa5,sizeof(p));bframe(200,CMD_ECHO,p,20);total=bn;bn=8;protocol_poll();CHECK(links[1].used==8&&bon==0);
 command(200,CMD_ECHO,(uint8_t*)"USB",3,OK,3);CHECK(links[1].used==8);bn=total;drain();CHECK(!memcmp(bexpect(200,CMD_ECHO,OK,20),p,20));
 fresh();start_ble(201);before=begin_calls;command(201,CMD_BEGIN,p,8,ERR_STATE,0);CHECK(begin_calls==before&&download_owner==2);
 command(202,CMD_DATA,p,5,ERR_STATE,0);command(203,CMD_END,0,0,ERR_STATE,0);CHECK(data_calls==0&&end_calls==0);
 wr32(d,0);memset(d+4,0x6a,8);bcmd(202,CMD_DATA,d,12,OK,4);CHECK(data_calls==1);memcpy(saved,bo,24);
 command(204,CMD_ECHO,(uint8_t*)"probe",5,OK,5);bcmd(202,CMD_DATA,d,12,OK,4);CHECK(data_calls==1&&!memcmp(saved,bo,24));
 reset_seen=1;protocol_poll();CHECK(download_owner==2&&user_state==USER_PROGRAM_DOWNLOADING&&!stop_calls);
 finish_incomplete=1;bcmd(2049,CMD_END,0,0,ERR_IMAGE,0);CHECK(download_owner==2);finish_incomplete=0;
 bcmd(205,CMD_END,0,0,OK,0);CHECK(!download_owner&&end_calls==2);user_state=USER_PROGRAM_READY;breset=1;protocol_poll();CHECK(user_state==USER_PROGRAM_READY&&!stop_calls);
 start_ble(206);breset=1;protocol_poll();CHECK(!download_owner&&stop_calls==1);
 fresh();wr32(p,64);wr32(p+4,1);command(207,CMD_BEGIN,p,8,OK,0);user_state=USER_PROGRAM_DOWNLOADING;CHECK(download_owner==1);breset=1;protocol_poll();CHECK(download_owner==1&&!stop_calls);bcmd(208,CMD_STOP,0,0,OK,0);CHECK(!download_owner&&stop_calls==1);
 puts("PASS independent framing/cache, download ownership and owner-only disconnect cancellation");
}
static void engineering_and_busy(void)
{
 uint8_t p[12],saved[1048];unsigned before;
 fresh();open_session();before=cancel_calls;breset=1;protocol_poll();CHECK(owner&&cancel_calls==before);
 memset(p,0,sizeof(p));bcmd(301,CMD_EXT_OPEN,p,9,ERR_COMMAND,0);CHECK(owner&&open_calls==1);bcmd(302,CMD_VOICE_ARM,p,1,ERR_COMMAND,0);CHECK(owner);
 reset_seen=1;protocol_poll();CHECK(!owner&&cancel_calls==before+1);
 fresh();bhold=1;bframe(303,CMD_ECHO,(uint8_t*)"HELLO",5);drain();CHECK(links[1].reply_pending&&links[1].tx_owner&&bheld_n==25);memcpy(saved,bcopy,bheld_n);
 command(304,CMD_ECHO,(uint8_t*)"USB-OK",6,OK,6);CHECK(!memcmp(saved,bheld,bheld_n));
 command(305,CMD_STOP,0,0,OK,0);CHECK(stop_calls==1&&bs==BLE_TX_BUSY);
 bframe(306,CMD_STOP,0,0);drain();CHECK(stop_calls==2&&links[1].urgent_pending);memcpy(bo,bheld,bheld_n);bon=bheld_n;bhold=0;bs=BLE_TX_DONE;protocol_poll();CHECK(bon==45&&!links[1].reply_pending&&!links[1].urgent_pending&&rd32(bo+29)==306&&rd32(bo+41)==OK);
 fresh();bhold=1;bframe(307,CMD_ECHO,(uint8_t*)"FRAME",5);drain();bframe(308,CMD_STOP,0,0);drain();CHECK(links[1].urgent_pending&&bs==BLE_TX_BUSY);
 breset=1;protocol_poll();CHECK(bs==BLE_TX_FAILED&&!links[1].tx_owner&&!links[1].urgent_pending&&!links[1].reply_pending);
 bhold=0;bcmd(309,CMD_ECHO,(uint8_t*)"NEW-EPOCH",9,OK,9);CHECK(!links[1].urgent_pending);
 puts("PASS BLE engineering controls unavailable; slow BLE replies preserve USB/physical safety service");
}
static void deferred_and_status(void)
{
 uint8_t p[8];const uint8_t *q;unsigned before;
 fresh();bhold=1;bframe(400,CMD_RESET,0,0);drain();CHECK(links[1].action_pending==2&&links[1].tx_owner==1);bs=BLE_TX_DONE;breset=1;protocol_poll();CHECK(!links[1].action_pending&&!links[1].tx_owner&&!boot_requests&&!reset_calls);
 fresh();bfail=1;bframe(401,CMD_RESET,0,0);drain();CHECK(!bon&&!boot_requests&&!reset_calls&&links[1].action_pending==2);before=stop_calls;
 command(402,CMD_ECHO,(uint8_t*)"monitor",7,OK,7);CHECK(links[1].action_pending==2);bcmd(401,CMD_RESET,0,0,OK,0);CHECK(boot_requests==1&&reset_calls==1&&stop_calls==before);bcmd(401,CMD_RESET,0,0,OK,0);CHECK(reset_calls==1);
 fresh();bhold=1;bframe(403,CMD_RESET,0,0);drain();CHECK(links[1].action_pending==2);wr32(p,64);wr32(p+4,123);command(404,CMD_BEGIN,p,8,ERR_STATE,0);CHECK(!begin_calls);bs=BLE_TX_FAILED;bhold=0;protocol_poll();CHECK(links[1].action_pending==2);
 command(405,CMD_BEGIN,p,8,OK,0);user_state=USER_PROGRAM_DOWNLOADING;CHECK(download_owner==1&&!links[1].cached&&!links[1].action_pending);bcmd(403,CMD_RESET,0,0,ERR_STATE,0);CHECK(!reset_calls&&!boot_requests);
 fresh();q=bcmd(406,CMD_BLE_STATUS,0,0,OK,88);CHECK(q[0]==1&&!strcmp((char*)q+36,"KLQ2"));q=command(407,CMD_BLE_STATUS,0,0,OK,88);CHECK(q[0]==1);bcmd(408,CMD_BLE_STATUS,p,1,ERR_LENGTH,0);
 fresh();bframe(409,CMD_BEGIN,p,8);bi[12]^=1;drain();bexpect(409,CMD_BEGIN,ERR_CRC,0);CHECK(!download_owner&&!begin_calls);
 puts("PASS deferred control exactly once, cross-link mutation barrier, status and CRC protection");
}
int main(void)
{unsigned base;usb_baseline_main();base=checks;fragment_and_owner();engineering_and_busy();deferred_and_status();printf("Dual APP protocol: %u additional assertions, total %u PASS\n",checks-base,checks);return 0;}
