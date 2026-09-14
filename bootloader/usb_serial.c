/* KLQ board adapter for the Geehy USB device library. */
#include "board.h"
#include "usb_serial.h"
#include "usbd_init.h"
#include "usbd_descriptor.h"
#include <string.h>

#define RX_SIZE 4096u
static uint8_t rx[RX_SIZE], packet[64], tx[64];
static volatile uint32_t head, tail;
static volatile bool configured, tx_busy, reset_seen;
volatile uint32_t usb_reset_count;
static uint8_t line_coding[8] = {0x00,0xc2,0x01,0,0,0,8,0};

void USBD_HardWareInit(void)
{
    GPIO_Config_T io;
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_GPIOA);
    /* D+ remains driven low until the controller and callbacks are ready. */
    io.pin = GPIO_PIN_11;
    io.mode = GPIO_MODE_IN_FLOATING;
    io.speed = GPIO_SPEED_2MHz;
    GPIO_Config(GPIOA, &io);
    RCM_EnableAPB1PeriphReset(RCM_APB1_PERIPH_USB);
    RCM_DisableAPB1PeriphReset(RCM_APB1_PERIPH_USB);
    RCM_ConfigUSBCLK(RCM_USB_DIV_1_5);
    RCM_EnableAPB1PeriphClock(RCM_APB1_PERIPH_USB);
    USBD_SetRegCTRL(1); /* Power analog block, retain forced digital reset. */
    delay_ms(2);
    NVIC_SetPriority(USBD1_LP_CAN1_RX0_IRQn, 2);
    NVIC_EnableIRQ(USBD1_LP_CAN1_RX0_IRQn);
}

static void reset_callback(void)
{
    USBD_EPConfig_T ep;
    memset(&ep, 0, sizeof(ep));
    head = tail = 0;
    configured = tx_busy = false;
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
    configured = g_usbDev.curConfiguration == 1;
    head = tail = 0;
    tx_busy = false;
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
    } else { head = tail = 0; reset_seen = true; }
    USBD_RxData(USBD_EP_1, packet, sizeof(packet));
}
static void in_callback(uint8_t ep) { if (ep == USBD_EP_1) tx_busy = false; }

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
    io.pin = GPIO_PIN_12;
    io.mode = GPIO_MODE_IN_FLOATING;
    io.speed = GPIO_SPEED_2MHz;
    GPIO_Config(GPIOA, &io);
}

int usb_serial_read(void)
{
    int b = -1;
    __disable_irq();
    if (tail != head) { b = rx[tail & (RX_SIZE-1)]; ++tail; }
    __enable_irq();
    return b;
}

bool usb_serial_reset_seen(void)
{
    bool value;
    __disable_irq(); value = reset_seen; reset_seen = false; __enable_irq();
    return value;
}

bool usb_serial_write(const uint8_t *p, uint32_t n)
{
    uint32_t count, begin;
    while (n) {
        if (!configured) return false;
        /* Short packets avoid relying on host read length or missing ZLP logic. */
        count = n > 63 ? 63 : n;
        memcpy(tx, p, count);
        __disable_irq();
        if (!configured) { __enable_irq(); return false; }
        tx_busy = true;
        USBD_TxData(USBD_EP_1, tx, count);
        __enable_irq();
        begin = board_ms;
        while (tx_busy && configured && board_ms - begin < 2000u) {}
        if (tx_busy || !configured) {
            __disable_irq();
            USBD_SetEPTxStatus(USBD_EP_1, USBD_EP_STATUS_NAK);
            tx_busy = false;
            __enable_irq();
            return false;
        }
        p += count; n -= count;
    }
    return true;
}
