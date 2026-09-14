#ifndef USB_CONFIG_H_
#define USB_CONFIG_H_
#define USB1 0
#define USB2 1
#define USB_SELECT USB1
#define USB_INT_SOURCE (USBD_INT_RST | USBD_INT_CTR | USBD_INT_SUS | USBD_INT_WKUP)
#define USB_LOW_POWER_SWITCH 0
#define USB_CONFIGURATION_NUM 1
#define USB_EP_MAX_NUM 3
#define USB_EP0_PACKET_SIZE 64
#define USB_BUFFER_TABLE_ADDR 0
/* PMA byte offsets: 24 bytes BTABLE, then disjoint 64-byte buffers. */
#define USB_EP0_TX_ADDR 0x18
#define USB_EP0_RX_ADDR 0x58
#define USB_EP1_TX_ADDR 0x98
#define USB_EP1_RX_ADDR 0xD8
#define USB_EP2_TX_ADDR 0x118
#endif
