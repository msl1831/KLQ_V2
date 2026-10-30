#ifndef KLQ_RUNTIME_H
#define KLQ_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>
#include "sc7a20.h"

enum { KLQ_RT_OK=0, KLQ_RT_RANGE, KLQ_RT_OFFLINE, KLQ_RT_UNSUPPORTED };

void klq_runtime_init(void);
void klq_runtime_poll(void);
void klq_runtime_start(void);
void klq_runtime_update_imu(const sc7a20_sample_t *sample, bool valid);
enum { KLQ_COND_PENDING=0, KLQ_COND_TRUE, KLQ_COND_FAULT };
uint8_t klq_runtime_distance_state(unsigned port, int compare, int32_t cm);
uint8_t klq_runtime_tilt_state(unsigned direction);
bool klq_runtime_distance(unsigned port, int compare, int32_t cm);
bool klq_runtime_tilt(unsigned direction);
bool klq_runtime_button(bool pressed);
int klq_runtime_motor(unsigned port, int direction, bool run);
int klq_runtime_move(unsigned direction, bool run);
bool klq_runtime_motors_ok(void);
void klq_runtime_stop_all(void);
void klq_runtime_set_motor_power(unsigned level);
void klq_runtime_set_move_power(unsigned level);
bool klq_runtime_face(unsigned face);
bool klq_runtime_pattern(const uint8_t columns[13]);
bool klq_runtime_number(unsigned value);
bool klq_runtime_display_off(void);

#endif
