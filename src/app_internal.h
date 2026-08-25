#ifndef KILIX_TECHNO_APP_INTERNAL_H
#define KILIX_TECHNO_APP_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum ktech_cli_operation {
    KTECH_CLI_RUN = 0,
    KTECH_CLI_EXPORT_WAV,
    KTECH_CLI_EXPORT_ARRANGEMENT,
    KTECH_CLI_EXPORT_STEMS,
    KTECH_CLI_EXPORT_MIDI,
    KTECH_CLI_IMPORT_MIDI,
    KTECH_CLI_EXPORT_PRESET
} ktech_cli_operation;

typedef struct ktech_cli_options {
    const char *open_path;
    const char *save_path;
    const char *export_input;
    const char *export_output;
    const char *preset_name;
    uint32_t sample_rate;
    uint32_t seed;
    uint16_t bars;
    ktech_cli_operation operation;
    bool new_project;
    bool headless;
    bool force_cell;
    bool no_audio;
} ktech_cli_options;

int ktech_app_run(const ktech_cli_options *options);

#endif
