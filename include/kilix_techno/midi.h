#ifndef KILIX_TECHNO_MIDI_H
#define KILIX_TECHNO_MIDI_H

#include "kilix_techno/project.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KTECH_MIDI_MAX_BYTES (1024u * 1024u)
#define KTECH_MIDI_EVENT_CAPACITY 8192u

bool ktech_midi_encode(const ktech_project *project, uint8_t *bytes,
                       size_t capacity, size_t *encoded_size,
                       char *error, size_t error_size);
bool ktech_midi_decode(ktech_project *project, const uint8_t *bytes,
                       size_t size, char *error, size_t error_size);
bool ktech_midi_save(const ktech_project *project, const char *absolute_path,
                     bool replace_existing, char *error,
                     size_t error_size);
bool ktech_midi_load(ktech_project *project, const char *absolute_path,
                     char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
