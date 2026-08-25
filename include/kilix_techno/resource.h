#ifndef KILIX_TECHNO_RESOURCE_H
#define KILIX_TECHNO_RESOURCE_H

#include "kilix_techno/project.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ktech_resource_report {
    uint8_t available;
    uint8_t missing;
    uint8_t changed;
    uint8_t unsafe;
} ktech_resource_report;

/* Import runs only on the control thread. Source and root must be absolute;
 * destination is a canonical relative path below root. The source must be a
 * regular, non-symlink WAV/OGG/SFZ/SF2 file no larger than 64 MiB and decode
 * successfully through pcm-mixer-assets before a private copy is published.
 * SFZ/SF2 sources are rendered to a self-contained mono WAV, so their
 * destination must end in .wav; WAV/OGG sources retain their original form. */
bool ktech_sample_import(ktech_project *project, const char *source,
                         const char *resource_root,
                         const char *destination, const char *license,
                         ktech_role role, char *error, size_t error_size);
bool ktech_resource_check(const ktech_project *project,
                          const char *resource_root,
                          ktech_resource_report *report,
                          char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
