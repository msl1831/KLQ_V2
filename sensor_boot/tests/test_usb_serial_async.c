#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "usb_serial.h"
static unsigned checks,irq_depth,packets,bytes,clears,stalls,ctrl_n,delays,pending_in;
static uint8_t *rx_packet;static uint8_t output[8192];
volatile uint32_t board_ms;
MockUSBDevice g_usbDev;
uint8_t g_deviceDescriptor,g_configDescriptor,*g_stringDescriptor[1];
#define CHECK(x) do {++checks;if(!(x)){printf("USB async failed %u: %s\n",__LINE__,#x);exit(1);}}while(0)
void __disable_irq(void){CHECK(!irq_depth);irq_depth=1;}
void __enable_irq(void){CHECK(irq_depth);irq_depth=0;}
void usb_disconnect(void){}
void delay_ms(uint32_t n){++delays;board_ms+=n;}
void USBD_OpenInEP(USBD_EPConfig_T *p){(void)p;}
void USBD_OpenOutEP(USBD_EPConfig_T *p){(void)p;}
void USBD_SetEPRxStatus(unsigned ep,unsigned n){(void)ep;(void)n;}
void USBD_SetEPTxStatus(unsigned ep,unsigned n){(void)ep;(void)n;}
void USBD_SetEPTxRxStatus(unsigned ep,unsigned a,unsigned b){(void)ep;(void)a;(void)b;++stalls;}
void USBD_ResetEPTxFlag(unsigned ep){(void)ep;++clears;pending_in=0;}
void USBD_RxData(unsigned ep,uint8_t *p,unsigned n){(void)ep;CHECK(n==64);rx_packet=p;}
void USBD_TxData(unsigned ep,uint8_t *p,unsigned n){CHECK(ep==1 && n && n<=63 && !pending_in && irq_depth);CHECK(bytes+n<=sizeof(output));memcpy(output+bytes,p,n);bytes+=n;++packets;pending_in=1;}
void USBD_CtrlOutData(uint8_t *p,unsigned n){(void)p;ctrl_n=n;}
void USBD_CtrlInData(uint8_t *p,unsigned n){(void)p;ctrl_n=n;}
void USBD_CtrlTxStatus(void){}
void USBD_InitParamStructInit(USBD_InitParam_T *p){memset(p,0,sizeof(*p));}
void USBD_Init(USBD_InitParam_T *p){p->resetHandler();}
#include "usb_serial_source.c"
static void dtr(unsigned on)
{USBD_DevReqData_T r;memset(&r,0,sizeof(r));r.byte.bRequest=0x22;r.byte.wValue[0]=(uint8_t)on;class_request(&r);}
static void ack(void){CHECK(pending_in);pending_in=0;in_callback(1);}
static void drain(void){unsigned i=0;while(usb_serial_tx_state()==USB_TX_BUSY){CHECK(i++<32);if(pending_in)ack();usb_serial_poll();}}
int main(void)
{
    uint8_t data[1048],line[160];unsigned i,before;USBD_DevReqData_T r;
    memset(data,0xa7,sizeof(data));memset(line,'x',sizeof(line));line[159]='\n';
    usb_serial_init();CHECK(!usb_serial_console_active() && usb_serial_reset_seen());
    g_usbDev.curConfiguration=1;config_callback();CHECK(!usb_serial_console_active());
    before=delays;CHECK(!usb_serial_log(line,sizeof(line)) && delays==before && !packets);
    CHECK(usb_serial_write(data,20));drain();CHECK(usb_serial_tx_state()==USB_TX_DONE && bytes==20 && !memcmp(output,data,20));
    CHECK(!usb_serial_console_active() && !usb_serial_log_ready() && !usb_serial_log(line,sizeof(line)));
    bytes=packets=0;
    dtr(1);CHECK(usb_serial_console_active() && usb_serial_log_ready());
    CHECK(usb_serial_write(data,sizeof(data)) && usb_serial_tx_state()==USB_TX_BUSY && packets==1);
    for(i=0;i<100;i++)usb_serial_poll();CHECK(packets==1 && !usb_serial_log_ready() && delays==before);
    in_callback(2);CHECK(tx_busy);drain();CHECK(usb_serial_tx_state()==USB_TX_DONE && packets==17 && bytes==sizeof(data) && !memcmp(output,data,sizeof(data)));
    before=packets;CHECK(!usb_serial_write(data,1049) && !usb_serial_write(data,0) && packets==before);
    bytes=packets=0;CHECK(usb_serial_log(line,sizeof(line)));memset(line,0,sizeof(line));drain();
    CHECK(bytes==160 && packets==3 && output[0]=='x' && output[158]=='x' && output[159]=='\n');
    CHECK(!usb_serial_log(data,161));
    board_ms=0xfffffff0u;CHECK(usb_serial_write(data,20));board_ms+=249;usb_serial_poll();CHECK(usb_serial_tx_state()==USB_TX_BUSY);
    board_ms++;usb_serial_poll();CHECK(usb_serial_tx_state()==USB_TX_FAILED && !pending_in && clears && !usb_serial_console_active());
    CHECK(!usb_serial_log_ready() && !usb_serial_log(line,sizeof(line)));
    CHECK(usb_serial_write(data,20));ack();CHECK(usb_serial_tx_state()==USB_TX_DONE);board_ms+=1000;usb_serial_poll();CHECK(usb_serial_tx_state()==USB_TX_DONE);
    CHECK(!usb_serial_console_active());dtr(1);CHECK(usb_serial_console_active());
    CHECK(usb_serial_write(data,100));dtr(0);CHECK(usb_serial_tx_state()==USB_TX_FAILED && !pending_in && !usb_serial_console_active() && usb_serial_reset_seen());
    CHECK(usb_serial_write(data,20));drain();CHECK(usb_serial_tx_state()==USB_TX_DONE && !usb_serial_console_active() && !usb_serial_log(line,sizeof(line)));
    dtr(1);CHECK(usb_serial_write(data,20));ack();dtr(0);CHECK(usb_serial_tx_state()==USB_TX_DONE && usb_serial_reset_seen());
    dtr(1);CHECK(usb_serial_write(data,100));pending_in=0;reset_callback();CHECK(usb_serial_tx_state()==USB_TX_FAILED && !usb_serial_console_active() && usb_serial_reset_seen());
    g_usbDev.curConfiguration=1;config_callback();dtr(1);usb_serial_reset_seen();
    memset(rx_packet,0x56,64);g_usbDev.outBuf[1].xferCnt=64;out_callback(1);CHECK(usb_serial_read()==0x56);for(i=1;i<64;i++)CHECK(usb_serial_read()==0x56);CHECK(usb_serial_read()==-1);
    for(i=0;i<64;i++)out_callback(1);CHECK(usb_serial_reset_seen() && usb_serial_read()==-1);
    memset(&r,0,sizeof(r));r.byte.bRequest=0x21;r.byte.wLength[0]=255;r.byte.wLength[1]=255;r.byte.bmRequestType.bit.dir=1;class_request(&r);CHECK(ctrl_n==7);
    r.byte.wIndex[0]=1;class_request(&r);CHECK(stalls==1);
    g_usbDev.curConfiguration=0;config_callback();CHECK(!usb_serial_console_active() && !usb_serial_log_ready());
    CHECK(!irq_depth);printf("USB serial async: %u assertions passed\n",checks);return 0;
}
