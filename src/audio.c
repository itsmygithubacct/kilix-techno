#include "kilix_techno/audio.h"

#include "kilix_techno/compile.h"
#include "kilix_techno/dsp.h"
#include "kilix_techno/soundbank.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { AUDIO_SCRATCH_FRAMES = 1024 };

struct ktech_audio {
    chipseq sequence;
    pcmmix mixer;
    ktech_dsp dsp;
    ktech_compiled *active;
    ktech_compiled *pending;
    ktech_soundbank *soundbank;
    uint32_t generation;
    uint32_t pending_generation;
    uint32_t sample_rate;
    uint32_t latency_ms;
    uint64_t queue_failures;
    pcmmix_stats final_transport;
    chipseq_channel_mix last_mix;
    atomic_uint tempo;
    atomic_uint level;
    atomic_uint tone;
    atomic_uint drive;
    atomic_uint delay_amount;
    atomic_uint peak_left;
    atomic_uint peak_right;
    int16_t scratch[AUDIO_SCRATCH_FRAMES * 2u];
    uint8_t scene;
    uint8_t pending_scene;
    bool sequence_live;
    bool mixer_live;
    bool offline;
    bool bar_wait;
    bool pending_queued;
    bool pending_restart;
    bool paused;
    bool stopped;
    bool mix_valid;
};

static void set_error(char *error, size_t size, const char *format, ...)
{
    va_list arguments;
    if (!error || size == 0u) return;
    va_start(arguments, format);
    (void)vsnprintf(error, size, format, arguments);
    va_end(arguments);
}

void ktech_audio_options_init(ktech_audio_options *options)
{
    if (!options) return;
    options->sample_rate = 44100u;
    options->latency_ms = 24u;
    options->offline = false;
}

static void load_dsp_controls(ktech_audio *audio,
                              const ktech_project *project)
{
    atomic_store_explicit(&audio->tempo, project->tempo,
                          memory_order_release);
    atomic_store_explicit(&audio->level, project->master_level,
                          memory_order_release);
    atomic_store_explicit(&audio->tone, project->master_tone,
                          memory_order_release);
    atomic_store_explicit(&audio->drive, project->master_drive,
                          memory_order_release);
    atomic_store_explicit(&audio->delay_amount, project->master_delay,
                          memory_order_release);
}

static void configure_callback_dsp(ktech_audio *audio)
{
    audio->dsp.tempo = (uint16_t)atomic_load_explicit(
        &audio->tempo, memory_order_acquire);
    audio->dsp.level = (uint8_t)atomic_load_explicit(
        &audio->level, memory_order_acquire);
    audio->dsp.tone = (uint8_t)atomic_load_explicit(
        &audio->tone, memory_order_acquire);
    audio->dsp.drive = (uint8_t)atomic_load_explicit(
        &audio->drive, memory_order_acquire);
    audio->dsp.delay_amount = (uint8_t)atomic_load_explicit(
        &audio->delay_amount, memory_order_acquire);
}

static void audio_generator(float *destination, size_t frames,
                            uint32_t channels, void *user)
{
    ktech_audio *audio = user;
    size_t done = 0u;
    uint16_t peak_left = 0u;
    uint16_t peak_right = 0u;
    configure_callback_dsp(audio);
    while (done < frames) {
        size_t chunk = frames - done;
        if (chunk > AUDIO_SCRATCH_FRAMES) chunk = AUDIO_SCRATCH_FRAMES;
        chipseq_render_s16_stereo(&audio->sequence, audio->scratch, chunk);
        ktech_dsp_process(&audio->dsp, audio->scratch, chunk);
        for (size_t frame = 0u; frame < chunk; ++frame) {
            int32_t left = audio->scratch[frame * 2u];
            int32_t right = audio->scratch[frame * 2u + 1u];
            uint16_t left_level = (uint16_t)(left < 0 ? -left : left);
            uint16_t right_level = (uint16_t)(right < 0 ? -right : right);
            if (left_level > peak_left) peak_left = left_level;
            if (right_level > peak_right) peak_right = right_level;
            if (channels == 2u) {
                destination[(done + frame) * 2u] +=
                    (float)left * (1.0f / 32768.0f);
                destination[(done + frame) * 2u + 1u] +=
                    (float)right * (1.0f / 32768.0f);
            } else {
                destination[done + frame] +=
                    (float)(left + right) * (1.0f / 65536.0f);
            }
        }
        done += chunk;
    }
    atomic_store_explicit(&audio->peak_left, peak_left,
                          memory_order_release);
    atomic_store_explicit(&audio->peak_right, peak_right,
                          memory_order_release);
}

static bool apply_mix(ktech_audio *audio, const ktech_scene *scene)
{
    if (!audio->mixer_live) return true;
    chipseq_channel_mix mix;
    chipseq_channel_mix_init(&mix);
    bool any_solo = false;
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track)
        any_solo = any_solo || scene->tracks[track].solo != 0u;
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
        const ktech_track *source = &scene->tracks[track];
        mix.gain[track] = (float)source->level * (1.0f / 100.0f);
        mix.pan[track] = source->pan;
        if (source->mute != 0u || (any_solo && source->solo == 0u))
            mix.mute_mask |= (uint8_t)(1u << track);
    }
    if (audio->mix_valid &&
        memcmp(&mix, &audio->last_mix, sizeof mix) == 0)
        return true;
    if (!chipseq_music_set_channel_mix(&audio->sequence, &mix)) {
        ++audio->queue_failures;
        return false;
    }
    audio->last_mix = mix;
    audio->mix_valid = true;
    return true;
}

static bool start_sequence(ktech_audio *audio, const ktech_project *project,
                           char *error, size_t error_size)
{
    if (!chipseq_music_set_swing(&audio->sequence, project->swing) ||
        !chipseq_music_play(&audio->sequence,
                            ktech_compiled_song(audio->active), true,
                            error, error_size) ||
        !apply_mix(audio, &project->scenes[audio->scene])) {
        set_error(error, error_size, "cannot queue initial audio state");
        return false;
    }
    return true;
}

ktech_audio *ktech_audio_create(const ktech_project *project,
                                const ktech_audio_options *options_value,
                                char *error, size_t error_size)
{
    ktech_audio_options defaults;
    ktech_audio_options_init(&defaults);
    const ktech_audio_options *options = options_value ? options_value :
                                                           &defaults;
    if (!project || !ktech_project_validate(project, error, error_size) ||
        options->sample_rate < 8000u || options->sample_rate > 192000u ||
        options->latency_ms < 5u || options->latency_ms > 1000u) {
        set_error(error, error_size, "invalid audio options");
        return NULL;
    }
    ktech_audio *audio = calloc(1u, sizeof *audio);
    if (!audio) {
        set_error(error, error_size, "cannot allocate audio state");
        return NULL;
    }
    audio->sample_rate = options->sample_rate;
    audio->latency_ms = options->latency_ms;
    audio->offline = options->offline;
    audio->scene = project->active_scene;
    atomic_init(&audio->tempo, project->tempo);
    atomic_init(&audio->level, project->master_level);
    atomic_init(&audio->tone, project->master_tone);
    atomic_init(&audio->drive, project->master_drive);
    atomic_init(&audio->delay_amount, project->master_delay);
    atomic_init(&audio->peak_left, 0u);
    atomic_init(&audio->peak_right, 0u);
    char bank_warning[160];
    audio->soundbank = ktech_soundbank_load(options->sample_rate,
                                             bank_warning,
                                             sizeof bank_warning);
    audio->active = ktech_compile_scene_with_bank(
        project, audio->scene, audio->soundbank, error, error_size);
    if (!audio->active || !ktech_dsp_init(&audio->dsp,
                                           options->sample_rate)) {
        ktech_compiled_free(audio->active);
        ktech_soundbank_free(audio->soundbank);
        free(audio);
        set_error(error, error_size, "cannot initialize audio renderer");
        return NULL;
    }
    chipseq_options sequence_options;
    chipseq_options_init(&sequence_options);
    sequence_options.sample_rate = options->sample_rate;
    sequence_options.oversample = 2u;
    if (!chipseq_init(&audio->sequence, &sequence_options)) {
        ktech_dsp_free(&audio->dsp);
        ktech_compiled_free(audio->active);
        ktech_soundbank_free(audio->soundbank);
        free(audio);
        set_error(error, error_size, "cannot initialize sequencer");
        return NULL;
    }
    audio->sequence_live = true;

    pcmmix_options mixer_options;
    pcmmix_options_init(&mixer_options);
    mixer_options.sample_rate = options->sample_rate;
    mixer_options.channels = 2u;
    mixer_options.latency_ms = options->latency_ms;
    mixer_options.soft_clip = false;
    mixer_options.offline = options->offline;
    if (pcmmix_start(&audio->mixer, &mixer_options)) {
        audio->mixer_live = true;
        pcmmix_set_generator_interleaved(&audio->mixer, audio_generator,
                                         audio);
        if (!start_sequence(audio, project, error, error_size)) {
            ktech_audio_free(audio);
            return NULL;
        }
    }
    if (error && error_size) error[0] = '\0';
    return audio;
}

static bool queue_pending(ktech_audio *audio,
                          chipseq_replace_boundary boundary, bool restart,
                          char *error, size_t error_size)
{
    ++audio->generation;
    if (audio->generation == 0u) ++audio->generation;
    if (!chipseq_music_replace(
            &audio->sequence, ktech_compiled_song(audio->pending), true,
            boundary, restart ? CHIPSEQ_REPLACE_RESTART :
                                CHIPSEQ_REPLACE_PRESERVE_POSITION,
            audio->generation, error, error_size)) {
        ++audio->queue_failures;
        return false;
    }
    audio->pending_generation = audio->generation;
    audio->pending_queued = true;
    audio->bar_wait = false;
    return true;
}

bool ktech_audio_publish(ktech_audio *audio, const ktech_project *project,
                         uint8_t scene, ktech_launch_quantization boundary,
                         bool restart, char *error, size_t error_size)
{
    if (!audio || !project || scene >= KTECH_SCENE_COUNT ||
        boundary >= KTECH_LAUNCH_COUNT || audio->pending) {
        set_error(error, error_size, audio && audio->pending ?
                  "an audio snapshot is still pending" :
                  "invalid audio publication");
        return false;
    }
    ktech_compiled *candidate = ktech_compile_scene_with_bank(
        project, scene, audio->soundbank, error, error_size);
    if (!candidate) return false;
    load_dsp_controls(audio, project);
    if (!audio->mixer_live) {
        ktech_compiled_free(audio->active);
        audio->active = candidate;
        audio->scene = scene;
        return true;
    }
    audio->pending = candidate;
    audio->pending_scene = scene;
    audio->pending_restart = restart;
    if (boundary == KTECH_LAUNCH_BAR) {
        audio->bar_wait = true;
        return true;
    }
    chipseq_replace_boundary sequencer_boundary =
        boundary == KTECH_LAUNCH_BEAT ? CHIPSEQ_REPLACE_NEXT_BEAT :
                                        CHIPSEQ_REPLACE_NEXT_ROW;
    if (!queue_pending(audio, sequencer_boundary, restart, error, error_size)) {
        ktech_compiled_free(audio->pending);
        audio->pending = NULL;
        return false;
    }
    return true;
}

bool ktech_audio_update_mix(ktech_audio *audio,
                            const ktech_project *project, uint8_t scene)
{
    if (!audio || !project || scene >= KTECH_SCENE_COUNT) return false;
    load_dsp_controls(audio, project);
    return apply_mix(audio, &project->scenes[scene]);
}

void ktech_audio_update_dsp(ktech_audio *audio,
                            const ktech_project *project)
{
    if (audio && project) load_dsp_controls(audio, project);
}

bool ktech_audio_toggle_play(ktech_audio *audio)
{
    if (!audio || !audio->mixer_live) return false;
    if (audio->stopped) {
        char error[96];
        if (!chipseq_music_play(&audio->sequence,
                                ktech_compiled_song(audio->active), true,
                                error, sizeof error)) {
            ++audio->queue_failures;
            return false;
        }
        audio->stopped = false;
        audio->paused = false;
        return true;
    }
    audio->paused = !audio->paused;
    if (!chipseq_music_set_paused(&audio->sequence, audio->paused)) {
        audio->paused = !audio->paused;
        ++audio->queue_failures;
        return false;
    }
    return true;
}

bool ktech_audio_stop(ktech_audio *audio)
{
    if (!audio || !audio->mixer_live) return false;
    if (!chipseq_music_stop(&audio->sequence, 0u)) {
        ++audio->queue_failures;
        return false;
    }
    audio->stopped = true;
    audio->paused = false;
    return true;
}

bool ktech_audio_restart(ktech_audio *audio)
{
    if (!audio || !audio->mixer_live) return false;
    char error[96];
    if (!chipseq_music_stop(&audio->sequence, 0u) ||
        !chipseq_music_play(&audio->sequence,
                            ktech_compiled_song(audio->active), true,
                            error, sizeof error)) {
        ++audio->queue_failures;
        return false;
    }
    audio->stopped = false;
    audio->paused = false;
    return true;
}

static void degrade_sink(ktech_audio *audio)
{
    pcmmix_get_stats(&audio->mixer, &audio->final_transport);
    pcmmix_stop(&audio->mixer);
    audio->mixer_live = false;
    chipseq_shutdown(&audio->sequence);
    audio->sequence_live = false;
    if (audio->pending) {
        ktech_compiled_free(audio->active);
        audio->active = audio->pending;
        audio->scene = audio->pending_scene;
        audio->pending = NULL;
    }
    audio->pending_generation = 0u;
    audio->pending_queued = false;
    audio->bar_wait = false;
}

void ktech_audio_poll(ktech_audio *audio)
{
    if (!audio || !audio->mixer_live) return;
    if (!audio->offline && !pcmmix_is_running(&audio->mixer)) {
        degrade_sink(audio);
        return;
    }
    chipseq_playhead playhead;
    bool have_playhead = chipseq_music_snapshot(&audio->sequence, &playhead);
    if (audio->bar_wait && have_playhead && playhead.active &&
        playhead.rows_elapsed % 16u == 15u) {
        char error[96];
        if (!queue_pending(audio, CHIPSEQ_REPLACE_NEXT_ROW,
                           audio->pending_restart,
                           error, sizeof error)) {
            ktech_compiled_free(audio->pending);
            audio->pending = NULL;
            audio->bar_wait = false;
        }
    }
    if (audio->pending && audio->pending_queued && have_playhead &&
        playhead.applied_generation >= audio->pending_generation) {
        ktech_compiled_free(audio->active);
        audio->active = audio->pending;
        audio->pending = NULL;
        audio->scene = audio->pending_scene;
        audio->pending_generation = 0u;
        audio->pending_queued = false;
    }
}

void ktech_audio_get_status(ktech_audio *audio, ktech_audio_status *status)
{
    if (!status) return;
    memset(status, 0, sizeof *status);
    if (!audio) return;
    status->scene = audio->scene;
    status->soundbank_count = ktech_soundbank_installed_count(
        audio->soundbank);
    status->offline = audio->offline;
    status->pending = audio->pending != NULL;
    status->pending_generation = audio->pending_generation;
    status->queue_failures = audio->queue_failures;
    status->sample_rate = audio->sample_rate;
    status->latency_ms = audio->latency_ms;
    status->paused = audio->paused;
    status->stopped = audio->stopped;
    status->peak_left = (uint16_t)atomic_load_explicit(
        &audio->peak_left, memory_order_acquire);
    status->peak_right = (uint16_t)atomic_load_explicit(
        &audio->peak_right, memory_order_acquire);
    if (audio->mixer_live) {
        status->audible = !audio->offline && pcmmix_is_running(&audio->mixer);
        status->backend = pcmmix_backend_name(&audio->mixer);
        pcmmix_get_stats(&audio->mixer, &status->transport);
        (void)chipseq_music_snapshot(&audio->sequence, &status->playhead);
    } else {
        status->transport = audio->final_transport;
    }
}

void ktech_audio_mix(ktech_audio *audio, int16_t *destination, size_t frames)
{
    if (!destination || frames == 0u) return;
    if (!audio || !audio->mixer_live || !audio->offline) {
        memset(destination, 0, frames * 2u * sizeof *destination);
        return;
    }
    pcmmix_mix_block(&audio->mixer, destination, frames);
}

void ktech_audio_free(ktech_audio *audio)
{
    if (!audio) return;
    if (audio->mixer_live) {
        pcmmix_get_stats(&audio->mixer, &audio->final_transport);
        pcmmix_stop(&audio->mixer);
        audio->mixer_live = false;
    }
    if (audio->sequence_live) chipseq_shutdown(&audio->sequence);
    ktech_dsp_free(&audio->dsp);
    ktech_compiled_free(audio->pending);
    ktech_compiled_free(audio->active);
    ktech_soundbank_free(audio->soundbank);
    memset(audio, 0, sizeof *audio);
    free(audio);
}
