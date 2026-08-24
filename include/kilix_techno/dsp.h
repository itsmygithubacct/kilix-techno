#ifndef KILIX_TECHNO_DSP_H
#define KILIX_TECHNO_DSP_H

#include "kilix_techno/project.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ktech_dsp {
    int32_t *delay;
    size_t delay_capacity;
    size_t delay_position;
    uint32_t sample_rate;
    uint16_t tempo;
    uint8_t level;
    uint8_t tone;
    uint8_t drive;
    uint8_t delay_amount;
    int32_t tone_state[2];
} ktech_dsp;

bool ktech_dsp_init(ktech_dsp *dsp, uint32_t sample_rate);
void ktech_dsp_free(ktech_dsp *dsp);
void ktech_dsp_configure(ktech_dsp *dsp, const ktech_project *project);
void ktech_dsp_process(ktech_dsp *dsp, int16_t *interleaved,
                       size_t frames);

#ifdef __cplusplus
}
#endif

#endif
