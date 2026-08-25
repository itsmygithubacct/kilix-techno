#ifndef KILIX_TECHNO_AUDIO_H
#define KILIX_TECHNO_AUDIO_H

#include "kilix_techno/project.h"

#include "chip_sequencer.h"
#include "pcm_mixer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ktech_audio ktech_audio;

typedef struct ktech_audio_options {
    uint32_t sample_rate;
    uint32_t latency_ms;
    bool offline;
    bool arrangement;
    bool bypass_master;
} ktech_audio_options;

typedef struct ktech_audio_status {
    chipseq_playhead playhead;
    pcmmix_stats transport;
    const char *backend;
    uint64_t queue_failures;
    uint32_t sample_rate;
    uint32_t latency_ms;
    uint32_t pending_generation;
    uint32_t soundbank_mask;
    uint16_t peak_left;
    uint16_t peak_right;
    uint16_t bus_peak_left[KTECH_BUS_CAPACITY];
    uint16_t bus_peak_right[KTECH_BUS_CAPACITY];
    uint8_t scene;
    uint8_t soundbank_count;
    bool audible;
    bool arrangement;
    bool offline;
    bool pending;
    bool paused;
    bool stopped;
} ktech_audio_status;

void ktech_audio_options_init(ktech_audio_options *options);
ktech_audio *ktech_audio_create(const ktech_project *project,
                                const ktech_audio_options *options,
                                char *error, size_t error_size);
void ktech_audio_free(ktech_audio *audio);

/* Compile and publish an edit or scene. BAR waits for the final step of the
 * current four-beat bar before queueing a next-row replacement. Only one
 * unacknowledged snapshot is retained; a second request returns false. */
bool ktech_audio_publish(ktech_audio *audio, const ktech_project *project,
                         uint8_t scene, ktech_launch_quantization boundary,
                         bool restart, char *error, size_t error_size);
bool ktech_audio_publish_arrangement(
    ktech_audio *audio, const ktech_project *project,
    ktech_launch_quantization boundary, bool restart,
    char *error, size_t error_size);
bool ktech_audio_update_mix(ktech_audio *audio,
                            const ktech_project *project, uint8_t scene);
void ktech_audio_update_dsp(ktech_audio *audio,
                            const ktech_project *project);

bool ktech_audio_toggle_play(ktech_audio *audio);
bool ktech_audio_stop(ktech_audio *audio);
bool ktech_audio_restart(ktech_audio *audio);
bool ktech_audio_audition(ktech_audio *audio, ktech_role role, uint8_t note,
                          char *error, size_t error_size);

/* Reclaim acknowledged snapshots, queue deferred bar launches, and degrade a
 * dead sink to editable no-audio state. Call from the control thread. */
void ktech_audio_poll(ktech_audio *audio);
void ktech_audio_get_status(ktech_audio *audio, ktech_audio_status *status);

/* Pull from an offline instance. `dst` contains frames * 2 samples. */
void ktech_audio_mix(ktech_audio *audio, int16_t *dst, size_t frames);

#ifdef __cplusplus
}
#endif

#endif
