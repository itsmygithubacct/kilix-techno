#include "app_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_help(FILE *stream)
{
    (void)fprintf(stream,
        "Usage: kilix-techno [OPTIONS]\n"
        "  --new                 create a new project\n"
        "  --open PATH           open one .ktech project\n"
        "  --headless            validate/describe without a terminal UI\n"
        "  --save PATH           save a headless new/open project\n"
        "  --export IN OUT       export the active scene as stereo WAV\n"
        "  --export-arrangement IN OUT  export the song arrangement as WAV\n"
        "  --export-stems IN DIR export eight atomic track stems\n"
        "  --export-midi IN OUT  export bounded Standard MIDI\n"
        "  --import-midi IN OUT  import MIDI into a new .ktech project\n"
        "  --export-preset IN OUT NAME  use a saved export preset\n"
        "  --bars N              export length in four-beat bars (1..256)\n"
        "  --sample-rate HZ      audio/export rate (8000..192000)\n"
        "  --seed N              deterministic seed for a new project\n"
        "  --cell                 force the ordinary-terminal interface\n"
        "  --no-audio             do not probe or start an audio sink\n"
        "  --version              print the version\n"
        "  --help                 show this help\n");
}

static bool parse_u32(const char *text, uint32_t minimum, uint32_t maximum,
                      uint32_t *value)
{
    if (!text || !value || text[0] == '\0' || text[0] == '-') return false;
    errno = 0;
    char *end;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed < minimum || parsed > maximum)
        return false;
    *value = (uint32_t)parsed;
    return true;
}

int main(int argc, char **argv)
{
    ktech_cli_options options;
    memset(&options, 0, sizeof options);
    options.sample_rate = 44100u;
    options.bars = 4u;
    options.seed = UINT32_C(0x4b544543);

    for (int index = 1; index < argc; ++index) {
        const char *argument = argv[index];
        if (strcmp(argument, "--help") == 0) {
            print_help(stdout);
            return 0;
        } else if (strcmp(argument, "--version") == 0) {
            (void)printf("kilix-techno 0.2.1\n");
            return 0;
        } else if (strcmp(argument, "--new") == 0) {
            options.new_project = true;
        } else if (strcmp(argument, "--headless") == 0) {
            options.headless = true;
        } else if (strcmp(argument, "--cell") == 0) {
            options.force_cell = true;
        } else if (strcmp(argument, "--no-audio") == 0) {
            options.no_audio = true;
        } else if (strcmp(argument, "--open") == 0 && index + 1 < argc) {
            options.open_path = argv[++index];
        } else if (strcmp(argument, "--save") == 0 && index + 1 < argc) {
            options.save_path = argv[++index];
        } else if (strcmp(argument, "--export") == 0 && index + 2 < argc) {
            if (options.operation != KTECH_CLI_RUN) goto duplicate_operation;
            options.export_input = argv[++index];
            options.export_output = argv[++index];
            options.operation = KTECH_CLI_EXPORT_WAV;
        } else if (strcmp(argument, "--export-arrangement") == 0 &&
                   index + 2 < argc) {
            if (options.operation != KTECH_CLI_RUN) goto duplicate_operation;
            options.export_input = argv[++index];
            options.export_output = argv[++index];
            options.operation = KTECH_CLI_EXPORT_ARRANGEMENT;
        } else if (strcmp(argument, "--export-stems") == 0 &&
                   index + 2 < argc) {
            if (options.operation != KTECH_CLI_RUN) goto duplicate_operation;
            options.export_input = argv[++index];
            options.export_output = argv[++index];
            options.operation = KTECH_CLI_EXPORT_STEMS;
        } else if (strcmp(argument, "--export-midi") == 0 &&
                   index + 2 < argc) {
            if (options.operation != KTECH_CLI_RUN) goto duplicate_operation;
            options.export_input = argv[++index];
            options.export_output = argv[++index];
            options.operation = KTECH_CLI_EXPORT_MIDI;
        } else if (strcmp(argument, "--import-midi") == 0 &&
                   index + 2 < argc) {
            if (options.operation != KTECH_CLI_RUN) goto duplicate_operation;
            options.export_input = argv[++index];
            options.export_output = argv[++index];
            options.operation = KTECH_CLI_IMPORT_MIDI;
        } else if (strcmp(argument, "--export-preset") == 0 &&
                   index + 3 < argc) {
            if (options.operation != KTECH_CLI_RUN) goto duplicate_operation;
            options.export_input = argv[++index];
            options.export_output = argv[++index];
            options.preset_name = argv[++index];
            options.operation = KTECH_CLI_EXPORT_PRESET;
        } else if (strcmp(argument, "--sample-rate") == 0 &&
                   index + 1 < argc) {
            if (!parse_u32(argv[++index], 8000u, 192000u,
                           &options.sample_rate)) {
                (void)fprintf(stderr, "invalid sample rate\n");
                return 2;
            }
        } else if (strcmp(argument, "--bars") == 0 && index + 1 < argc) {
            uint32_t bars;
            if (!parse_u32(argv[++index], 1u, 256u, &bars)) {
                (void)fprintf(stderr, "invalid bar count\n");
                return 2;
            }
            options.bars = (uint16_t)bars;
        } else if (strcmp(argument, "--seed") == 0 && index + 1 < argc) {
            if (!parse_u32(argv[++index], 1u, UINT32_MAX, &options.seed)) {
                (void)fprintf(stderr, "invalid project seed\n");
                return 2;
            }
        } else {
            (void)fprintf(stderr, "unknown or incomplete option: %s\n",
                          argument);
            print_help(stderr);
            return 2;
        }
        continue;
duplicate_operation:
        (void)fprintf(stderr, "only one import/export operation is allowed\n");
        return 2;
    }
    if (options.open_path && options.new_project) {
        (void)fprintf(stderr, "--new and --open are mutually exclusive\n");
        return 2;
    }
    return ktech_app_run(&options);
}
