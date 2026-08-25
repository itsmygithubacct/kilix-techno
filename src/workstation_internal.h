#ifndef KTECH_WORKSTATION_INTERNAL_H
#define KTECH_WORKSTATION_INTERNAL_H

#include "kilix_techno/project.h"

#include "kilix_state_codec.h"

#include <stdbool.h>
#include <stddef.h>

void ktech_workstation_defaults(ktech_project *project);
bool ktech_workstation_validate(const ktech_project *project, char *error,
                                size_t error_size);
size_t ktech_workstation_encoded_size(const ktech_project *project);
bool ktech_workstation_write(kilixstate_writer *writer,
                             const ktech_project *project);
bool ktech_workstation_read(kilixstate_reader *reader,
                            ktech_project *project);

#endif
