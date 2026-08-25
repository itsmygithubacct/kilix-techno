#include "kilix_techno/audio.h"
#include "kilix_techno/project.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

typedef struct thread_fixture {
    ktech_audio *audio;
    ktech_project project;
    atomic_bool renderer_done;
    atomic_bool failed;
} thread_fixture;

static void *render_thread(void *user)
{
    thread_fixture *fixture = user;
    int16_t samples[127u * 2u];
    for (unsigned block = 0u; block < 2000u; ++block)
        ktech_audio_mix(fixture->audio, samples,
                        1u + (size_t)(block % 127u));
    atomic_store_explicit(&fixture->renderer_done, true,
                          memory_order_release);
    return NULL;
}

static void *control_thread(void *user)
{
    thread_fixture *fixture = user;
    unsigned edit = 0u;
    while (!atomic_load_explicit(&fixture->renderer_done,
                                 memory_order_acquire)) {
        ktech_audio_poll(fixture->audio);
        ktech_audio_status status;
        ktech_audio_get_status(fixture->audio, &status);
        if (!status.pending && edit < 128u) {
            ktech_track *track = &fixture->project.scenes[0]
                .tracks[edit % KTECH_TRACK_COUNT];
            uint8_t step = (uint8_t)(edit % track->length);
            track->steps[step].enabled ^= 1u;
            char error[160];
            if (!ktech_audio_publish(fixture->audio, &fixture->project, 0u,
                                     KTECH_LAUNCH_STEP, false,
                                     error, sizeof error)) {
                (void)fprintf(stderr, "test-thread: publish: %s\n", error);
                atomic_store_explicit(&fixture->failed, true,
                                      memory_order_release);
            }
            ++edit;
        }
        fixture->project.scenes[0].tracks[0].pan = (uint8_t)edit;
        if (!ktech_audio_update_mix(fixture->audio, &fixture->project, 0u)) {
            (void)fprintf(stderr, "test-thread: mix update failed\n");
            atomic_store_explicit(&fixture->failed, true,
                                  memory_order_release);
        }
    }
    return NULL;
}

int main(void)
{
    thread_fixture fixture;
    ktech_project_init(&fixture.project, UINT32_C(0x74687264));
    atomic_init(&fixture.renderer_done, false);
    atomic_init(&fixture.failed, false);
    ktech_audio_options options;
    ktech_audio_options_init(&options);
    options.sample_rate = 8000u;
    options.offline = true;
    char error[160];
    fixture.audio = ktech_audio_create(&fixture.project, &options,
                                        error, sizeof error);
    if (!fixture.audio) {
        (void)fprintf(stderr, "test-thread: %s\n", error);
        return 1;
    }
    pthread_t renderer, control;
    if (pthread_create(&renderer, NULL, render_thread, &fixture) != 0 ||
        pthread_create(&control, NULL, control_thread, &fixture) != 0) {
        ktech_audio_free(fixture.audio);
        return 1;
    }
    (void)pthread_join(renderer, NULL);
    (void)pthread_join(control, NULL);
    ktech_audio_poll(fixture.audio);
    bool failed = atomic_load_explicit(&fixture.failed,
                                       memory_order_acquire);
    ktech_audio_free(fixture.audio);
    if (failed) return 1;
    (void)printf("test-thread: OK\n");
    return 0;
}
