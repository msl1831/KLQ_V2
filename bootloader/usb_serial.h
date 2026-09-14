#ifndef KLQ_USB_SERIAL_H
#define KLQ_USB_SERIAL_H
#include <stdint.h>
#include <stdbool.h>
void usb_serial_init(void);
int usb_serial_read(void);
bool usb_serial_write(const uint8_t *p, uint32_t n);
bool usb_serial_reset_seen(void);
extern volatile uint32_t usb_reset_count;
#endif
