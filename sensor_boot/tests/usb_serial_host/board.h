#ifndef TEST_USB_SERIAL_BOARD_H
#define TEST_USB_SERIAL_BOARD_H
#include <stdint.h>
#include <stdbool.h>
extern volatile uint32_t board_ms;
#define GPIOA 0
#define GPIO_PIN_11 0x800u
#define GPIO_PIN_12 0x1000u
#define GPIO_MODE_IN_FLOATING 0
#define GPIO_SPEED_2MHz 0
#define RCM_APB2_PERIPH_GPIOA 0
#define RCM_USB_DIV_1_5 0
#define RCM_APB1_PERIPH_USB 0
#define USBD1_LP_CAN1_RX0_IRQn 0
#define USB_EP1_TX_ADDR 0x80
#define USB_EP1_RX_ADDR 0xc0
#define USB_EP2_TX_ADDR 0x100
#define USBD_EP_0 0
#define USBD_EP_1 1
#define USBD_EP_2 2
#define USBD_EP_TYPE_BULK 0
#define USBD_EP_TYPE_INTERRUPT 1
#define USBD_EP_STATUS_NAK 0
#define USBD_EP_STATUS_STALL 1
#define __DMB() ((void)0)
#define RCM_EnableAPB2PeriphClock(x) ((void)0)
#define RCM_ConfigUSBCLK(x) ((void)0)
#define RCM_EnableAPB1PeriphClock(x) ((void)0)
#define RCM_EnableAPB1PeriphReset(x) ((void)0)
#define RCM_DisableAPB1PeriphReset(x) ((void)0)
#define NVIC_SetPriority(x,y) ((void)0)
#define NVIC_EnableIRQ(x) ((void)0)
#define GPIO_Config(x,y) ((void)0)
#define USBD_SetRegCTRL(x) ((void)0)
typedef struct { unsigned pin,mode,speed; } GPIO_Config_T;
typedef struct { unsigned epNum,epType,epBufAddr,maxPackSize,epStatus; } USBD_EPConfig_T;
typedef struct { struct { uint8_t bRequest,wLength[2],wIndex[2],wValue[2]; struct { struct { unsigned dir; } bit; } bmRequestType; } byte; } USBD_DevReqData_T;
typedef struct { void (*f0)(void),(*f1)(void),(*f2)(void),(*f3)(void),(*f4)(void),(*f5)(void),(*f6)(void),(*f7)(void),(*f8)(void),(*f9)(void); } USBD_StdReqCallback_T;
typedef struct { void (*classReqHandler)(USBD_DevReqData_T *); void (*resetHandler)(void); void (*inEpHandler)(uint8_t),(*outEpHandler)(uint8_t); void *pDeviceDesc,*pConfigurationDesc,*pStringDesc; USBD_StdReqCallback_T *pStdReqCallback; } USBD_InitParam_T;
typedef struct { unsigned curConfiguration; struct { unsigned xferCnt; } outBuf[3]; } MockUSBDevice;
extern MockUSBDevice g_usbDev;
extern uint8_t g_deviceDescriptor,g_configDescriptor,*g_stringDescriptor[1];
void __disable_irq(void);void __enable_irq(void);
void usb_disconnect(void);void delay_ms(uint32_t ms);
void USBD_OpenInEP(USBD_EPConfig_T *p);void USBD_OpenOutEP(USBD_EPConfig_T *p);
void USBD_SetEPRxStatus(unsigned ep,unsigned status);void USBD_SetEPTxStatus(unsigned ep,unsigned status);
void USBD_SetEPTxRxStatus(unsigned ep,unsigned a,unsigned b);void USBD_ResetEPTxFlag(unsigned ep);
void USBD_RxData(unsigned ep,uint8_t *data,unsigned n);void USBD_TxData(unsigned ep,uint8_t *data,unsigned n);
void USBD_CtrlOutData(uint8_t *data,unsigned n);void USBD_CtrlInData(uint8_t *data,unsigned n);void USBD_CtrlTxStatus(void);
void USBD_InitParamStructInit(USBD_InitParam_T *p);void USBD_Init(USBD_InitParam_T *p);
#endif
