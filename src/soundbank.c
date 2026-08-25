#include "kilix_techno/soundbank.h"

#include "pcm_mixer.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { CANONICAL_SAMPLE_RATE = 44100 };

typedef struct sample_spec {
    const char *bank;
    const char *file;
    uint8_t root_note;
    uint8_t bank_index;
} sample_spec;

struct ktech_soundbank {
    int16_t *owned[KTECH_ROLE_COUNT];
    chipseq_pcm pcm[KTECH_ROLE_COUNT];
    uint32_t sample_rate;
    uint8_t installed_mask;
};

static const sample_spec sample_specs[] = {
    {"tr808-fischer", "kick.wav", 36u, 0u},
    {"tr808-fischer", "snare.wav", 38u, 0u},
    {"tr808-fischer", "clap.wav", 39u, 0u},
    {"tr808-fischer", "closed-hat.wav", 42u, 0u},
    {"tr808-fischer", "open-hat.wav", 46u, 0u},
    {"tr808-fischer", "cowbell.wav", 48u, 0u},
    {"stargate", "kick.wav", 36u, 1u},
    {"stargate", "snare.wav", 38u, 1u},
    {"stargate", "closed-hat.wav", 42u, 1u},
    {"stargate", "open-hat.wav", 46u, 1u},
    {"stargate", "fm-perc.wav", 48u, 1u},
    {"stargate", "sub-a.wav", 33u, 1u},
    {"scoredata-forge", "growl.wav", 36u, 2u},
    {"scoredata-forge", "house-stab.wav", 48u, 2u},
    {"mechsounds", "pluck-bass.wav", 60u, 3u},
    {"mechsounds", "fuzz-bass.wav", 60u, 3u},
    {"mechsounds", "wobble.wav", 60u, 3u},
    {"mechsounds", "time-pad.wav", 60u, 3u},
    {"mechsounds", "industrial-hit.wav", 48u, 3u},
    {"mechsounds", "glitch-vox.wav", 60u, 3u},
    {"karoryfer", "a2.wav", 45u, 4u},
    {"karoryfer", "c4.wav", 60u, 4u},
    {"karoryfer", "f3.wav", 53u, 4u},
    {"karoryfer", "g2.wav", 43u, 4u},
    {"vcsl", "clavisynth-c3.wav", 60u, 5u},
    {"vcsl", "fm-piano-c3.wav", 60u, 5u},
    {"vcsl", "brake-drum.wav", 48u, 5u},
    {"vcsl", "ocean-drum.wav", 60u, 5u}
};

_Static_assert(KTECH_ROLE_TIDAL_KICK == KTECH_NATIVE_ROLE_COUNT,
               "sample roles must follow native roles");
_Static_assert(sizeof sample_specs / sizeof sample_specs[0] ==
                   KTECH_ROLE_COUNT - KTECH_NATIVE_ROLE_COUNT,
               "every sample role needs one soundbank specification");

static void set_warning(char *warning, size_t size, const char *format, ...)
{
    va_list arguments;
    if (!warning || size == 0u) return;
    va_start(arguments, format);
    (void)vsnprintf(warning, size, format, arguments);
    va_end(arguments);
}

static bool regular_file(const char *path)
{
    struct stat status;
    return lstat(path, &status) == 0 && S_ISREG(status.st_mode) &&
           status.st_size > 0;
}

static bool data_root(char *output, size_t capacity)
{
    const char *override = getenv("KILIX_TECHNO_SOUNDBANK_DIR");
    if (override && override[0] == '/') {
        int count = snprintf(output, capacity, "%s", override);
        return count >= 0 && (size_t)count < capacity;
    }
    const char *base = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    int count;
    if (base && base[0] == '/')
        count = snprintf(output, capacity,
                         "%s/kilix-techno/soundbanks", base);
    else if (home && home[0] == '/')
        count = snprintf(output, capacity,
                         "%s/.local/share/kilix-techno/soundbanks", home);
    else
        return false;
    return count >= 0 && (size_t)count < capacity;
}

static int16_t interpolate(int16_t left, int16_t right, uint32_t numerator,
                           uint32_t denominator)
{
    int64_t first = (int64_t)left * (denominator - numerator);
    int64_t second = (int64_t)right * numerator;
    int64_t value = first + second;
    value += value >= 0 ? (int64_t)denominator / 2 :
                          -(int64_t)denominator / 2;
    value /= denominator;
    if (value > INT16_MAX) value = INT16_MAX;
    if (value < INT16_MIN) value = INT16_MIN;
    return (int16_t)value;
}

static int16_t *resample(int16_t *input, size_t input_count,
                         uint32_t output_rate, size_t *output_count)
{
    if (output_rate == CANONICAL_SAMPLE_RATE) {
        *output_count = input_count;
        return input;
    }
    if (input_count > (SIZE_MAX - output_rate / 2u) /
                      output_rate) {
        free(input);
        return NULL;
    }
    size_t count = (input_count * output_rate +
                    CANONICAL_SAMPLE_RATE / 2u) /
                   CANONICAL_SAMPLE_RATE;
    if (count == 0u || count > UINT32_MAX ||
        count > SIZE_MAX / sizeof(int16_t)) {
        free(input);
        return NULL;
    }
    int16_t *output = malloc(count * sizeof *output);
    if (!output) {
        free(input);
        return NULL;
    }
    for (size_t index = 0u; index < count; ++index) {
        uint64_t position = (uint64_t)index * CANONICAL_SAMPLE_RATE;
        size_t source = (size_t)(position / output_rate);
        uint32_t fraction = (uint32_t)(position % output_rate);
        if (source >= input_count - 1u) {
            output[index] = input[input_count - 1u];
        } else {
            output[index] = interpolate(input[source], input[source + 1u],
                                        fraction, output_rate);
        }
    }
    free(input);
    *output_count = count;
    return output;
}

static void normalize_peak(int16_t *frames, size_t count)
{
    int32_t peak = 0;
    for (size_t index = 0u; index < count; ++index) {
        int32_t magnitude = frames[index] < 0 ? -(int32_t)frames[index] :
                                                frames[index];
        if (magnitude > peak) peak = magnitude;
    }
    if (peak == 0) return;
    for (size_t index = 0u; index < count; ++index) {
        int64_t scaled = (int64_t)frames[index] * 30000;
        scaled += scaled >= 0 ? peak / 2 : -(peak / 2);
        scaled /= peak;
        if (scaled > INT16_MAX) scaled = INT16_MAX;
        if (scaled < INT16_MIN) scaled = INT16_MIN;
        frames[index] = (int16_t)scaled;
    }
}

static void clear_bank_index(ktech_soundbank *bank, uint8_t bank_index)
{
    for (size_t index = 0u;
         index < sizeof sample_specs / sizeof sample_specs[0]; ++index) {
        if (sample_specs[index].bank_index != bank_index) continue;
        size_t role = KTECH_NATIVE_ROLE_COUNT + index;
        free(bank->owned[role]);
        bank->owned[role] = NULL;
        bank->pcm[role] = (chipseq_pcm){0};
    }
    bank->installed_mask &= (uint8_t)~(1u << bank_index);
}

static bool load_bank_index(ktech_soundbank *bank, uint8_t bank_index,
                            const char *root, char *warning,
                            size_t warning_size)
{
    const sample_spec *first = NULL;
    for (size_t index = 0u;
         index < sizeof sample_specs / sizeof sample_specs[0]; ++index) {
        if (sample_specs[index].bank_index == bank_index) {
            first = &sample_specs[index];
            break;
        }
    }
    if (!first) return false;
    char receipt[4096];
    int count = snprintf(receipt, sizeof receipt, "%s/%s/.kilix-bank",
                         root, first->bank);
    if (count < 0 || (size_t)count >= sizeof receipt ||
        !regular_file(receipt))
        return false;

    for (size_t index = 0u;
         index < sizeof sample_specs / sizeof sample_specs[0]; ++index) {
        const sample_spec *spec = &sample_specs[index];
        if (spec->bank_index != bank_index) continue;
        char path[4096];
        count = snprintf(path, sizeof path, "%s/%s/%s", root, spec->bank,
                         spec->file);
        if (count < 0 || (size_t)count >= sizeof path || !regular_file(path)) {
            set_warning(warning, warning_size,
                        "%s is incomplete; using native fallbacks", first->bank);
            clear_bank_index(bank, bank_index);
            return false;
        }
        size_t frame_count = 0u;
        char wav_error[160];
        int16_t *frames = pcmmix_wav_load(path, &frame_count, wav_error,
                                           sizeof wav_error);
        if (!frames) {
            set_warning(warning, warning_size, "%s: %.100s", first->bank,
                        wav_error);
            clear_bank_index(bank, bank_index);
            return false;
        }
        frames = resample(frames, frame_count, bank->sample_rate,
                          &frame_count);
        if (!frames || frame_count > UINT32_MAX) {
            set_warning(warning, warning_size,
                        "%s cannot be resampled; using native fallbacks",
                        first->bank);
            clear_bank_index(bank, bank_index);
            return false;
        }
        normalize_peak(frames, frame_count);
        size_t role = KTECH_NATIVE_ROLE_COUNT + index;
        bank->owned[role] = frames;
        bank->pcm[role] = (chipseq_pcm){
            frames, (uint32_t)frame_count, 0u, 0u, spec->root_note
        };
    }
    bank->installed_mask |= (uint8_t)(1u << bank_index);
    return true;
}

ktech_soundbank *ktech_soundbank_load(uint32_t sample_rate, char *warning,
                                      size_t warning_size)
{
    if (warning && warning_size) warning[0] = '\0';
    if (sample_rate < 8000u || sample_rate > 192000u) return NULL;
    ktech_soundbank *bank = calloc(1u, sizeof *bank);
    if (!bank) return NULL;
    bank->sample_rate = sample_rate;
    char root[4096];
    if (!data_root(root, sizeof root)) return bank;
    for (uint8_t index = 0u; index < KTECH_SAMPLE_BANK_COUNT; ++index)
        (void)load_bank_index(bank, index, root, warning, warning_size);
    return bank;
}

void ktech_soundbank_free(ktech_soundbank *bank)
{
    if (!bank) return;
    for (size_t role = KTECH_NATIVE_ROLE_COUNT; role < KTECH_ROLE_COUNT;
         ++role)
        free(bank->owned[role]);
    memset(bank, 0, sizeof *bank);
    free(bank);
}

const chipseq_pcm *ktech_soundbank_pcm(const ktech_soundbank *bank,
                                       ktech_role role)
{
    if (!bank || role < KTECH_NATIVE_ROLE_COUNT || role >= KTECH_ROLE_COUNT ||
        !bank->owned[role])
        return NULL;
    return &bank->pcm[role];
}

bool ktech_soundbank_role_available(const ktech_soundbank *bank,
                                    ktech_role role)
{
    return ktech_soundbank_pcm(bank, role) != NULL;
}

uint8_t ktech_soundbank_installed_count(const ktech_soundbank *bank)
{
    if (!bank) return 0u;
    uint8_t count = 0u;
    for (uint8_t index = 0u; index < KTECH_SAMPLE_BANK_COUNT; ++index)
        if ((bank->installed_mask & (uint8_t)(1u << index)) != 0u) ++count;
    return count;
}
