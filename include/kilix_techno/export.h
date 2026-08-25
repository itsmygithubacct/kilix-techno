#ifndef KILIX_TECHNO_EXPORT_H
#define KILIX_TECHNO_EXPORT_H

#include "kilix_techno/project.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ktech_export_options {
    uint32_t sample_rate;
    uint16_t bars;
    uint16_t tail_ms;
    uint8_t scene;
    uint8_t bit_depth;
    bool arrangement;
    bool normalize;
    bool include_master;
    bool replace_existing;
    const atomic_bool *cancel;
} ktech_export_options;

typedef struct ktech_export_info {
    uint64_t frames;
    uint64_t pcm_hash;
    uint32_t bytes;
} ktech_export_info;

void ktech_export_options_init(ktech_export_options *options);
bool ktech_export_options_apply_preset(ktech_export_options *options,
                                       const ktech_export_preset *preset,
                                       char *error, size_t error_size);
bool ktech_export_wav(const ktech_project *project, const char *absolute_path,
                      const ktech_export_options *options,
                      ktech_export_info *info, char *error,
                      size_t error_size);
bool ktech_export_stems(const ktech_project *project,
                        const char *absolute_directory,
                        const ktech_export_options *options,
                        ktech_export_info info[KTECH_TRACK_COUNT],
                        char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
