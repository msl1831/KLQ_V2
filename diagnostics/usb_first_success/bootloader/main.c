#include "board.h"
#include "usb_serial.h"
#include "protocol.h"

int main(void)
{
    board_init();
#ifdef KLQ_DEMO_APP
    display_icon(1);
#endif
    usb_serial_init();
    /* Always enter the loader on reset. Host RUN is explicit; no boot timeout. */
    for (;;) { protocol_poll(); }
}
