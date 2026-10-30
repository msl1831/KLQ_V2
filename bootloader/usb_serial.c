/* KLQ board adapter for the Geehy USB device library. */
#include "board.h"
#include "usb_serial.h"
#include "usbd_init.h"
#include "usbd_descriptor.h"
#include <string.h>

#define RX_SIZE 4096u
static uint8_t rx[RX_SIZE], packet[64], tx[64];
static volatile uint32_t head, tail;
static volatile uint32_t usb_epoch;
static volatile bool configured, opened, tx_busy, reset_seen;
static volatile uint8_t tx_state;
static const uint8_t *tx_data;
static uint32_t tx_left, tx_start, tx_epoch;
static bool log_stalled;
static uint8_t log_line[160];
volatile uint32_t usb_reset_count;
static uint8_t line_coding[8] = {0x00,0xc2,0x01,0,0,0,8,0};

void USBD_HardWareInit(void)
{
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_GPIOA);
    /* Both bus pins remain low while the controller is initialized. */
    RCM_ConfigUSBCLK(RCM_USB_DIV_1_5);
    RCM_EnableAPB1PeriphClock(RCM_APB1_PERIPH_USB);
    RCM_EnableAPB1PeriphReset(RCM_APB1_PERIPH_USB);
    RCM_DisableAPB1PeriphReset(RCM_APB1_PERIPH_USB);
    USBD_SetRegCTRL(1); /* Power analog block, retain forced digital reset. */
    delay_ms(2);
    NVIC_SetPriority(USBD1_LP_CAN1_RX0_IRQn, 2);
    NVIC_EnableIRQ(USBD1_LP_CAN1_RX0_IRQn);
}

static void reset_callback(void)
{
    USBD_EPConfig_T ep;
    ++usb_epoch;
    memset(&ep, 0, sizeof(ep));
    head = tail = 0;
    configured = opened = tx_busy = false;
    if (tx_state!=USB_TX_DONE) tx_state=USB_TX_FAILED; log_stalled=false;
    reset_seen = true;
    ++usb_reset_count;
    ep.epNum = USBD_EP_1;
    ep.epType = USBD_EP_TYPE_BULK;
    ep.epBufAddr = USB_EP1_TX_ADDR;
    ep.maxPackSize = 64;
    ep.epStatus = USBD_EP_STATUS_NAK;
    USBD_OpenInEP(&ep);
    ep.epBufAddr = USB_EP1_RX_ADDR;
    USBD_OpenOutEP(&ep);
    ep.epNum = USBD_EP_2;
    ep.epType = USBD_EP_TYPE_INTERRUPT;
    ep.epBufAddr = USB_EP2_TX_ADDR;
    ep.maxPackSize = 8;
    USBD_OpenInEP(&ep);
}

static void config_callback(void)
{
    ++usb_epoch;
    configured = g_usbDev.curConfiguration == 1;
    USBD_SetEPTxStatus(USBD_EP_1,USBD_EP_STATUS_NAK);
    USBD_ResetEPTxFlag(USBD_EP_1);
    head = tail = 0;
    tx_busy = opened = false;
    if (tx_state!=USB_TX_DONE) tx_state=USB_TX_FAILED; log_stalled=false;
    reset_seen = true;
    if (configured) USBD_RxData(USBD_EP_1, packet, sizeof(packet));
    else {
        USBD_SetEPRxStatus(USBD_EP_1, USBD_EP_STATUS_NAK);
        USBD_SetEPTxStatus(USBD_EP_1, USBD_EP_STATUS_NAK);
    }
}

static void out_callback(uint8_t ep)
{
    uint32_t i, n;
    if (ep != USBD_EP_1) return;
    n = g_usbDev.outBuf[ep].xferCnt;
    if (n <= 64 && head - tail + n < RX_SIZE) {
        for (i = 0; i < n; ++i) rx[(head + i) & (RX_SIZE-1)] = packet[i];
        __DMB();
        head += n;
    } else { ++usb_epoch; head = tail = 0; reset_seen = true; }
    USBD_RxData(USBD_EP_1, packet, sizeof(packet));
}
static void in_callback(uint8_t ep)
{
    if (ep==USBD_EP_1) {
        tx_busy=false;
        if (tx_state==USB_TX_BUSY && !tx_left) tx_state=USB_TX_DONE;
    }
}

static void class_request(USBD_DevReqData_T *r)
{
    unsigned length = r->byte.wLength[0] | (r->byte.wLength[1] << 8);
    /* Limit line-coding transfers and reject unsupported class requests. */
    if (r->byte.wIndex[0] == 0 && r->byte.wIndex[1] == 0) {
        if (r->byte.bRequest == 0x20 && length == 7 && !r->byte.bmRequestType.bit.dir) {
            USBD_CtrlOutData(line_coding, 7); return;
        }
        if (r->byte.bRequest == 0x21 && length && r->byte.bmRequestType.bit.dir) {
            USBD_CtrlInData(line_coding, length < 7 ? length : 7); return;
        }
        if ((r->byte.bRequest == 0x22 || r->byte.bRequest == 0x23) && !length && !r->byte.bmRequestType.bit.dir) {
            if (r->byte.bRequest==0x22) {
                bool next=(r->byte.wValue[0]&1u)!=0u;
                if (opened && !next) {
                    ++usb_epoch; head=tail=0; reset_seen=true;
                    USBD_SetEPTxStatus(USBD_EP_1,USBD_EP_STATUS_NAK);
                    USBD_ResetEPTxFlag(USBD_EP_1);
                    tx_busy=false; if (tx_state!=USB_TX_DONE) tx_state=USB_TX_FAILED;
                }
                opened=next; log_stalled=false;
            }
            USBD_CtrlTxStatus(); return;
        }
    }
    USBD_SetEPTxRxStatus(USBD_EP_0, USBD_EP_STATUS_STALL, USBD_EP_STATUS_STALL);
}

static USBD_StdReqCallback_T callbacks = {0,0,0,0,0,config_callback,0,0,0,0};

void usb_serial_init(void)
{
    USBD_InitParam_T param = {0};
    GPIO_Config_T io;
    usb_disconnect();
    USBD_InitParamStructInit(&param);
    param.classReqHandler = class_request;
    param.resetHandler = reset_callback;
    param.inEpHandler = in_callback;
    param.outEpHandler = out_callback;
    param.pDeviceDesc = &g_deviceDescriptor;
    param.pConfigurationDesc = &g_configDescriptor;
    param.pStringDesc = g_stringDescriptor;
    param.pStdReqCallback = &callbacks;
    USBD_Init(&param);
    io.pin = GPIO_PIN_11 | GPIO_PIN_12;
    io.mode = GPIO_MODE_IN_FLOATING;
    io.speed = GPIO_SPEED_2MHz;
    GPIO_Config(GPIOA, &io);
}

int usb_serial_read(void)
{
    int b = -1;
    __disable_irq();
    if (!reset_seen && tail != head) { b = rx[tail & (RX_SIZE-1)]; ++tail; }
    __enable_irq();
    return b;
}

bool usb_serial_reset_seen(void)
{
    bool value;
    __disable_irq(); value = reset_seen;
    if (value && tx_state==USB_TX_BUSY) {
        USBD_SetEPTxStatus(USBD_EP_1,USBD_EP_STATUS_NAK);
        USBD_ResetEPTxFlag(USBD_EP_1);
        tx_busy=false; tx_state=USB_TX_FAILED;
    }
    reset_seen = false; __enable_irq();
    return value;
}

/* Caller storage remains immutable until DONE/FAILED; no polling from IRQs. */
void usb_serial_poll(void)
{
    uint32_t count;
    __disable_irq();
    if (tx_state!=USB_TX_BUSY) { __enable_irq(); return; }
    if (!configured || tx_epoch!=usb_epoch || board_ms-tx_start>=250u) {
        USBD_SetEPTxStatus(USBD_EP_1,USBD_EP_STATUS_NAK);
        USBD_ResetEPTxFlag(USBD_EP_1);
        tx_busy=false; if (tx_state!=USB_TX_DONE) tx_state=USB_TX_FAILED; log_stalled=true;
        __enable_irq(); return;
    }
    if (tx_busy) { __enable_irq(); return; }
    if (!tx_left) { tx_state=USB_TX_DONE; __enable_irq(); return; }
    /* Short packets terminate host transfers without an implicit ZLP. */
    count=tx_left>63u?63u:tx_left;
    memcpy(tx,tx_data,count); tx_data+=count; tx_left-=count;
    tx_busy=true; USBD_TxData(USBD_EP_1,tx,count);
    __enable_irq();
}

uint8_t usb_serial_tx_state(void) { return tx_state; }
bool usb_serial_console_active(void) { return configured && opened && !log_stalled; }
bool usb_serial_log_ready(void) { return usb_serial_console_active() && !reset_seen && tx_state!=USB_TX_BUSY; }

static bool start(const uint8_t *p, uint32_t n, bool log)
{
    __disable_irq();
    if (!configured || reset_seen || (log && (!opened || log_stalled)) || tx_state==USB_TX_BUSY || !n || n>1048u) { __enable_irq(); return false; }
    tx_data=p; tx_left=n; tx_start=board_ms; tx_epoch=usb_epoch; tx_state=USB_TX_BUSY;
    __enable_irq();
    usb_serial_poll(); return true;
}

bool usb_serial_write(const uint8_t *p, uint32_t n) { return start(p,n,false); }

bool usb_serial_log(const uint8_t *p, uint32_t n)
{
    if (!usb_serial_log_ready() || !n || n>sizeof(log_line)) return false;
    memcpy(log_line,p,n);
    return start(log_line,n,true);
}
