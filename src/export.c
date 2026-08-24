#include "kilix_techno/export.h"

#include "kilix_techno/compile.h"
#include "kilix_techno/dsp.h"

#include "chip_sequencer.h"

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
    options->scene = 0u;
    options->replace_existing = false;
    options->cancel = NULL;
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
                            uint32_t data_bytes)
{
    memset(header, 0, 44u);
    memcpy(header, "RIFF", 4u);
    put_u32(header + 4u, data_bytes + 36u);
    memcpy(header + 8u, "WAVEfmt ", 8u);
    put_u32(header + 16u, 16u);
    put_u16(header + 20u, 1u);
    put_u16(header + 22u, 2u);
    put_u32(header + 24u, sample_rate);
    put_u32(header + 28u, sample_rate * 4u);
    put_u16(header + 32u, 4u);
    put_u16(header + 34u, 16u);
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

static void set_channel_mix(chipseq *sequence, const ktech_scene *scene)
{
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
    (void)chipseq_music_set_channel_mix(sequence, &mix);
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
        options->scene >= KTECH_SCENE_COUNT) {
        set_error(error, error_size, "invalid export options");
        return false;
    }
    uint64_t numerator = (uint64_t)options->sample_rate * 60u * 4u *
                         options->bars;
    uint64_t frames = (numerator + project->tempo - 1u) / project->tempo;
    if (frames > (UINT32_MAX - 36u) / 4u) {
        set_error(error, error_size, "export exceeds RIFF size limit");
        return false;
    }
    uint32_t data_bytes = (uint32_t)(frames * 4u);

    char compile_error[160];
    ktech_compiled *compiled = ktech_compile_scene(
        project, options->scene, compile_error, sizeof compile_error);
    if (!compiled) {
        set_error(error, error_size, "%s", compile_error);
        return false;
    }
    chipseq_options sequence_options;
    chipseq_options_init(&sequence_options);
    sequence_options.sample_rate = options->sample_rate;
    chipseq sequence;
    ktech_dsp dsp;
    bool sequence_live = chipseq_init(&sequence, &sequence_options);
    bool dsp_live = ktech_dsp_init(&dsp, options->sample_rate);
    if (!sequence_live || !dsp_live) {
        if (sequence_live) chipseq_shutdown(&sequence);
        if (dsp_live) ktech_dsp_free(&dsp);
        ktech_compiled_free(compiled);
        set_error(error, error_size, "cannot initialize offline renderer");
        return false;
    }
    char song_error[160] = {0};
    bool started = chipseq_music_set_swing(&sequence, project->swing);
    if (started)
        started = chipseq_music_play(&sequence,
                                     ktech_compiled_song(compiled), true,
                                     song_error, sizeof song_error);
    if (started) {
        set_channel_mix(&sequence, &project->scenes[options->scene]);
        chipseq_flush_commands(&sequence);
    }
    ktech_dsp_configure(&dsp, project);
    if (!started) {
        chipseq_shutdown(&sequence);
        ktech_dsp_free(&dsp);
        ktech_compiled_free(compiled);
        set_error(error, error_size, "cannot start offline song: %s",
                  song_error);
        return false;
    }

    char basename[256];
    int directory = open_parent_secure(absolute_path, basename,
                                       sizeof basename);
    if (directory < 0) {
        chipseq_shutdown(&sequence);
        ktech_dsp_free(&dsp);
        ktech_compiled_free(compiled);
        set_error(error, error_size, "unsafe export path: %s",
                  strerror(errno));
        return false;
    }
    struct stat status;
    if (fstatat(directory, basename, &status, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!options->replace_existing || !S_ISREG(status.st_mode)) {
            (void)close(directory);
            chipseq_shutdown(&sequence);
            ktech_dsp_free(&dsp);
            ktech_compiled_free(compiled);
            set_error(error, error_size, "%s",
                      options->replace_existing ? "unsafe export target" :
                                                  "export already exists");
            return false;
        }
    } else if (errno != ENOENT) {
        (void)close(directory);
        chipseq_shutdown(&sequence);
        ktech_dsp_free(&dsp);
        ktech_compiled_free(compiled);
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
        chipseq_shutdown(&sequence);
        ktech_dsp_free(&dsp);
        ktech_compiled_free(compiled);
        set_error(error, error_size, "export filename is too long");
        return false;
    }
    int descriptor = openat(directory, temporary,
                            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC |
                                O_NOFOLLOW,
                            S_IRUSR | S_IWUSR);
    bool ok = descriptor >= 0;
    uint8_t header[44];
    make_wav_header(header, options->sample_rate, data_bytes);
    if (ok) ok = write_all(descriptor, header, sizeof header);

    int16_t samples[EXPORT_BLOCK_FRAMES * 2u];
    uint8_t little_endian[EXPORT_BLOCK_FRAMES * 4u];
    uint64_t done = 0u;
    uint64_t hash = UINT64_C(14695981039346656037);
    while (ok && done < frames) {
        if (options->cancel &&
            atomic_load_explicit(options->cancel, memory_order_acquire)) {
            ok = false;
            errno = ECANCELED;
            break;
        }
        size_t chunk = frames - done > EXPORT_BLOCK_FRAMES
                           ? EXPORT_BLOCK_FRAMES : (size_t)(frames - done);
        chipseq_render_s16_stereo(&sequence, samples, chunk);
        ktech_dsp_process(&dsp, samples, chunk);
        hash = hash_samples(hash, samples, chunk * 2u);
        for (size_t index = 0u; index < chunk * 2u; ++index) {
            uint16_t value = (uint16_t)samples[index];
            little_endian[index * 2u] = (uint8_t)value;
            little_endian[index * 2u + 1u] = (uint8_t)(value >> 8);
        }
        ok = write_all(descriptor, little_endian, chunk * 4u);
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

    chipseq_shutdown(&sequence);
    ktech_dsp_free(&dsp);
    ktech_compiled_free(compiled);
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
