#ifndef KILIX_TECHNO_DSP_H
#define KILIX_TECHNO_DSP_H

#include "kilix_techno/project.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

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

typedef struct ktech_fx_rack {
    int32_t *delay;
    size_t delay_capacity;
    size_t delay_position[KTECH_EFFECT_CHAIN_CAPACITY];
    int32_t filter_state[KTECH_EFFECT_CHAIN_CAPACITY][2];
    int32_t compressor_envelope[KTECH_EFFECT_CHAIN_CAPACITY][2];
    int32_t crush_hold[KTECH_EFFECT_CHAIN_CAPACITY][2];
    uint8_t crush_counter[KTECH_EFFECT_CHAIN_CAPACITY];
    uint32_t sample_rate;
    atomic_uint effect_count;
    atomic_uint config[KTECH_EFFECT_CHAIN_CAPACITY][2];
} ktech_fx_rack;

bool ktech_dsp_init(ktech_dsp *dsp, uint32_t sample_rate);
void ktech_dsp_free(ktech_dsp *dsp);
void ktech_dsp_configure(ktech_dsp *dsp, const ktech_project *project);
void ktech_dsp_process(ktech_dsp *dsp, int16_t *interleaved,
                       size_t frames);
bool ktech_fx_rack_init(ktech_fx_rack *rack, uint32_t sample_rate);
void ktech_fx_rack_free(ktech_fx_rack *rack);
void ktech_fx_rack_configure(ktech_fx_rack *rack, const ktech_bus *bus);
void ktech_fx_rack_process(ktech_fx_rack *rack, int16_t *interleaved,
                           size_t frames);

#ifdef __cplusplus
}
#endif

#endif
