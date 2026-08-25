#include "kilix_techno/soundbank.h"

#include "pcmmix_asset.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct sample_spec {
    const char *bank;
    const char *file;
    uint16_t hold_ms;
    uint16_t release_ms;
    uint8_t root_note;
    uint8_t select_note;
    uint8_t velocity;
    uint8_t program;
    uint8_t bank_index;
} sample_spec;

struct ktech_soundbank {
    int16_t *owned[KTECH_ROLE_COUNT];
    chipseq_pcm pcm[KTECH_ROLE_COUNT];
    uint32_t sample_rate;
    uint32_t installed_mask;
};

#define ASSET(bank_value, file_value, root_value, select_value, velocity_value, \
              program_value, hold_value, release_value, index_value)          \
    {(bank_value), (file_value), (hold_value), (release_value), (root_value), \
     (select_value), (velocity_value), (program_value), (index_value)}
#define PCM(bank_value, file_value, root_value, index_value)                  \
    ASSET((bank_value), (file_value), (root_value), (root_value), 100u, 0u,  \
          1200u, 500u, (index_value))

static const sample_spec sample_specs[] = {
    PCM("tr808-fischer", "kick.wav", 36u, 0u),
    PCM("tr808-fischer", "snare.wav", 38u, 0u),
    PCM("tr808-fischer", "clap.wav", 39u, 0u),
    PCM("tr808-fischer", "closed-hat.wav", 42u, 0u),
    PCM("tr808-fischer", "open-hat.wav", 46u, 0u),
    PCM("tr808-fischer", "cowbell.wav", 48u, 0u),
    PCM("stargate", "kick.wav", 36u, 1u),
    PCM("stargate", "snare.wav", 38u, 1u),
    PCM("stargate", "closed-hat.wav", 42u, 1u),
    PCM("stargate", "open-hat.wav", 46u, 1u),
    PCM("stargate", "fm-perc.wav", 48u, 1u),
    PCM("stargate", "sub-a.wav", 33u, 1u),
    PCM("scoredata-forge", "growl.wav", 36u, 2u),
    PCM("scoredata-forge", "house-stab.wav", 48u, 2u),
    PCM("mechsounds", "pluck-bass.wav", 60u, 3u),
    PCM("mechsounds", "fuzz-bass.wav", 60u, 3u),
    PCM("mechsounds", "wobble.wav", 60u, 3u),
    PCM("mechsounds", "time-pad.wav", 60u, 3u),
    PCM("mechsounds", "industrial-hit.wav", 48u, 3u),
    PCM("mechsounds", "glitch-vox.wav", 60u, 3u),
    PCM("karoryfer", "a2.wav", 45u, 4u),
    PCM("karoryfer", "c4.wav", 60u, 4u),
    PCM("karoryfer", "f3.wav", 53u, 4u),
    PCM("karoryfer", "g2.wav", 43u, 4u),
    PCM("vcsl", "clavisynth-c3.wav", 60u, 5u),
    PCM("vcsl", "fm-piano-c3.wav", 60u, 5u),
    PCM("vcsl", "brake-drum.wav", 48u, 5u),
    PCM("vcsl", "ocean-drum.wav", 60u, 5u),

    PCM("ongenet-kit", "kick.wav", 36u, 6u),
    PCM("ongenet-kit", "snare.wav", 38u, 6u),
    PCM("ongenet-kit", "clap.wav", 39u, 6u),
    PCM("ongenet-kit", "closed-hat.wav", 42u, 6u),
    PCM("ongenet-kit", "open-hat.wav", 46u, 6u),
    PCM("ongenet-kit", "ride.wav", 51u, 6u),

    ASSET("vcsl-acoustic", "kick.sfz", 36u, 60u, 90u, 0u,
          1200u, 500u, 7u),
    ASSET("vcsl-acoustic", "snare.sfz", 38u, 60u, 90u, 0u,
          1200u, 500u, 7u),
    ASSET("vcsl-acoustic", "strumstick.sfz", 59u, 60u, 90u, 0u,
          1200u, 500u, 7u),
    ASSET("vcsl-acoustic", "piano.sfz", 60u, 60u, 80u, 0u,
          1200u, 500u, 7u),

    ASSET("vsco2ce", "violin-pizz.sfz", 59u, 60u, 80u, 0u,
          1200u, 500u, 8u),
    ASSET("vsco2ce", "cello-pizz.sfz", 60u, 60u, 80u, 0u,
          1200u, 500u, 8u),
    ASSET("vsco2ce", "clarinet-stac.sfz", 62u, 60u, 80u, 0u,
          1200u, 500u, 8u),
    ASSET("vsco2ce", "horn-stac.sfz", 60u, 60u, 80u, 0u,
          1200u, 500u, 8u),

    ASSET("sf2-chaosbank", "ChaosBank.sf2", 60u, 60u, 100u, 0u,
          900u, 300u, 9u),
    ASSET("sf2-jnsgm2", "Jnsgm2.sf2", 36u, 36u, 100u, 38u,
          900u, 300u, 10u),
    ASSET("sf2-masterpiece", "Masterpiece.sf2", 60u, 60u, 100u, 89u,
          1200u, 500u, 11u),
    ASSET("sf2-unison", "Unison.SF2", 60u, 60u, 100u, 81u,
          1000u, 400u, 12u)
};

#undef PCM
#undef ASSET

_Static_assert(KTECH_ROLE_TIDAL_KICK == KTECH_NATIVE_ROLE_COUNT,
               "sample roles must follow native roles");
_Static_assert(sizeof sample_specs / sizeof sample_specs[0] ==
                   KTECH_ROLE_COUNT - KTECH_NATIVE_ROLE_COUNT,
               "every sample role needs one soundbank specification");
_Static_assert(KTECH_SAMPLE_BANK_COUNT <= 32u,
               "installed bank mask capacity exceeded");

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
        pcmmix_asset_free(bank->owned[role]);
        bank->owned[role] = NULL;
        bank->pcm[role] = (chipseq_pcm){0};
    }
    bank->installed_mask &= ~(UINT32_C(1) << bank_index);
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
        char asset_error[160];
        uint8_t asset_root = 0u;
        pcmmix_asset_options options;
        pcmmix_asset_options_init(&options);
        options.target_rate = bank->sample_rate;
        options.note = spec->select_note;
        options.velocity = spec->velocity;
        options.sf2_program = spec->program;
        options.sf2_hold_ms = spec->hold_ms;
        options.sf2_release_ms = spec->release_ms;
        int16_t *frames = pcmmix_asset_load(
            path, &options, &frame_count, &asset_root, asset_error,
            sizeof asset_error);
        if (!frames) {
            set_warning(warning, warning_size, "%s: %.100s", first->bank,
                        asset_error);
            clear_bank_index(bank, bank_index);
            return false;
        }
        if (frame_count > UINT32_MAX) {
            pcmmix_asset_free(frames);
            set_warning(warning, warning_size,
                        "%s is too long; using native fallbacks",
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
    bank->installed_mask |= UINT32_C(1) << bank_index;
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
        pcmmix_asset_free(bank->owned[role]);
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
        if ((bank->installed_mask & (UINT32_C(1) << index)) != 0u) ++count;
    return count;
}
