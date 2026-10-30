#ifndef KLQ_BLE_SERIAL_H
#define KLQ_BLE_SERIAL_H
#include <stdint.h>
#include <stdbool.h>
enum { BLE_TX_IDLE, BLE_TX_BUSY, BLE_TX_DONE, BLE_TX_FAILED };
#define BLE_STATUS_SIZE 88u
void ble_serial_init(void);
void ble_serial_poll(void);
int ble_serial_read(void);
bool ble_serial_write(const uint8_t *p,uint32_t n);
uint8_t ble_serial_tx_state(void);
/* A true reset event synchronously releases any BUSY buffer before returning. */
bool ble_serial_reset_seen(void);
bool ble_serial_ready(void);
bool ble_serial_connected(void);
/* ABI1: flags[8], seven LE32 counters, name[21], version[16], MAC[13], zero[2]. */
uint16_t ble_serial_status(uint8_t *out);
#ifdef KLQ_BLE_TEST
void ble_serial_test_receive(uint8_t byte,uint8_t error);
#endif
#endif
