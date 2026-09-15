#ifndef KLQ_EXTERNAL_PORTS_H
#define KLQ_EXTERNAL_PORTS_H
#include <stdint.h>

#define KLQ_EXTERNAL_PORT_COUNT 3u

typedef struct {
    uint8_t online, mode, device_class, sample_status, quality_flags, driver_state;
    uint16_t board_id, hw_compat, sensor_type, echo_ticks, distance_mm;
    uint32_t uid[3], fw_version, capabilities, sample_sequence, capture_time_ms;
    uint32_t timeout_count, frame_error_count, uart_error_count, request_count, response_count;
} klq_port_status_t;

void external_ports_init(void);
void external_ports_poll(void);
const klq_port_status_t *external_port_status(unsigned port);

#endif
