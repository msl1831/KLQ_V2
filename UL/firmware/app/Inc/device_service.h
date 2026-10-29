#ifndef DEVICE_SERVICE_H
#define DEVICE_SERVICE_H

#include <stdint.h>
#include "frame_codec.h"

void device_service_init(frame_parser_t *parser);
void device_service_handle_frame(const klq_frame_t *frame);
void device_service_poll(uint32_t now_ms);

#endif
