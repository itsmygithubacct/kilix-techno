#include "kilix_techno/project.h"
#include "kilix_techno/settings.h"
#include "kilix_techno/midi.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t random_state = UINT32_C(0x91e10da5);

static uint32_t next_random(void)
{
    uint32_t value = random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    random_state = value;
    return value;
}

static size_t parse_iterations(int argc, char **argv)
{
    if (argc < 2) return 5000u;
    errno = 0;
    char *end;
    unsigned long value = strtoul(argv[1], &end, 10);
    if (errno != 0 || *end != '\0' || value == 0u || value > 1000000u)
        return 0u;
    return (size_t)value;
}

static int fuzz_project(size_t iterations)
{
    uint8_t *canonical = malloc(KTECH_PROJECT_ENCODED_MAX);
    uint8_t *mutated = malloc(KTECH_PROJECT_ENCODED_MAX + 1u);
    ktech_project *destination = malloc(sizeof *destination);
    ktech_project *before = malloc(sizeof *before);
    if (!canonical || !mutated || !destination || !before) {
        free(canonical); free(mutated); free(destination); free(before);
        return 1;
    }
    size_t canonical_size;
    ktech_project source;
    ktech_project_init(&source, UINT32_C(0x6b746563));
    if (ktech_project_encode(&source, canonical, KTECH_PROJECT_ENCODED_MAX,
                             &canonical_size) != KTECH_PROJECT_OK) {
        free(canonical); free(mutated); free(destination); free(before);
        return 1;
    }

    int failed = 0;
    for (size_t iteration = 0u; iteration < iterations; ++iteration) {
        memcpy(mutated, canonical, canonical_size);
        size_t size = canonical_size;
        uint32_t mode = next_random() % 4u;
        if (mode == 0u) {
            size = next_random() % (canonical_size + 1u);
        } else if (mode == 1u && size < KTECH_PROJECT_ENCODED_MAX + 1u) {
            mutated[size++] = (uint8_t)next_random();
        } else {
            size_t changes = 1u + next_random() % 8u;
            for (size_t change = 0u; change < changes; ++change) {
                size_t offset = next_random() % size;
                mutated[offset] ^= (uint8_t)(1u << (next_random() & 7u));
            }
        }
        memset(destination, 0xa5, sizeof *destination);
        *before = *destination;
        ktech_project_result result = ktech_project_decode(
            destination, mutated, size);
        if (result == KTECH_PROJECT_OK) {
            if (!ktech_project_validate(destination, NULL, 0u)) {
                failed = 1;
                break;
            }
        } else if (memcmp(destination, before, sizeof *destination) != 0) {
            failed = 1;
            break;
        }
    }
    memset(destination, 0xa5, sizeof *destination);
    *before = *destination;
    if (ktech_project_decode(destination, canonical,
                             KTECH_PROJECT_MAX_BYTES + 1u) !=
            KTECH_PROJECT_TOO_LARGE ||
        memcmp(destination, before, sizeof *destination) != 0)
        failed = 1;
    free(canonical); free(mutated); free(destination); free(before);
    return failed;
}

static int fuzz_midi(size_t iterations)
{
    uint8_t *canonical = malloc(KTECH_MIDI_MAX_BYTES);
    uint8_t *mutated = malloc(KTECH_MIDI_MAX_BYTES + 1u);
    ktech_project *destination = malloc(sizeof *destination);
    ktech_project *before = malloc(sizeof *before);
    if (!canonical || !mutated || !destination || !before) {
        free(canonical); free(mutated); free(destination); free(before);
        return 1;
    }
    ktech_project source;
    ktech_project_init(&source, UINT32_C(0x6d696469));
    size_t canonical_size = 0u;
    char error[96];
    if (!ktech_midi_encode(&source, canonical, KTECH_MIDI_MAX_BYTES,
                           &canonical_size, error, sizeof error)) {
        free(canonical); free(mutated); free(destination); free(before);
        return 1;
    }
    int failed = 0;
    for (size_t iteration = 0u; iteration < iterations; ++iteration) {
        memcpy(mutated, canonical, canonical_size);
        size_t size = canonical_size;
        uint32_t mode = next_random() % 3u;
        if (mode == 0u) size = next_random() % (canonical_size + 1u);
        else if (mode == 1u && size < KTECH_MIDI_MAX_BYTES + 1u)
            mutated[size++] = (uint8_t)next_random();
        else {
            size_t changes = 1u + next_random() % 8u;
            for (size_t change = 0u; change < changes; ++change)
                mutated[next_random() % size] ^=
                    (uint8_t)(1u << (next_random() & 7u));
        }
        memset(destination, 0xa5, sizeof *destination);
        *before = *destination;
        if (ktech_midi_decode(destination, mutated, size, error,
                              sizeof error)) {
            if (!ktech_project_validate(destination, NULL, 0u)) {
                failed = 1;
                break;
            }
        } else if (memcmp(destination, before, sizeof *destination) != 0) {
            failed = 1;
            break;
        }
    }
    free(canonical); free(mutated); free(destination); free(before);
    return failed;
}

static int fuzz_settings(size_t iterations)
{
    uint8_t canonical[KTECH_SETTINGS_ENCODED_MAX];
    uint8_t mutated[KTECH_SETTINGS_ENCODED_MAX + 1u];
    size_t canonical_size;
    ktech_settings source;
    ktech_settings_init(&source);
    if (ktech_settings_encode(&source, canonical, sizeof canonical,
                              &canonical_size) != KTECH_SETTINGS_OK)
        return 1;
    for (size_t iteration = 0u; iteration < iterations; ++iteration) {
        memcpy(mutated, canonical, canonical_size);
        size_t size = canonical_size;
        uint32_t mode = next_random() % 3u;
        if (mode == 0u) {
            size = next_random() % (canonical_size + 1u);
        } else if (mode == 1u && size < sizeof mutated) {
            mutated[size++] = (uint8_t)next_random();
        } else {
            size_t offset = next_random() % size;
            mutated[offset] ^= (uint8_t)(1u << (next_random() & 7u));
        }
        ktech_settings destination;
        memset(&destination, 0xa5, sizeof destination);
        ktech_settings before = destination;
        ktech_settings_result result = ktech_settings_decode(
            &destination, mutated, size);
        if (result == KTECH_SETTINGS_OK) {
            if (!ktech_settings_validate(&destination)) return 1;
        } else if (memcmp(&destination, &before, sizeof destination) != 0) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    size_t iterations = parse_iterations(argc, argv);
    if (iterations == 0u) {
        (void)fprintf(stderr, "usage: fuzz-project [1..1000000]\n");
        return 2;
    }
    if (fuzz_project(iterations) != 0 ||
        fuzz_settings(iterations / 4u + 1u) != 0 ||
        fuzz_midi(iterations / 4u + 1u) != 0) {
        (void)fprintf(stderr, "fuzz-project: invariant failure\n");
        return 1;
    }
    (void)printf("fuzz-project: OK (%zu project, %zu settings, %zu MIDI cases)\n",
                 iterations, iterations / 4u + 1u,
                 iterations / 4u + 1u);
    return 0;
}
