#include "kilix_techno/dsp.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int16_t clamp16(int64_t value)
{
    if (value > INT16_MAX) return INT16_MAX;
    if (value < INT16_MIN) return INT16_MIN;
    return (int16_t)value;
}

bool ktech_dsp_init(ktech_dsp *dsp, uint32_t sample_rate)
{
    if (!dsp || sample_rate < 8000u || sample_rate > 192000u) return false;
    memset(dsp, 0, sizeof *dsp);
    /* One eighth note at the minimum 20 BPM, plus one frame. */
    dsp->delay_capacity = (size_t)sample_rate * 3u / 2u + 1u;
    if (dsp->delay_capacity > SIZE_MAX / 2u / sizeof *dsp->delay)
        return false;
    dsp->delay = calloc(dsp->delay_capacity * 2u, sizeof *dsp->delay);
    if (!dsp->delay) {
        memset(dsp, 0, sizeof *dsp);
        return false;
    }
    dsp->sample_rate = sample_rate;
    dsp->tempo = 120u;
    dsp->level = 100u;
    dsp->tone = 100u;
    return true;
}

void ktech_dsp_free(ktech_dsp *dsp)
{
    if (!dsp) return;
    free(dsp->delay);
    memset(dsp, 0, sizeof *dsp);
}

void ktech_dsp_configure(ktech_dsp *dsp, const ktech_project *project)
{
    if (!dsp || !dsp->delay || !project) return;
    dsp->tempo = project->tempo;
    dsp->level = project->master_level;
    dsp->tone = project->master_tone;
    dsp->drive = project->master_drive;
    dsp->delay_amount = project->master_delay;
}

static int32_t saturate(int32_t sample, uint8_t drive)
{
    if (drive == 0u) return sample;
    int64_t gain = INT64_C(32768) + (int64_t)drive * 983;
    int64_t driven = ((int64_t)sample * gain) / 32768;
    int64_t magnitude = driven < 0 ? -driven : driven;
    int64_t shaped = driven * 32768 / (32768 + magnitude);
    int64_t blended = ((int64_t)sample * (100u - drive) +
                       shaped * drive) / 100;
    if (blended > INT16_MAX) blended = INT16_MAX;
    if (blended < INT16_MIN) blended = INT16_MIN;
    return (int32_t)blended;
}

void ktech_dsp_process(ktech_dsp *dsp, int16_t *interleaved,
                       size_t frames)
{
    if (!dsp || !dsp->delay || !interleaved || frames == 0u) return;
    uint16_t tempo = dsp->tempo < 20u ? 20u : dsp->tempo;
    size_t delay_frames = (size_t)((uint64_t)dsp->sample_rate * 60u /
                                   ((uint64_t)tempo * 2u));
    if (delay_frames == 0u) delay_frames = 1u;
    if (delay_frames > dsp->delay_capacity)
        delay_frames = dsp->delay_capacity;
    if (dsp->delay_position >= delay_frames) dsp->delay_position = 0u;

    int32_t tone_alpha = 512 + (int32_t)dsp->tone * 322;
    if (tone_alpha > 32768) tone_alpha = 32768;
    const int32_t wet = (int32_t)dsp->delay_amount * 164;
    const int32_t level = ((int32_t)dsp->level * 32768 + 50) / 100;

    for (size_t frame = 0u; frame < frames; ++frame) {
        for (size_t channel = 0u; channel < 2u; ++channel) {
            size_t sample_index = frame * 2u + channel;
            int32_t value = saturate(interleaved[sample_index], dsp->drive);
            int64_t delta = (int64_t)value - dsp->tone_state[channel];
            dsp->tone_state[channel] +=
                (int32_t)(delta * tone_alpha / 32768);
            value = dsp->tone_state[channel];

            size_t delay_index = dsp->delay_position * 2u + channel;
            int32_t delayed = dsp->delay[delay_index];
            int32_t feedback = value + (int32_t)((int64_t)delayed * 11469 /
                                                 32768);
            dsp->delay[delay_index] = clamp16(feedback);
            value += (int32_t)((int64_t)delayed * wet / 16384);
            interleaved[sample_index] =
                clamp16((int64_t)value * level / 32768);
        }
        ++dsp->delay_position;
        if (dsp->delay_position >= delay_frames) dsp->delay_position = 0u;
    }
}

bool ktech_fx_rack_init(ktech_fx_rack *rack, uint32_t sample_rate)
{
    if (!rack || sample_rate < 8000u || sample_rate > 192000u) return false;
    memset(rack, 0, sizeof *rack);
    rack->delay_capacity = (size_t)sample_rate * 3u / 2u + 1u;
    size_t samples = rack->delay_capacity * 2u *
                     KTECH_EFFECT_CHAIN_CAPACITY;
    if (rack->delay_capacity > SIZE_MAX / 2u /
            KTECH_EFFECT_CHAIN_CAPACITY / sizeof *rack->delay)
        return false;
    rack->delay = calloc(samples, sizeof *rack->delay);
    if (!rack->delay) return false;
    rack->sample_rate = sample_rate;
    atomic_init(&rack->effect_count, 0u);
    for (unsigned effect = 0u; effect < KTECH_EFFECT_CHAIN_CAPACITY; ++effect)
        for (unsigned word = 0u; word < 2u; ++word)
            atomic_init(&rack->config[effect][word], 0u);
    return true;
}

void ktech_fx_rack_free(ktech_fx_rack *rack)
{
    if (!rack) return;
    free(rack->delay);
    memset(rack, 0, sizeof *rack);
}

void ktech_fx_rack_configure(ktech_fx_rack *rack, const ktech_bus *bus)
{
    if (!rack || !rack->delay || !bus) return;
    unsigned count = bus->effect_count;
    if (count > KTECH_EFFECT_CHAIN_CAPACITY)
        count = KTECH_EFFECT_CHAIN_CAPACITY;
    for (unsigned index = 0u; index < count; ++index) {
        const ktech_effect *effect = &bus->effects[index];
        unsigned first = effect->kind | ((unsigned)effect->enabled << 8) |
                         ((unsigned)effect->wet << 16) |
                         ((unsigned)effect->parameter_count << 24);
        unsigned second = effect->parameters[0] |
                          ((unsigned)effect->parameters[1] << 8) |
                          ((unsigned)effect->parameters[2] << 16) |
                          ((unsigned)effect->parameters[3] << 24);
        atomic_store_explicit(&rack->config[index][0], first,
                              memory_order_release);
        atomic_store_explicit(&rack->config[index][1], second,
                              memory_order_release);
    }
    atomic_store_explicit(&rack->effect_count, count, memory_order_release);
}

static int32_t blend_wet(int32_t dry, int32_t processed, unsigned wet)
{
    return (int32_t)(((int64_t)dry * (100u - wet) +
                      (int64_t)processed * wet) / 100);
}

static int32_t rack_delay(ktech_fx_rack *rack, unsigned slot,
                          unsigned channel, int32_t input,
                          unsigned time, unsigned feedback)
{
    size_t delay_frames = (size_t)rack->sample_rate * (20u + time * 8u) /
                          1000u;
    if (delay_frames == 0u) delay_frames = 1u;
    if (delay_frames > rack->delay_capacity)
        delay_frames = rack->delay_capacity;
    if (rack->delay_position[slot] >= delay_frames)
        rack->delay_position[slot] = 0u;
    size_t base = (size_t)slot * rack->delay_capacity * 2u;
    size_t at = base + rack->delay_position[slot] * 2u + channel;
    int32_t delayed = rack->delay[at];
    rack->delay[at] = clamp16(input +
        (int32_t)((int64_t)delayed * feedback / 100));
    return delayed;
}

void ktech_fx_rack_process(ktech_fx_rack *rack, int16_t *interleaved,
                           size_t frames)
{
    if (!rack || !rack->delay || !interleaved || frames == 0u) return;
    unsigned count = atomic_load_explicit(&rack->effect_count,
                                          memory_order_acquire);
    if (count > KTECH_EFFECT_CHAIN_CAPACITY)
        count = KTECH_EFFECT_CHAIN_CAPACITY;
    for (unsigned slot = 0u; slot < count; ++slot) {
        unsigned first = atomic_load_explicit(&rack->config[slot][0],
                                              memory_order_acquire);
        unsigned parameters = atomic_load_explicit(&rack->config[slot][1],
                                                   memory_order_acquire);
        unsigned kind = first & 0xffu;
        bool enabled = ((first >> 8) & 0xffu) != 0u;
        unsigned wet = (first >> 16) & 0xffu;
        unsigned p0 = parameters & 0xffu;
        unsigned p1 = (parameters >> 8) & 0xffu;
        if (!enabled || kind >= KTECH_EFFECT_KIND_COUNT || wet == 0u)
            continue;
        for (size_t frame = 0u; frame < frames; ++frame) {
            for (unsigned channel = 0u; channel < 2u; ++channel) {
                size_t at = frame * 2u + channel;
                int32_t dry = interleaved[at];
                int32_t value = dry;
                if (kind == KTECH_EFFECT_FILTER) {
                    int32_t alpha = 256 + (int32_t)p0 * 322;
                    if (alpha > 32768) alpha = 32768;
                    rack->filter_state[slot][channel] +=
                        (int32_t)(((int64_t)value -
                                   rack->filter_state[slot][channel]) * alpha /
                                  32768);
                    value = rack->filter_state[slot][channel];
                } else if (kind == KTECH_EFFECT_SATURATOR) {
                    value = saturate(value, (uint8_t)(p0 > 100u ? 100u : p0));
                } else if (kind == KTECH_EFFECT_TAPE_DELAY ||
                           kind == KTECH_EFFECT_ROOM) {
                    unsigned time = kind == KTECH_EFFECT_ROOM ?
                        2u + p0 / 8u : p0;
                    unsigned feedback = kind == KTECH_EFFECT_ROOM ?
                        28u + p1 / 5u : p1;
                    if (feedback > 92u) feedback = 92u;
                    int32_t delayed = rack_delay(rack, slot, channel, value,
                                                 time, feedback);
                    value = kind == KTECH_EFFECT_ROOM ?
                        value / 2 + delayed : value + delayed;
                } else if (kind == KTECH_EFFECT_COMPRESSOR) {
                    int32_t magnitude = value < 0 ? -value : value;
                    int32_t *envelope =
                        &rack->compressor_envelope[slot][channel];
                    *envelope += (magnitude - *envelope) /
                                 (magnitude > *envelope ? 8 : 128);
                    int32_t threshold = 2048 + (int32_t)p0 * 286;
                    unsigned ratio = 2u + p1 / 20u;
                    if (*envelope > threshold && *envelope > 0)
                        value = (int32_t)((int64_t)value *
                            (threshold + (*envelope - threshold) /
                             (int32_t)ratio) / *envelope);
                } else if (kind == KTECH_EFFECT_BITCRUSH) {
                    unsigned bits = 4u + p0 * 12u / 100u;
                    unsigned shift = 16u - bits;
                    int32_t quantum = (int32_t)(1u << shift);
                    unsigned hold = 1u + p1 * 7u / 100u;
                    if (rack->crush_counter[slot] == 0u)
                        rack->crush_hold[slot][channel] =
                            (value / quantum) * quantum;
                    value = rack->crush_hold[slot][channel];
                    if (channel == 1u)
                        rack->crush_counter[slot] = (uint8_t)(
                            (rack->crush_counter[slot] + 1u) % hold);
                }
                interleaved[at] = clamp16(blend_wet(dry, value, wet));
            }
            if (kind == KTECH_EFFECT_TAPE_DELAY ||
                kind == KTECH_EFFECT_ROOM) {
                ++rack->delay_position[slot];
                if (rack->delay_position[slot] >= rack->delay_capacity)
                    rack->delay_position[slot] = 0u;
            }
        }
    }
}
