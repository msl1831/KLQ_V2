#ifndef KLQ_USB_SERIAL_H
#define KLQ_USB_SERIAL_H
#include <stdint.h>
#include <stdbool.h>
void usb_serial_init(void);
int usb_serial_read(void);
enum { USB_TX_IDLE, USB_TX_BUSY, USB_TX_DONE, USB_TX_FAILED };
void usb_serial_poll(void);
uint8_t usb_serial_tx_state(void);
bool usb_serial_console_active(void);
bool usb_serial_log_ready(void);
/* Acceptance only; caller retains p until tx_state is DONE/FAILED. */
bool usb_serial_write(const uint8_t *p, uint32_t n);
bool usb_serial_log(const uint8_t *p, uint32_t n);
bool usb_serial_reset_seen(void);
extern volatile uint32_t usb_reset_count;
#endif
