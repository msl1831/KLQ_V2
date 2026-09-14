#ifndef KLQ_SC7A20_H
#define KLQ_SC7A20_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int16_t x_mg;
    int16_t y_mg;
    int16_t z_mg;
    int32_t roll_cdeg;
    int32_t pitch_cdeg;
} sc7a20_sample_t;

bool sc7a20_init(void);
bool sc7a20_read(sc7a20_sample_t *sample);
uint8_t sc7a20_address(void);
uint8_t sc7a20_identity(void);
uint8_t sc7a20_version(void);

#endif
