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
    uint8_t scene;
    bool replace_existing;
    const atomic_bool *cancel;
} ktech_export_options;

typedef struct ktech_export_info {
    uint64_t frames;
    uint64_t pcm_hash;
    uint32_t bytes;
} ktech_export_info;

void ktech_export_options_init(ktech_export_options *options);
bool ktech_export_wav(const ktech_project *project, const char *absolute_path,
                      const ktech_export_options *options,
                      ktech_export_info *info, char *error,
                      size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
