#include "kilix_techno/audio.h"
#include "kilix_techno/compile.h"
#include "kilix_techno/project.h"
#include "kilix_techno/ui.h"

#include "soft_raster.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t nanoseconds(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0u;
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

static int compare_u64(const void *left_value, const void *right_value)
{
    uint64_t left = *(const uint64_t *)left_value;
    uint64_t right = *(const uint64_t *)right_value;
    return left < right ? -1 : left > right ? 1 : 0;
}

int main(void)
{
    enum { DECODE_RUNS = 200, UI_RUNS = 100, AUDIO_RUNS = 1000,
           AUDIO_FRAMES = 512 };
    ktech_project project;
    ktech_project_init(&project, UINT32_C(0x62656e63));
    uint8_t encoded[KTECH_PROJECT_ENCODED_MAX];
    size_t encoded_size;
    if (ktech_project_encode(&project, encoded, sizeof encoded,
                             &encoded_size) != KTECH_PROJECT_OK)
        return 1;

    uint64_t begin = nanoseconds();
    char error[160];
    ktech_compiled *compiled = ktech_compile_scene(&project, 0u,
                                                    error, sizeof error);
    uint64_t compile_ns = nanoseconds() - begin;
    if (!compiled) return 1;
    ktech_compiled_free(compiled);

    begin = nanoseconds();
    for (unsigned run = 0u; run < DECODE_RUNS; ++run) {
        ktech_project decoded;
        if (ktech_project_decode(&decoded, encoded, encoded_size) !=
            KTECH_PROJECT_OK)
            return 1;
    }
    uint64_t decode_ns = (nanoseconds() - begin) / DECODE_RUNS;

    ktech_editor *editor = ktech_editor_create(&project);
    sr_canvas canvas;
    if (!editor || !sr_canvas_init(&canvas, 1000, 640)) {
        ktech_editor_free(editor);
        return 1;
    }
    ktech_audio_status status = {0};
    begin = nanoseconds();
    for (unsigned run = 0u; run < UI_RUNS; ++run)
        ktech_ui_render(&canvas, editor, &status, NULL);
    uint64_t ui_ns = (nanoseconds() - begin) / UI_RUNS;
    sr_canvas_free(&canvas);
    ktech_editor_free(editor);

    ktech_audio_options audio_options;
    ktech_audio_options_init(&audio_options);
    audio_options.sample_rate = 44100u;
    audio_options.offline = true;
    ktech_audio *audio = ktech_audio_create(&project, &audio_options,
                                             error, sizeof error);
    if (!audio) return 1;
    int16_t samples[AUDIO_FRAMES * 2u];
    uint64_t samples_ns[AUDIO_RUNS];
    for (unsigned run = 0u; run < AUDIO_RUNS; ++run) {
        begin = nanoseconds();
        ktech_audio_mix(audio, samples, AUDIO_FRAMES);
        samples_ns[run] = nanoseconds() - begin;
    }
    ktech_audio_free(audio);
    qsort(samples_ns, AUDIO_RUNS, sizeof samples_ns[0], compare_u64);
    uint64_t p99_ns = samples_ns[(AUDIO_RUNS * 99u) / 100u];
    uint64_t block_ns = (uint64_t)AUDIO_FRAMES * UINT64_C(1000000000) /
                        UINT64_C(44100);

    (void)printf("compile_scene_ns=%llu\nproject_decode_ns=%llu\n"
                 "ui_1000x640_ns=%llu\naudio_512_p99_ns=%llu\n"
                 "audio_block_budget_ns=%llu\naudio_budget_percent=%.2f\n",
                 (unsigned long long)compile_ns,
                 (unsigned long long)decode_ns,
                 (unsigned long long)ui_ns,
                 (unsigned long long)p99_ns,
                 (unsigned long long)block_ns,
                 block_ns ? (double)p99_ns * 100.0 / (double)block_ns : 0.0);
    if (block_ns == 0u || p99_ns > block_ns / 4u) {
        (void)fprintf(stderr, "benchmark: audio p99 exceeds 25%% budget\n");
        return 1;
    }
    if (ui_ns > UINT64_C(33333333)) {
        (void)fprintf(stderr, "benchmark: UI render misses 30 Hz budget\n");
        return 1;
    }
    return 0;
}
