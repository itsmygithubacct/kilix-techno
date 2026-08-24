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
