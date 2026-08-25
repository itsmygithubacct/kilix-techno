#include "kilix_techno/audio.h"

#include "kilix_techno/compile.h"
#include "kilix_techno/dsp.h"
#include "kilix_techno/soundbank.h"
#include "kilix_techno/workstation.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { AUDIO_SCRATCH_FRAMES = 1024 };

struct ktech_audio {
    chipseq sequences[KTECH_BUS_CAPACITY];
    pcmmix mixer;
    ktech_dsp dsp;
    ktech_fx_rack bus_fx[KTECH_BUS_CAPACITY];
    pthread_mutex_t control_lock;
    ktech_compiled *active;
    ktech_compiled *pending;
    ktech_project *controls;
    ktech_project *pending_controls;
    ktech_soundbank *soundbank;
    ktech_compiled *auditions[KTECH_ROLE_COUNT];
    uint32_t generation;
    uint32_t pending_generation;
    uint32_t sample_rate;
    uint32_t latency_ms;
    uint64_t queue_failures;
    uint64_t control_row;
    pcmmix_stats final_transport;
    chipseq_channel_mix last_mix[KTECH_BUS_CAPACITY];
    atomic_uint tempo;
    atomic_uint level;
    atomic_uint tone;
    atomic_uint drive;
    atomic_uint delay_amount;
    atomic_uint peak_left;
    atomic_uint peak_right;
    atomic_uint bus_peak_left[KTECH_BUS_CAPACITY];
    atomic_uint bus_peak_right[KTECH_BUS_CAPACITY];
    atomic_uint bus_count;
    int16_t scratch[AUDIO_SCRATCH_FRAMES * 2u];
    int16_t bus_scratch[KTECH_BUS_CAPACITY][AUDIO_SCRATCH_FRAMES * 2u];
    uint8_t scene;
    uint8_t pending_scene;
    bool sequence_live[KTECH_BUS_CAPACITY];
    bool mixer_live;
    bool offline;
    bool bar_wait;
    bool pending_queued;
    bool pending_restart;
    bool paused;
    bool stopped;
    bool mix_valid[KTECH_BUS_CAPACITY];
    bool arrangement;
    bool pending_arrangement;
    bool control_row_valid;
    bool bypass_master;
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
    options->arrangement = false;
    options->bypass_master = false;
}

static void load_dsp_controls(ktech_audio *audio,
                              const ktech_project *project, uint16_t row)
{
    atomic_store_explicit(&audio->tempo, project->tempo,
                          memory_order_release);
    atomic_store_explicit(&audio->level, (unsigned)ktech_control_evaluate(
                              project, KTECH_AUTOMATION_MASTER_LEVEL, 0u, row,
                              project->master_level),
                          memory_order_release);
    atomic_store_explicit(&audio->tone, (unsigned)ktech_control_evaluate(
                              project, KTECH_AUTOMATION_MASTER_TONE, 0u, row,
                              project->master_tone),
                          memory_order_release);
    atomic_store_explicit(&audio->drive, (unsigned)ktech_control_evaluate(
                              project, KTECH_AUTOMATION_MASTER_DRIVE, 0u, row,
                              project->master_drive),
                          memory_order_release);
    atomic_store_explicit(&audio->delay_amount,
                          (unsigned)ktech_control_evaluate(
                              project, KTECH_AUTOMATION_MASTER_DELAY, 0u, row,
                              project->master_delay),
                          memory_order_release);
    atomic_store_explicit(&audio->bus_count, project->bus_count,
                          memory_order_release);
    static const ktech_bus empty_bus = {0};
    unsigned effect_index = 0u;
    for (uint8_t bus = 0u; bus < KTECH_BUS_CAPACITY; ++bus) {
        if (bus >= project->bus_count) {
            ktech_fx_rack_configure(&audio->bus_fx[bus], &empty_bus);
            continue;
        }
        ktech_bus configured = project->buses[bus];
        for (uint8_t effect = 0u; effect < configured.effect_count; ++effect) {
            configured.effects[effect].wet = (uint8_t)ktech_control_evaluate(
                project, KTECH_AUTOMATION_EFFECT_WET,
                (uint8_t)effect_index, row, configured.effects[effect].wet);
            ++effect_index;
        }
        ktech_fx_rack_configure(&audio->bus_fx[bus], &configured);
    }
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
    uint16_t bus_peak_left[KTECH_BUS_CAPACITY] = {0};
    uint16_t bus_peak_right[KTECH_BUS_CAPACITY] = {0};
    configure_callback_dsp(audio);
    while (done < frames) {
        size_t chunk = frames - done;
        if (chunk > AUDIO_SCRATCH_FRAMES) chunk = AUDIO_SCRATCH_FRAMES;
        memset(audio->scratch, 0, chunk * 2u * sizeof audio->scratch[0]);
        unsigned bus_count = atomic_load_explicit(&audio->bus_count,
                                                  memory_order_acquire);
        if (bus_count == 0u || bus_count > KTECH_BUS_CAPACITY) bus_count = 1u;
        for (unsigned bus = 0u; bus < KTECH_BUS_CAPACITY; ++bus) {
            chipseq_render_s16_stereo(&audio->sequences[bus],
                                      audio->bus_scratch[bus], chunk);
            if (bus >= bus_count) continue;
            ktech_fx_rack_process(&audio->bus_fx[bus],
                                  audio->bus_scratch[bus], chunk);
            for (size_t sample = 0u; sample < chunk * 2u; ++sample) {
                int32_t bus_sample = audio->bus_scratch[bus][sample];
                uint16_t magnitude = (uint16_t)(bus_sample < 0 ?
                                                -bus_sample : bus_sample);
                if (sample % 2u == 0u) {
                    if (magnitude > bus_peak_left[bus])
                        bus_peak_left[bus] = magnitude;
                } else if (magnitude > bus_peak_right[bus]) {
                    bus_peak_right[bus] = magnitude;
                }
                int32_t mixed = (int32_t)audio->scratch[sample] +
                                audio->bus_scratch[bus][sample];
                if (mixed > INT16_MAX) mixed = INT16_MAX;
                if (mixed < INT16_MIN) mixed = INT16_MIN;
                audio->scratch[sample] = (int16_t)mixed;
            }
        }
        if (!audio->bypass_master)
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
    for (unsigned bus = 0u; bus < KTECH_BUS_CAPACITY; ++bus) {
        atomic_store_explicit(&audio->bus_peak_left[bus], bus_peak_left[bus],
                              memory_order_release);
        atomic_store_explicit(&audio->bus_peak_right[bus],
                              bus_peak_right[bus], memory_order_release);
    }
}

static bool apply_mix(ktech_audio *audio, const ktech_project *project,
                      const ktech_scene *scene, uint16_t row)
{
    if (!audio->mixer_live) return true;
    bool any_solo = false;
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track)
        any_solo = any_solo || scene->tracks[track].solo != 0u;
    bool any_bus_solo = false;
    for (uint8_t bus = 0u; bus < project->bus_count; ++bus)
        any_bus_solo = any_bus_solo || project->buses[bus].solo != 0u;
    bool success = true;
    for (uint8_t bus = 0u; bus < KTECH_BUS_CAPACITY; ++bus) {
        chipseq_channel_mix mix;
        chipseq_channel_mix_init(&mix);
        bool active_bus = bus < project->bus_count;
        const ktech_bus *target = active_bus ? &project->buses[bus] : NULL;
        for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
            const ktech_track *source = &scene->tracks[track];
            unsigned track_level = (unsigned)ktech_control_evaluate(
                project, KTECH_AUTOMATION_TRACK_LEVEL, track, row,
                source->level);
            unsigned bus_level = active_bus ?
                (unsigned)ktech_control_evaluate(
                    project, KTECH_AUTOMATION_BUS_LEVEL, bus, row,
                    target->level) : 0u;
            unsigned gain = track_level * bus_level;
            mix.gain[track] = (float)gain * (1.0f / 10000.0f);
            int pan = ktech_control_evaluate(
                          project, KTECH_AUTOMATION_TRACK_PAN, track, row,
                          source->pan) +
                      (active_bus ? (int)target->pan - 128 : 0);
            if (pan < 0) pan = 0;
            if (pan > 255) pan = 255;
            mix.pan[track] = (uint8_t)pan;
            if (!active_bus || project->track_bus[track] != bus ||
                source->mute != 0u || (any_solo && source->solo == 0u) ||
                target->mute != 0u ||
                (any_bus_solo && target->solo == 0u))
                mix.mute_mask |= (uint8_t)(1u << track);
        }
        if (audio->mix_valid[bus] &&
            memcmp(&mix, &audio->last_mix[bus], sizeof mix) == 0)
            continue;
        if (!chipseq_music_set_channel_mix(&audio->sequences[bus], &mix)) {
            ++audio->queue_failures;
            success = false;
            continue;
        }
        audio->last_mix[bus] = mix;
        audio->mix_valid[bus] = true;
    }
    return success;
}

static uint8_t arrangement_scene_at(const ktech_project *project,
                                    uint64_t row)
{
    if (!project || project->arrangement_count == 0u)
        return project ? project->active_scene : 0u;
    uint32_t total_bars = 0u;
    for (uint16_t index = 0u; index < project->arrangement_count; ++index)
        total_bars += project->arrangement[index].bars;
    if (total_bars == 0u) return project->arrangement[0].scene;
    uint32_t bar = (uint32_t)((row / 16u) % total_bars);
    for (uint16_t index = 0u; index < project->arrangement_count; ++index) {
        const ktech_arrangement_entry *entry = &project->arrangement[index];
        if (bar < entry->bars) return entry->scene;
        bar -= entry->bars;
    }
    return project->arrangement[0].scene;
}

static bool synchronize_controls(ktech_audio *audio, uint64_t row,
                                 bool force)
{
    if (!audio || !audio->controls ||
        (!force && audio->control_row_valid && audio->control_row == row))
        return true;
    uint16_t automation_row = (uint16_t)row;
    uint8_t scene = audio->arrangement ?
        arrangement_scene_at(audio->controls, row) : audio->scene;
    if (scene >= KTECH_SCENE_COUNT) scene = audio->controls->active_scene;
    load_dsp_controls(audio, audio->controls, automation_row);
    bool success = apply_mix(audio, audio->controls,
                             &audio->controls->scenes[scene],
                             automation_row);
    audio->control_row = row;
    audio->control_row_valid = true;
    return success;
}

static bool start_sequence(ktech_audio *audio, const ktech_project *project,
                           char *error, size_t error_size)
{
    bool success = true;
    for (uint8_t bus = 0u; bus < KTECH_BUS_CAPACITY; ++bus)
        if (!chipseq_music_set_swing(&audio->sequences[bus], project->swing) ||
            !chipseq_music_play(&audio->sequences[bus],
                                ktech_compiled_song(audio->active), true,
                                error, error_size)) success = false;
    if (!success || !synchronize_controls(audio, 0u, true)) {
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
        options->latency_ms < 5u || options->latency_ms > 1000u ||
        (options->arrangement && project->arrangement_count == 0u)) {
        set_error(error, error_size, "invalid audio options");
        return NULL;
    }
    ktech_audio *audio = calloc(1u, sizeof *audio);
    if (!audio) {
        set_error(error, error_size, "cannot allocate audio state");
        return NULL;
    }
    if (pthread_mutex_init(&audio->control_lock, NULL) != 0) {
        free(audio);
        set_error(error, error_size, "cannot initialize control lock");
        return NULL;
    }
    audio->sample_rate = options->sample_rate;
    audio->latency_ms = options->latency_ms;
    audio->offline = options->offline;
    audio->bypass_master = options->bypass_master;
    audio->scene = options->arrangement ? project->arrangement[0].scene :
                                          project->active_scene;
    audio->controls = malloc(sizeof *audio->controls);
    if (!audio->controls) {
        (void)pthread_mutex_destroy(&audio->control_lock);
        free(audio);
        set_error(error, error_size, "cannot allocate control snapshot");
        return NULL;
    }
    *audio->controls = *project;
    atomic_init(&audio->tempo, project->tempo);
    atomic_init(&audio->level, project->master_level);
    atomic_init(&audio->tone, project->master_tone);
    atomic_init(&audio->drive, project->master_drive);
    atomic_init(&audio->delay_amount, project->master_delay);
    atomic_init(&audio->peak_left, 0u);
    atomic_init(&audio->peak_right, 0u);
    atomic_init(&audio->bus_count, project->bus_count);
    for (uint8_t bus = 0u; bus < KTECH_BUS_CAPACITY; ++bus) {
        atomic_init(&audio->bus_peak_left[bus], 0u);
        atomic_init(&audio->bus_peak_right[bus], 0u);
    }
    char bank_warning[160];
    audio->soundbank = ktech_soundbank_load(options->sample_rate,
                                             bank_warning,
                                             sizeof bank_warning);
    audio->active = options->arrangement ?
        ktech_compile_arrangement_with_bank(project, audio->soundbank,
                                            error, error_size) :
        ktech_compile_scene_with_bank(project, audio->scene,
                                      audio->soundbank, error, error_size);
    audio->arrangement = options->arrangement;
    if (!audio->active || !ktech_dsp_init(&audio->dsp,
                                           options->sample_rate)) {
        ktech_compiled_free(audio->active);
        ktech_soundbank_free(audio->soundbank);
        free(audio->controls);
        (void)pthread_mutex_destroy(&audio->control_lock);
        free(audio);
        set_error(error, error_size, "cannot initialize audio renderer");
        return NULL;
    }
    chipseq_options sequence_options;
    chipseq_options_init(&sequence_options);
    sequence_options.sample_rate = options->sample_rate;
    sequence_options.oversample = 2u;
    bool renderers_ready = true;
    for (uint8_t bus = 0u; bus < KTECH_BUS_CAPACITY; ++bus) {
        if (!ktech_fx_rack_init(&audio->bus_fx[bus], options->sample_rate) ||
            !chipseq_init(&audio->sequences[bus], &sequence_options)) {
            renderers_ready = false;
            break;
        }
        audio->sequence_live[bus] = true;
    }
    if (!renderers_ready) {
        for (uint8_t bus = 0u; bus < KTECH_BUS_CAPACITY; ++bus) {
            if (audio->sequence_live[bus])
                chipseq_shutdown(&audio->sequences[bus]);
            ktech_fx_rack_free(&audio->bus_fx[bus]);
        }
        ktech_dsp_free(&audio->dsp);
        ktech_compiled_free(audio->active);
        ktech_soundbank_free(audio->soundbank);
        free(audio->controls);
        (void)pthread_mutex_destroy(&audio->control_lock);
        free(audio);
        set_error(error, error_size, "cannot initialize sequencer");
        return NULL;
    }
    load_dsp_controls(audio, project, 0u);

    pcmmix_options mixer_options;
    pcmmix_options_init(&mixer_options);
    mixer_options.sample_rate = options->sample_rate;
    mixer_options.output_channels = 2u;
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
    bool success = true;
    for (uint8_t bus = 0u; bus < KTECH_BUS_CAPACITY; ++bus)
        if (!chipseq_music_replace(
                &audio->sequences[bus], ktech_compiled_song(audio->pending),
                true, boundary, restart ? CHIPSEQ_REPLACE_RESTART :
                                          CHIPSEQ_REPLACE_PRESERVE_POSITION,
                audio->generation, error, error_size)) success = false;
    if (!success) {
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
    ktech_project *candidate_controls = malloc(sizeof *candidate_controls);
    if (!candidate_controls) {
        ktech_compiled_free(candidate);
        set_error(error, error_size, "cannot allocate control snapshot");
        return false;
    }
    *candidate_controls = *project;
    if (!audio->mixer_live) {
        ktech_compiled_free(audio->active);
        free(audio->controls);
        audio->active = candidate;
        audio->controls = candidate_controls;
        audio->scene = scene;
        audio->arrangement = false;
        audio->control_row_valid = false;
        return true;
    }
    audio->pending = candidate;
    audio->pending_controls = candidate_controls;
    audio->pending_scene = scene;
    audio->pending_arrangement = false;
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
        free(audio->pending_controls);
        audio->pending_controls = NULL;
        return false;
    }
    return true;
}

bool ktech_audio_publish_arrangement(
    ktech_audio *audio, const ktech_project *project,
    ktech_launch_quantization boundary, bool restart,
    char *error, size_t error_size)
{
    if (!audio || !project || boundary >= KTECH_LAUNCH_COUNT ||
        audio->pending || project->arrangement_count == 0u) {
        set_error(error, error_size, audio && audio->pending ?
                  "an audio snapshot is still pending" :
                  "invalid arrangement publication");
        return false;
    }
    ktech_compiled *candidate = ktech_compile_arrangement_with_bank(
        project, audio->soundbank, error, error_size);
    if (!candidate) return false;
    ktech_project *candidate_controls = malloc(sizeof *candidate_controls);
    if (!candidate_controls) {
        ktech_compiled_free(candidate);
        set_error(error, error_size, "cannot allocate control snapshot");
        return false;
    }
    *candidate_controls = *project;
    uint8_t scene = project->arrangement[0].scene;
    if (!audio->mixer_live) {
        ktech_compiled_free(audio->active);
        free(audio->controls);
        audio->active = candidate;
        audio->controls = candidate_controls;
        audio->scene = scene;
        audio->arrangement = true;
        audio->control_row_valid = false;
        return true;
    }
    audio->pending = candidate;
    audio->pending_controls = candidate_controls;
    audio->pending_scene = scene;
    audio->pending_arrangement = true;
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
        free(audio->pending_controls);
        audio->pending_controls = NULL;
        return false;
    }
    return true;
}

bool ktech_audio_update_mix(ktech_audio *audio,
                            const ktech_project *project, uint8_t scene)
{
    if (!audio || !project || !audio->controls ||
        scene >= KTECH_SCENE_COUNT)
        return false;
    (void)pthread_mutex_lock(&audio->control_lock);
    *audio->controls = *project;
    audio->scene = scene;
    bool success = synchronize_controls(audio, audio->control_row, true);
    (void)pthread_mutex_unlock(&audio->control_lock);
    return success;
}

void ktech_audio_update_dsp(ktech_audio *audio,
                            const ktech_project *project)
{
    if (audio && project && audio->controls) {
        (void)pthread_mutex_lock(&audio->control_lock);
        *audio->controls = *project;
        (void)synchronize_controls(audio, audio->control_row, true);
        (void)pthread_mutex_unlock(&audio->control_lock);
    }
}

bool ktech_audio_toggle_play(ktech_audio *audio)
{
    if (!audio || !audio->mixer_live) return false;
    if (audio->stopped) {
        char error[96];
        bool success = true;
        for (uint8_t bus = 0u; bus < KTECH_BUS_CAPACITY; ++bus)
            if (!chipseq_music_play(&audio->sequences[bus],
                                    ktech_compiled_song(audio->active), true,
                                    error, sizeof error)) success = false;
        if (!success) {
            ++audio->queue_failures;
            return false;
        }
        audio->stopped = false;
        audio->paused = false;
        return true;
    }
    audio->paused = !audio->paused;
    bool success = true;
    for (uint8_t bus = 0u; bus < KTECH_BUS_CAPACITY; ++bus)
        if (!chipseq_music_set_paused(&audio->sequences[bus], audio->paused))
            success = false;
    if (!success) {
        audio->paused = !audio->paused;
        ++audio->queue_failures;
        return false;
    }
    return true;
}

bool ktech_audio_stop(ktech_audio *audio)
{
    if (!audio || !audio->mixer_live) return false;
    bool success = true;
    for (uint8_t bus = 0u; bus < KTECH_BUS_CAPACITY; ++bus)
        if (!chipseq_music_stop(&audio->sequences[bus], 0u)) success = false;
    if (!success) {
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
    bool success = true;
    for (uint8_t bus = 0u; bus < KTECH_BUS_CAPACITY; ++bus)
        if (!chipseq_music_stop(&audio->sequences[bus], 0u) ||
            !chipseq_music_play(&audio->sequences[bus],
                                ktech_compiled_song(audio->active), true,
                                error, sizeof error)) success = false;
    if (!success) {
        ++audio->queue_failures;
        return false;
    }
    audio->stopped = false;
    audio->paused = false;
    return true;
}

bool ktech_audio_audition(ktech_audio *audio, ktech_role role, uint8_t note,
                          char *error, size_t error_size)
{
    if (!audio || role >= KTECH_ROLE_COUNT || note > 127u ||
        !audio->sequence_live[0]) {
        set_error(error, error_size, "audio preview is unavailable");
        return false;
    }
    if (!audio->auditions[role]) {
        uint16_t tempo = (uint16_t)atomic_load_explicit(
            &audio->tempo, memory_order_acquire);
        audio->auditions[role] = ktech_compile_audition(
            role, 60u, tempo, audio->soundbank, error, error_size);
        if (!audio->auditions[role]) return false;
    }
    int handle = chipseq_sfx_play(
        &audio->sequences[0], ktech_compiled_song(audio->auditions[role]),
        0.85f, (int)note - 60, false);
    if (handle <= 0) {
        ++audio->queue_failures;
        set_error(error, error_size, "audio preview queue is busy");
        return false;
    }
    if (error && error_size > 0u) error[0] = '\0';
    return true;
}

static void degrade_sink(ktech_audio *audio)
{
    pcmmix_get_stats(&audio->mixer, &audio->final_transport);
    pcmmix_stop(&audio->mixer);
    audio->mixer_live = false;
    for (uint8_t bus = 0u; bus < KTECH_BUS_CAPACITY; ++bus) {
        if (audio->sequence_live[bus])
            chipseq_shutdown(&audio->sequences[bus]);
        audio->sequence_live[bus] = false;
    }
    if (audio->pending) {
        ktech_compiled_free(audio->active);
        free(audio->controls);
        audio->active = audio->pending;
        audio->controls = audio->pending_controls;
        audio->scene = audio->pending_scene;
        audio->arrangement = audio->pending_arrangement;
        audio->pending = NULL;
        audio->pending_controls = NULL;
        audio->control_row_valid = false;
    }
    audio->pending_generation = 0u;
    audio->pending_queued = false;
    audio->bar_wait = false;
}

void ktech_audio_poll(ktech_audio *audio)
{
    if (!audio || !audio->mixer_live) return;
    (void)pthread_mutex_lock(&audio->control_lock);
    if (!audio->offline && !pcmmix_is_running(&audio->mixer)) {
        degrade_sink(audio);
        (void)pthread_mutex_unlock(&audio->control_lock);
        return;
    }
    chipseq_playhead playhead;
    bool have_playhead = chipseq_music_snapshot(&audio->sequences[0],
                                                &playhead);
    if (audio->bar_wait && have_playhead && playhead.active &&
        playhead.rows_elapsed % 16u == 15u) {
        char error[96];
        if (!queue_pending(audio, CHIPSEQ_REPLACE_NEXT_ROW,
                           audio->pending_restart,
                           error, sizeof error)) {
            ktech_compiled_free(audio->pending);
            audio->pending = NULL;
            free(audio->pending_controls);
            audio->pending_controls = NULL;
            audio->bar_wait = false;
        }
    }
    bool all_applied = audio->pending && audio->pending_queued;
    if (all_applied) {
        for (uint8_t bus = 0u; bus < KTECH_BUS_CAPACITY; ++bus) {
            chipseq_playhead bus_playhead;
            if (!chipseq_music_snapshot(&audio->sequences[bus],
                                        &bus_playhead) ||
                bus_playhead.applied_generation <
                    audio->pending_generation) {
                all_applied = false;
                break;
            }
        }
    }
    if (all_applied) {
        ktech_compiled_free(audio->active);
        free(audio->controls);
        audio->active = audio->pending;
        audio->controls = audio->pending_controls;
        audio->pending = NULL;
        audio->pending_controls = NULL;
        audio->scene = audio->pending_scene;
        audio->arrangement = audio->pending_arrangement;
        audio->pending_generation = 0u;
        audio->pending_queued = false;
        audio->control_row_valid = false;
    }
    if (have_playhead)
        (void)synchronize_controls(audio, playhead.rows_elapsed, false);
    (void)pthread_mutex_unlock(&audio->control_lock);
}

void ktech_audio_get_status(ktech_audio *audio, ktech_audio_status *status)
{
    if (!status) return;
    memset(status, 0, sizeof *status);
    if (!audio) return;
    status->scene = audio->scene;
    status->arrangement = audio->arrangement;
    status->soundbank_count = ktech_soundbank_installed_count(
        audio->soundbank);
    status->soundbank_mask = ktech_soundbank_installed_mask(audio->soundbank);
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
    for (uint8_t bus = 0u; bus < KTECH_BUS_CAPACITY; ++bus) {
        status->bus_peak_left[bus] = (uint16_t)atomic_load_explicit(
            &audio->bus_peak_left[bus], memory_order_acquire);
        status->bus_peak_right[bus] = (uint16_t)atomic_load_explicit(
            &audio->bus_peak_right[bus], memory_order_acquire);
    }
    if (audio->mixer_live) {
        status->audible = !audio->offline && pcmmix_is_running(&audio->mixer);
        status->backend = pcmmix_backend_name(&audio->mixer);
        pcmmix_get_stats(&audio->mixer, &status->transport);
        (void)chipseq_music_snapshot(&audio->sequences[0], &status->playhead);
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
    for (uint8_t bus = 0u; bus < KTECH_BUS_CAPACITY; ++bus) {
        if (audio->sequence_live[bus])
            chipseq_shutdown(&audio->sequences[bus]);
        ktech_fx_rack_free(&audio->bus_fx[bus]);
    }
    ktech_dsp_free(&audio->dsp);
    for (unsigned role = 0u; role < KTECH_ROLE_COUNT; ++role)
        ktech_compiled_free(audio->auditions[role]);
    ktech_compiled_free(audio->pending);
    ktech_compiled_free(audio->active);
    free(audio->pending_controls);
    free(audio->controls);
    ktech_soundbank_free(audio->soundbank);
    (void)pthread_mutex_destroy(&audio->control_lock);
    memset(audio, 0, sizeof *audio);
    free(audio);
}
