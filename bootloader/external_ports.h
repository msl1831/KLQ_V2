#ifndef KLQ_EXTERNAL_PORTS_H
#define KLQ_EXTERNAL_PORTS_H
#include <stdbool.h>
#include <stdint.h>

#define KLQ_EXTERNAL_PORT_COUNT 3u
#define KLQ_SENSOR_CS100A 1u
#define KLQ_SENSOR_IR_DUAL 2u

typedef struct {
    uint8_t online, mode, device_class, sample_status, quality_flags, driver_state, sample_valid;
    uint16_t board_id, hw_compat, sensor_type, echo_ticks, distance_mm;
    int16_t ir_left_delta, ir_right_delta;
    uint16_t ir_left, ir_right, ir_left_ambient, ir_left_active, ir_right_ambient, ir_right_active;
    /* Only a new sequence/capture pair publishes sample_host_ms; zero is a valid first timestamp. */
    uint32_t uid[3], fw_version, capabilities, sample_sequence, capture_time_ms, sample_host_ms;
    uint32_t timeout_count, frame_error_count, uart_error_count, request_count, response_count;
} klq_port_status_t;

void external_ports_init(void);
void external_ports_poll(void);
void external_ports_set_active(bool active);
const klq_port_status_t *external_port_status(unsigned port);
enum { KLQ_MOTOR_IDLE, KLQ_MOTOR_PENDING, KLQ_MOTOR_ACTIVE, KLQ_MOTOR_STOPPING, KLQ_MOTOR_ERROR };
bool external_ports_motor_stopped(void);
bool external_port_motor_ready(unsigned port);
bool external_port_motor_command(unsigned port, int16_t pwm);
uint8_t external_port_motor_state(unsigned port);
int16_t external_port_motor_ack_pwm(unsigned port);
int16_t external_port_motor_goal_pwm(unsigned port);
bool external_port_motor(unsigned port, int direction, bool run, unsigned power);
void external_ports_motor_stop_all(void);
uint32_t external_upgrade_open(uint8_t port0, uint32_t token, uint16_t board, uint16_t hw);
uint32_t external_upgrade_submit(uint32_t token, const uint8_t *frame, uint16_t length);
uint32_t external_upgrade_result(uint32_t token, uint8_t *out, uint16_t *length);
uint32_t external_upgrade_close(uint32_t token);
void external_upgrade_cancel(void);
uint8_t external_upgrade_active(void);
uint8_t external_motor_control_active(void);

#endif
