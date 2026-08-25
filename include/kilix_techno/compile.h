#ifndef KILIX_TECHNO_COMPILE_H
#define KILIX_TECHNO_COMPILE_H

#include "kilix_techno/project.h"
#include "kilix_techno/soundbank.h"

#include "chip_sequencer.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KTECH_PERFORMANCE_SUPERCYCLES 64u
#define KTECH_COMPILED_ROWS_MAX 12288u

typedef struct ktech_compiled ktech_compiled;

ktech_compiled *ktech_compile_scene(const ktech_project *project,
                                    uint8_t scene, char *error,
                                    size_t error_size);
ktech_compiled *ktech_compile_scene_with_bank(
    const ktech_project *project, uint8_t scene, const ktech_soundbank *bank,
    char *error, size_t error_size);
void ktech_compiled_free(ktech_compiled *compiled);
const chipseq_song *ktech_compiled_song(const ktech_compiled *compiled);
uint16_t ktech_compiled_rows(const ktech_compiled *compiled);
uint16_t ktech_compiled_supercycle_rows(const ktech_compiled *compiled);
uint64_t ktech_compiled_hash(const ktech_compiled *compiled);
uint8_t ktech_compiled_scene(const ktech_compiled *compiled);

/* Stable probability decision used by compiler, preview, and tests. */
bool ktech_probability_hit(uint32_t seed, uint8_t scene, uint8_t track,
                           uint32_t loop, uint8_t step,
                           uint8_t probability);

#ifdef __cplusplus
}
#endif

#endif
