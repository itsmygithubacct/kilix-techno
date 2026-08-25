#include "kilix_techno/export.h"

#include "kilix_techno/audio.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1u << 0)
#endif

enum { EXPORT_BLOCK_FRAMES = 4096 };

static void set_error(char *error, size_t size, const char *format, ...)
{
    va_list arguments;
    if (!error || size == 0u) return;
    va_start(arguments, format);
    (void)vsnprintf(error, size, format, arguments);
    va_end(arguments);
}

void ktech_export_options_init(ktech_export_options *options)
{
    if (!options) return;
    options->sample_rate = 44100u;
    options->bars = 4u;
    options->tail_ms = 0u;
    options->scene = 0u;
    options->bit_depth = 16u;
    options->arrangement = false;
    options->normalize = false;
    options->include_master = true;
    options->replace_existing = false;
    options->cancel = NULL;
}

bool ktech_export_options_apply_preset(ktech_export_options *options,
                                       const ktech_export_preset *preset,
                                       char *error, size_t error_size)
{
    if (!options || !preset || preset->format >= KTECH_EXPORT_FORMAT_COUNT ||
        (preset->bit_depth != 16u && preset->bit_depth != 24u &&
         preset->bit_depth != 32u) || preset->normalize > 1u ||
        preset->include_master > 1u || preset->bars == 0u ||
        preset->bars > KTECH_SONG_BAR_CAPACITY || preset->tail_ms > 10000u) {
        set_error(error, error_size, "invalid export preset");
        return false;
    }
    options->bars = preset->bars;
    options->tail_ms = preset->tail_ms;
    options->bit_depth = preset->bit_depth;
    options->normalize = preset->normalize != 0u;
    options->include_master = preset->include_master != 0u;
    if (error && error_size > 0u) error[0] = '\0';
    return true;
}

static bool valid_component(const char *component)
{
    return component[0] != '\0' && strcmp(component, ".") != 0 &&
           strcmp(component, "..") != 0;
}

static int open_parent_secure(const char *path, char *basename,
                              size_t basename_size)
{
    if (!path || path[0] != '/' || strlen(path) >= 4096u) {
        errno = EINVAL;
        return -1;
    }
    char copy[4096];
    (void)snprintf(copy, sizeof copy, "%s", path);
    char *last = strrchr(copy, '/');
    if (!last || !valid_component(last + 1u) ||
        strlen(last + 1u) >= basename_size) {
        errno = EINVAL;
        return -1;
    }
    (void)snprintf(basename, basename_size, "%s", last + 1u);
    *last = '\0';

    int directory = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) return -1;
    char *cursor = copy;
    while (*cursor == '/') ++cursor;
    while (*cursor != '\0') {
        char *slash = strchr(cursor, '/');
        if (slash) *slash = '\0';
        if (!valid_component(cursor)) {
            (void)close(directory);
            errno = EINVAL;
            return -1;
        }
        int next = openat(directory, cursor,
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next < 0) {
            (void)close(directory);
            return -1;
        }
        (void)close(directory);
        directory = next;
        if (!slash) break;
        cursor = slash + 1u;
        while (*cursor == '/') ++cursor;
    }
    return directory;
}

static void put_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value & UINT16_C(0xff));
    bytes[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value & UINT32_C(0xff));
    bytes[1] = (uint8_t)((value >> 8) & UINT32_C(0xff));
    bytes[2] = (uint8_t)((value >> 16) & UINT32_C(0xff));
    bytes[3] = (uint8_t)(value >> 24);
}

static void make_wav_header(uint8_t header[44], uint32_t sample_rate,
                            uint32_t data_bytes, uint8_t bit_depth)
{
    uint16_t frame_bytes = (uint16_t)(2u * (unsigned)bit_depth / 8u);
    memset(header, 0, 44u);
    memcpy(header, "RIFF", 4u);
    put_u32(header + 4u, data_bytes + 36u);
    memcpy(header + 8u, "WAVEfmt ", 8u);
    put_u32(header + 16u, 16u);
    put_u16(header + 20u, 1u);
    put_u16(header + 22u, 2u);
    put_u32(header + 24u, sample_rate);
    put_u32(header + 28u, sample_rate * frame_bytes);
    put_u16(header + 32u, frame_bytes);
    put_u16(header + 34u, bit_depth);
    memcpy(header + 36u, "data", 4u);
    put_u32(header + 40u, data_bytes);
}

static bool write_all(int descriptor, const void *data, size_t size)
{
    const uint8_t *bytes = data;
    while (size > 0u) {
        ssize_t count = write(descriptor, bytes, size);
        if (count > 0) {
            bytes += (size_t)count;
            size -= (size_t)count;
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

static uint64_t hash_samples(uint64_t hash, const int16_t *samples,
                             size_t count)
{
    for (size_t index = 0u; index < count; ++index) {
        uint16_t value = (uint16_t)samples[index];
        hash = (hash ^ (uint8_t)value) * UINT64_C(1099511628211);
        hash = (hash ^ (uint8_t)(value >> 8)) * UINT64_C(1099511628211);
    }
    return hash;
}

static ktech_audio *create_offline_audio(const ktech_project *project,
                                         const ktech_export_options *options,
                                         char *error, size_t error_size)
{
    ktech_audio_options audio_options;
    ktech_audio_options_init(&audio_options);
    audio_options.sample_rate = options->sample_rate;
    audio_options.offline = true;
    audio_options.arrangement = options->arrangement;
    audio_options.bypass_master = !options->include_master;
    return ktech_audio_create(project, &audio_options, error, error_size);
}

static bool measure_peak(const ktech_project *project,
                         const ktech_export_options *options,
                         uint64_t musical_frames, uint64_t frames,
                         uint32_t *peak, char *error, size_t error_size)
{
    *peak = 0u;
    ktech_audio *audio = create_offline_audio(project, options,
                                              error, error_size);
    if (!audio) return false;
    int16_t samples[EXPORT_BLOCK_FRAMES * 2u];
    uint64_t done = 0u;
    bool stopped = false;
    bool ok = true;
    while (done < frames) {
        if (options->cancel &&
            atomic_load_explicit(options->cancel, memory_order_acquire)) {
            ok = false;
            errno = ECANCELED;
            break;
        }
        if (!stopped && done == musical_frames) {
            ok = ktech_audio_stop(audio);
            stopped = true;
            if (!ok) break;
        }
        uint64_t remaining = frames - done;
        if (!stopped && done < musical_frames &&
            remaining > musical_frames - done)
            remaining = musical_frames - done;
        size_t chunk = remaining > EXPORT_BLOCK_FRAMES ?
            EXPORT_BLOCK_FRAMES : (size_t)remaining;
        ktech_audio_poll(audio);
        ktech_audio_mix(audio, samples, chunk);
        for (size_t index = 0u; index < chunk * 2u; ++index) {
            int32_t value = samples[index];
            uint32_t magnitude = (uint32_t)(value < 0 ? -value : value);
            if (magnitude > *peak) *peak = magnitude;
        }
        done += chunk;
    }
    ktech_audio_free(audio);
    if (!ok) {
        set_error(error, error_size, "normalization pass failed: %s",
                  strerror(errno ? errno : EIO));
        return false;
    }
    return true;
}

bool ktech_export_wav(const ktech_project *project, const char *absolute_path,
                      const ktech_export_options *options_value,
                      ktech_export_info *info, char *error,
                      size_t error_size)
{
    if (info) memset(info, 0, sizeof *info);
    ktech_export_options defaults;
    ktech_export_options_init(&defaults);
    const ktech_export_options *options = options_value ? options_value :
                                                           &defaults;
    if (!project || !ktech_project_validate(project, error, error_size) ||
        options->sample_rate < 8000u || options->sample_rate > 192000u ||
        options->bars == 0u || options->bars > 256u ||
        options->tail_ms > 10000u ||
        (options->bit_depth != 16u && options->bit_depth != 24u &&
         options->bit_depth != 32u) ||
        options->scene >= KTECH_SCENE_COUNT ||
        (options->arrangement && project->arrangement_count == 0u)) {
        set_error(error, error_size, "invalid export options");
        return false;
    }
    uint16_t render_bars = options->arrangement ?
        ktech_project_arrangement_bars(project) : options->bars;
    uint64_t numerator = (uint64_t)options->sample_rate * 60u * 4u *
                         render_bars;
    uint64_t musical_frames = (numerator + project->tempo - 1u) /
                              project->tempo;
    uint64_t tail_frames = ((uint64_t)options->sample_rate *
                            options->tail_ms + 999u) / 1000u;
    uint64_t frames = musical_frames + tail_frames;
    unsigned frame_bytes = 2u * (unsigned)options->bit_depth / 8u;
    if (frames > (UINT32_MAX - 36u) / frame_bytes) {
        set_error(error, error_size, "export exceeds RIFF size limit");
        return false;
    }
    uint32_t data_bytes = (uint32_t)(frames * frame_bytes);

    ktech_project *render_project = malloc(sizeof *render_project);
    if (!render_project) {
        set_error(error, error_size, "cannot allocate offline project");
        return false;
    }
    *render_project = *project;
    render_project->active_scene = options->scene;
    uint32_t normalization_peak = 0u;
    if (options->normalize &&
        !measure_peak(render_project, options, musical_frames, frames,
                      &normalization_peak, error, error_size)) {
        free(render_project);
        return false;
    }
    ktech_audio *audio = create_offline_audio(render_project, options,
                                              error, error_size);
    if (!audio) {
        free(render_project);
        return false;
    }

    char basename[256];
    int directory = open_parent_secure(absolute_path, basename,
                                       sizeof basename);
    if (directory < 0) {
        ktech_audio_free(audio);
        free(render_project);
        set_error(error, error_size, "unsafe export path: %s",
                  strerror(errno));
        return false;
    }
    struct stat status;
    if (fstatat(directory, basename, &status, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!options->replace_existing || !S_ISREG(status.st_mode)) {
            (void)close(directory);
            ktech_audio_free(audio);
            free(render_project);
            set_error(error, error_size, "%s",
                      options->replace_existing ? "unsafe export target" :
                                                  "export already exists");
            return false;
        }
    } else if (errno != ENOENT) {
        (void)close(directory);
        ktech_audio_free(audio);
        free(render_project);
        set_error(error, error_size, "cannot inspect export target");
        return false;
    }
    static atomic_uint temporary_counter;
    char temporary[320];
    unsigned counter = atomic_fetch_add_explicit(&temporary_counter, 1u,
                                                  memory_order_relaxed) + 1u;
    int temporary_length = snprintf(
        temporary, sizeof temporary, ".%s.ktech-tmp-%ld-%u",
        basename, (long)getpid(), counter);
    if (temporary_length < 0 ||
        (size_t)temporary_length >= sizeof temporary) {
        (void)close(directory);
        ktech_audio_free(audio);
        free(render_project);
        set_error(error, error_size, "export filename is too long");
        return false;
    }
    int descriptor = openat(directory, temporary,
                            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC |
                                O_NOFOLLOW,
                            S_IRUSR | S_IWUSR);
    bool ok = descriptor >= 0;
    uint8_t header[44];
    make_wav_header(header, options->sample_rate, data_bytes,
                    options->bit_depth);
    if (ok) ok = write_all(descriptor, header, sizeof header);

    int16_t samples[EXPORT_BLOCK_FRAMES * 2u];
    uint8_t little_endian[EXPORT_BLOCK_FRAMES * 8u];
    uint64_t done = 0u;
    uint64_t hash = UINT64_C(14695981039346656037);
    bool stopped_for_tail = false;
    while (ok && done < frames) {
        if (options->cancel &&
            atomic_load_explicit(options->cancel, memory_order_acquire)) {
            ok = false;
            errno = ECANCELED;
            break;
        }
        if (!stopped_for_tail && done == musical_frames) {
            ok = ktech_audio_stop(audio);
            stopped_for_tail = true;
            if (!ok) { errno = EIO; break; }
        }
        uint64_t remaining = frames - done;
        if (!stopped_for_tail && done < musical_frames &&
            remaining > musical_frames - done)
            remaining = musical_frames - done;
        size_t chunk = remaining > EXPORT_BLOCK_FRAMES ?
            EXPORT_BLOCK_FRAMES : (size_t)remaining;
        ktech_audio_poll(audio);
        ktech_audio_mix(audio, samples, chunk);
        if (options->normalize && normalization_peak > 0u)
            for (size_t index = 0u; index < chunk * 2u; ++index) {
                int64_t scaled = (int64_t)samples[index] * INT16_MAX /
                                 normalization_peak;
                if (scaled > INT16_MAX) scaled = INT16_MAX;
                if (scaled < INT16_MIN) scaled = INT16_MIN;
                samples[index] = (int16_t)scaled;
            }
        hash = hash_samples(hash, samples, chunk * 2u);
        for (size_t index = 0u; index < chunk * 2u; ++index) {
            int32_t expanded = samples[index];
            unsigned sample_bytes = (unsigned)options->bit_depth / 8u;
            if (options->bit_depth == 24u) expanded *= 256;
            else if (options->bit_depth == 32u) expanded *= 65536;
            for (unsigned byte = 0u; byte < sample_bytes; ++byte)
                little_endian[index * sample_bytes + byte] =
                    (uint8_t)((uint32_t)expanded >> (byte * 8u));
        }
        ok = write_all(descriptor, little_endian, chunk * frame_bytes);
        done += chunk;
    }
    if (ok && fsync(descriptor) != 0) ok = false;
    if (descriptor >= 0 && close(descriptor) != 0) ok = false;
    bool published = false;
    if (ok) {
        if (options->replace_existing) {
            ok = renameat(directory, temporary, directory, basename) == 0;
            published = ok;
        } else {
#ifdef SYS_renameat2
            ok = syscall(SYS_renameat2, directory, temporary, directory,
                         basename, RENAME_NOREPLACE) == 0;
            published = ok;
#else
            ok = linkat(directory, temporary, directory, basename, 0) == 0;
            published = ok;
            if (ok && unlinkat(directory, temporary, 0) != 0) ok = false;
#endif
        }
    }
    if (ok && fsync(directory) != 0) ok = false;
    int failure_errno = ok ? 0 : errno;
    if (!ok || !published) (void)unlinkat(directory, temporary, 0);
    (void)close(directory);

    ktech_audio_free(audio);
    free(render_project);
    if (!ok) {
        set_error(error, error_size, "export failed: %s",
                  strerror(failure_errno ? failure_errno : EIO));
        return false;
    }
    if (info) {
        info->frames = frames;
        info->pcm_hash = hash;
        info->bytes = data_bytes + 44u;
    }
    if (error && error_size) error[0] = '\0';
    return true;
}

bool ktech_export_stems(const ktech_project *project,
                        const char *absolute_directory,
                        const ktech_export_options *options_value,
                        ktech_export_info info[KTECH_TRACK_COUNT],
                        char *error, size_t error_size)
{
    if (info) memset(info, 0, KTECH_TRACK_COUNT * sizeof info[0]);
    if (!project || !absolute_directory || absolute_directory[0] != '/' ||
        strlen(absolute_directory) >= 4000u) {
        set_error(error, error_size, "invalid stem directory");
        return false;
    }
    int directory = open(absolute_directory,
                         O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory < 0) {
        set_error(error, error_size, "unsafe or missing stem directory");
        return false;
    }
    ktech_project *stem = malloc(sizeof *stem);
    if (!stem) {
        (void)close(directory);
        set_error(error, error_size, "cannot allocate stem project");
        return false;
    }
    ktech_export_options defaults;
    ktech_export_options_init(&defaults);
    const ktech_export_options *options = options_value ? options_value :
                                                           &defaults;
    if (options->replace_existing) {
        free(stem);
        (void)close(directory);
        set_error(error, error_size,
                  "atomic stem export does not replace existing files");
        return false;
    }
    char final_names[KTECH_TRACK_COUNT][32];
    char hidden_names[KTECH_TRACK_COUNT][96];
    char hidden_paths[KTECH_TRACK_COUNT][4096];
    static atomic_uint stem_counter;
    unsigned counter = atomic_fetch_add_explicit(&stem_counter, 1u,
                                                  memory_order_relaxed) + 1u;
    bool success = true;
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
        (void)snprintf(final_names[track], sizeof final_names[track],
                       "track-%02u.wav", (unsigned)track + 1u);
        (void)snprintf(hidden_names[track], sizeof hidden_names[track],
                       ".ktech-stems-%ld-%u-%02u.wav", (long)getpid(),
                       counter, (unsigned)track + 1u);
        int count = snprintf(hidden_paths[track], sizeof hidden_paths[track],
                             "%s/%s", absolute_directory,
                             hidden_names[track]);
        struct stat status;
        if (count < 0 || (size_t)count >= sizeof hidden_paths[track] ||
            fstatat(directory, final_names[track], &status,
                    AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT) {
            success = false;
            set_error(error, error_size,
                      "stem target exists or cannot be inspected");
            break;
        }
    }
    unsigned rendered = 0u;
    unsigned published = 0u;
    for (uint8_t selected = 0u; selected < KTECH_TRACK_COUNT; ++selected) {
        if (!success) break;
        *stem = *project;
        for (uint8_t scene = 0u; scene < KTECH_SCENE_COUNT; ++scene)
            for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
                stem->scenes[scene].tracks[track].mute =
                    (uint8_t)(track == selected ? 0u : 1u);
                stem->scenes[scene].tracks[track].solo =
                    (uint8_t)(track == selected ? 1u : 0u);
            }
        if (!ktech_export_wav(stem, hidden_paths[selected], options,
                              info ? &info[selected] : NULL,
                              error, error_size)) {
            success = false;
            break;
        }
        ++rendered;
    }
    for (uint8_t selected = 0u;
         success && selected < KTECH_TRACK_COUNT; ++selected) {
        if (linkat(directory, hidden_names[selected], directory,
                   final_names[selected], 0) != 0) {
            success = false;
            set_error(error, error_size, "cannot publish stem set: %s",
                      strerror(errno));
            break;
        }
        ++published;
    }
    for (unsigned selected = 0u; selected < rendered; ++selected)
        (void)unlinkat(directory, hidden_names[selected], 0);
    if (!success)
        for (unsigned selected = 0u; selected < published; ++selected)
            (void)unlinkat(directory, final_names[selected], 0);
    if (fsync(directory) != 0 && success) {
        success = false;
        set_error(error, error_size, "cannot synchronize stem directory");
        for (unsigned selected = 0u; selected < published; ++selected)
            (void)unlinkat(directory, final_names[selected], 0);
        (void)fsync(directory);
    }
    (void)close(directory);
    free(stem);
    if (!success) return false;
    if (error && error_size > 0u) error[0] = '\0';
    return true;
}
