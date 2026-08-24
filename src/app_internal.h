#ifndef KILIX_TECHNO_APP_INTERNAL_H
#define KILIX_TECHNO_APP_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct ktech_cli_options {
    const char *open_path;
    const char *save_path;
    const char *export_input;
    const char *export_output;
    uint32_t sample_rate;
    uint32_t seed;
    uint16_t bars;
    bool new_project;
    bool headless;
    bool force_cell;
    bool no_audio;
} ktech_cli_options;

int ktech_app_run(const ktech_cli_options *options);

#endif
