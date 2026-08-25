#include "kilix_techno/midi.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1u << 0)
#endif

enum { MIDI_DIVISION = 96, MIDI_TRACKS = KTECH_TRACK_COUNT + 1u };

typedef struct midi_writer {
    uint8_t *bytes;
    size_t capacity;
    size_t used;
    bool ok;
} midi_writer;

typedef struct midi_event {
    uint32_t tick;
    uint8_t status;
    uint8_t note;
    uint8_t velocity;
} midi_event;

static void set_error(char *error, size_t size, const char *format, ...)
{
    va_list arguments;
    if (!error || size == 0u) return;
    va_start(arguments, format);
    (void)vsnprintf(error, size, format, arguments);
    va_end(arguments);
}

static void write_bytes(midi_writer *writer, const void *data, size_t size)
{
    if (!writer->ok || size > writer->capacity - writer->used) {
        writer->ok = false;
        return;
    }
    memcpy(writer->bytes + writer->used, data, size);
    writer->used += size;
}

static void write_u8(midi_writer *writer, uint8_t value)
{
    write_bytes(writer, &value, 1u);
}

static void write_be16(midi_writer *writer, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    write_bytes(writer, bytes, sizeof bytes);
}

static void write_be32(midi_writer *writer, uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16),
                        (uint8_t)(value >> 8), (uint8_t)value};
    write_bytes(writer, bytes, sizeof bytes);
}

static void patch_be32(midi_writer *writer, size_t at, uint32_t value)
{
    if (at + 4u > writer->used) { writer->ok = false; return; }
    writer->bytes[at] = (uint8_t)(value >> 24);
    writer->bytes[at + 1u] = (uint8_t)(value >> 16);
    writer->bytes[at + 2u] = (uint8_t)(value >> 8);
    writer->bytes[at + 3u] = (uint8_t)value;
}

static void write_vlq(midi_writer *writer, uint32_t value)
{
    uint8_t bytes[4];
    unsigned count = 0u;
    bytes[count++] = (uint8_t)(value & 0x7fu);
    while ((value >>= 7) != 0u && count < 4u)
        bytes[count++] = (uint8_t)((value & 0x7fu) | 0x80u);
    while (count > 0u) write_u8(writer, bytes[--count]);
}

static int compare_event(const void *left_value, const void *right_value)
{
    const midi_event *left = left_value;
    const midi_event *right = right_value;
    if (left->tick < right->tick) return -1;
    if (left->tick > right->tick) return 1;
    bool left_off = (left->status & 0xf0u) == 0x80u;
    bool right_off = (right->status & 0xf0u) == 0x80u;
    if (left_off != right_off) return left_off ? -1 : 1;
    if (left->note < right->note) return -1;
    if (left->note > right->note) return 1;
    return 0;
}

static const ktech_track *arrangement_track(
    const ktech_project *project, const ktech_arrangement_entry *entry,
    uint8_t track, ktech_track *scratch)
{
    if (entry->patterns[track] == KTECH_PATTERN_NONE)
        return ktech_project_resolve_track(project, entry->scene, track,
                                           scratch);
    uint8_t pattern = entry->patterns[track];
    if (pattern >= project->pattern_count) return NULL;
    *scratch = project->scenes[entry->scene].tracks[track];
    scratch->role = project->patterns[pattern].role;
    scratch->length = project->patterns[pattern].length;
    memcpy(scratch->steps, project->patterns[pattern].steps,
           sizeof scratch->steps);
    return scratch;
}

static bool collect_events(const ktech_project *project, uint8_t track,
                           midi_event *events, size_t *count,
                           char *error, size_t error_size)
{
    size_t used = 0u;
    uint32_t global_row = 0u;
    uint16_t segments = project->arrangement_count > 0u ?
                        project->arrangement_count : 1u;
    for (uint16_t segment = 0u; segment < segments; ++segment) {
        ktech_arrangement_entry fallback;
        const ktech_arrangement_entry *entry;
        if (project->arrangement_count > 0u) {
            entry = &project->arrangement[segment];
        } else {
            memset(&fallback, 0, sizeof fallback);
            fallback.scene = project->active_scene;
            fallback.bars = 4u;
            memset(fallback.patterns, KTECH_PATTERN_NONE,
                   sizeof fallback.patterns);
            entry = &fallback;
        }
        uint32_t rows = (uint32_t)entry->bars * 16u;
        ktech_track scratch;
        const ktech_track *source = arrangement_track(project, entry, track,
                                                       &scratch);
        if (!source) return false;
        for (uint32_t row = 0u; row < rows; ++row) {
            const ktech_step *step = &source->steps[row % source->length];
            if (!step->enabled) continue;
            if (used + 2u > KTECH_MIDI_EVENT_CAPACITY * 2u) {
                set_error(error, error_size, "MIDI event capacity exceeded");
                return false;
            }
            uint32_t tick = (global_row + row) * (MIDI_DIVISION / 4u);
            uint32_t duration = (uint32_t)(MIDI_DIVISION / 4u) * step->gate /
                                100u;
            if (duration == 0u) duration = 1u;
            events[used++] = (midi_event){tick,
                (uint8_t)(0x90u | track), step->note, step->velocity};
            events[used++] = (midi_event){tick + duration,
                (uint8_t)(0x80u | track), step->note, 0u};
        }
        global_row += rows;
    }
    *count = used;
    return true;
}

static void write_track_start(midi_writer *writer, size_t *length_at,
                              size_t *data_at)
{
    write_bytes(writer, "MTrk", 4u);
    *length_at = writer->used;
    write_be32(writer, 0u);
    *data_at = writer->used;
}

static void write_track_end(midi_writer *writer, size_t length_at,
                            size_t data_at)
{
    write_vlq(writer, 0u);
    const uint8_t end[] = {0xffu, 0x2fu, 0x00u};
    write_bytes(writer, end, sizeof end);
    if (writer->used - data_at > UINT32_MAX) writer->ok = false;
    else patch_be32(writer, length_at, (uint32_t)(writer->used - data_at));
}

bool ktech_midi_encode(const ktech_project *project, uint8_t *bytes,
                       size_t capacity, size_t *encoded_size,
                       char *error, size_t error_size)
{
    if (encoded_size) *encoded_size = 0u;
    if (!project || !bytes || capacity > KTECH_MIDI_MAX_BYTES ||
        !ktech_project_validate(project, error, error_size)) {
        set_error(error, error_size, "invalid MIDI export request");
        return false;
    }
    midi_writer writer = {bytes, capacity, 0u, true};
    write_bytes(&writer, "MThd", 4u);
    write_be32(&writer, 6u);
    write_be16(&writer, 1u);
    write_be16(&writer, MIDI_TRACKS);
    write_be16(&writer, MIDI_DIVISION);
    size_t length_at, data_at;
    write_track_start(&writer, &length_at, &data_at);
    write_vlq(&writer, 0u);
    uint32_t micros = UINT32_C(60000000) / project->tempo;
    const uint8_t tempo[] = {0xffu,0x51u,0x03u,
        (uint8_t)(micros >> 16),(uint8_t)(micros >> 8),(uint8_t)micros};
    write_bytes(&writer, tempo, sizeof tempo);
    write_track_end(&writer, length_at, data_at);

    midi_event *events = malloc(KTECH_MIDI_EVENT_CAPACITY * 2u *
                                sizeof *events);
    if (!events) {
        set_error(error, error_size, "cannot allocate MIDI events");
        return false;
    }
    size_t note_events = 0u;
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT && writer.ok; ++track) {
        size_t count = 0u;
        if (!collect_events(project, track, events, &count,
                            error, error_size)) { writer.ok = false; break; }
        note_events += count / 2u;
        if (note_events > KTECH_MIDI_EVENT_CAPACITY) {
            set_error(error, error_size, "MIDI note capacity exceeded");
            writer.ok = false;
            break;
        }
        qsort(events, count, sizeof events[0], compare_event);
        write_track_start(&writer, &length_at, &data_at);
        const char *name = ktech_role_name(
            (ktech_role)project->scenes[project->active_scene].tracks[track].role);
        size_t name_length = strlen(name);
        write_vlq(&writer, 0u);
        write_u8(&writer, 0xffu); write_u8(&writer, 0x03u);
        write_vlq(&writer, (uint32_t)name_length);
        write_bytes(&writer, name, name_length);
        uint32_t previous = 0u;
        for (size_t index = 0u; index < count; ++index) {
            write_vlq(&writer, events[index].tick - previous);
            write_u8(&writer, events[index].status);
            write_u8(&writer, events[index].note);
            write_u8(&writer, events[index].velocity);
            previous = events[index].tick;
        }
        write_track_end(&writer, length_at, data_at);
    }
    free(events);
    if (!writer.ok || writer.used > KTECH_MIDI_MAX_BYTES) {
        if (!error || error[0] == '\0')
            set_error(error, error_size, "MIDI output buffer is too small");
        return false;
    }
    if (encoded_size) *encoded_size = writer.used;
    if (error && error_size > 0u) error[0] = '\0';
    return true;
}

static uint16_t read_be16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] << 8) | bytes[1];
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static bool read_vlq(const uint8_t *bytes, size_t size, size_t *offset,
                     uint32_t *value)
{
    uint32_t result = 0u;
    for (unsigned count = 0u; count < 4u; ++count) {
        if (*offset >= size) return false;
        uint8_t byte = bytes[(*offset)++];
        result = (result << 7) | (byte & 0x7fu);
        if ((byte & 0x80u) == 0u) { *value = result; return true; }
    }
    return false;
}

bool ktech_midi_decode(ktech_project *project, const uint8_t *bytes,
                       size_t size, char *error, size_t error_size)
{
    if (!project || !bytes || size < 14u || size > KTECH_MIDI_MAX_BYTES ||
        memcmp(bytes, "MThd", 4u) != 0 || read_be32(bytes + 4u) != 6u) {
        set_error(error, error_size, "invalid MIDI header");
        return false;
    }
    uint16_t format = read_be16(bytes + 8u);
    uint16_t tracks = read_be16(bytes + 10u);
    uint16_t division = read_be16(bytes + 12u);
    if (format > 1u || tracks == 0u || tracks > 32u || division == 0u ||
        (division & 0x8000u) != 0u || (format == 0u && tracks != 1u)) {
        set_error(error, error_size, "unsupported bounded MIDI format");
        return false;
    }
    ktech_project temporary;
    ktech_project_init(&temporary, UINT32_C(0x4d494449));
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
        (void)ktech_project_set_track_length(&temporary, 0u, track,
                                             KTECH_STEP_COUNT);
        for (uint8_t step = 0u; step < KTECH_STEP_COUNT;
             ++step)
            temporary.scenes[0].tracks[track].steps[step].enabled = 0u;
    }
    size_t offset = 14u;
    size_t note_count = 0u;
    uint32_t active_tick[16][128];
    memset(active_tick, 0xff, sizeof active_tick);
    for (uint16_t chunk = 0u; chunk < tracks; ++chunk) {
        if (offset + 8u > size || memcmp(bytes + offset, "MTrk", 4u) != 0) {
            set_error(error, error_size, "missing MIDI track chunk");
            return false;
        }
        uint32_t length = read_be32(bytes + offset + 4u);
        offset += 8u;
        if (length > size - offset || length > KTECH_MIDI_MAX_BYTES) {
            set_error(error, error_size, "MIDI track is truncated");
            return false;
        }
        size_t end = offset + length;
        uint32_t tick = 0u;
        uint8_t running = 0u;
        bool ended = false;
        while (offset < end && !ended) {
            uint32_t delta;
            if (!read_vlq(bytes, end, &offset, &delta) ||
                UINT32_MAX - tick < delta) {
                set_error(error, error_size, "invalid MIDI delta time");
                return false;
            }
            tick += delta;
            if (offset >= end) return false;
            uint8_t status = bytes[offset];
            if (status < 0x80u) {
                if (running < 0x80u || running >= 0xf0u) return false;
                status = running;
            } else {
                ++offset;
                if (status < 0xf0u) running = status;
                else running = 0u;
            }
            if (status == 0xffu) {
                if (offset >= end) return false;
                uint8_t type = bytes[offset++];
                uint32_t meta_length;
                if (!read_vlq(bytes, end, &offset, &meta_length) ||
                    meta_length > 65536u || meta_length > end - offset)
                    return false;
                if (type == 0x2fu) {
                    if (meta_length != 0u || offset != end) return false;
                    ended = true;
                } else if (type == 0x51u && meta_length == 3u) {
                    uint32_t micros = ((uint32_t)bytes[offset] << 16) |
                                      ((uint32_t)bytes[offset + 1u] << 8) |
                                      bytes[offset + 2u];
                    if (micros == 0u) return false;
                    uint32_t bpm = (UINT32_C(60000000) + micros / 2u) / micros;
                    if (bpm < 20u) bpm = 20u;
                    if (bpm > 255u) bpm = 255u;
                    temporary.tempo = (uint16_t)bpm;
                }
                offset += meta_length;
                continue;
            }
            if (status == 0xf0u || status == 0xf7u) {
                uint32_t sysex_length;
                if (!read_vlq(bytes, end, &offset, &sysex_length) ||
                    sysex_length > 65536u || sysex_length > end - offset)
                    return false;
                offset += sysex_length;
                continue;
            }
            if (status >= 0xf0u) return false;
            uint8_t kind = status & 0xf0u;
            unsigned data_count = (kind == 0xc0u || kind == 0xd0u) ? 1u : 2u;
            if (offset + data_count > end) return false;
            uint8_t first = bytes[offset++];
            uint8_t second = data_count == 2u ? bytes[offset++] : 0u;
            if (first > 127u || second > 127u) return false;
            if (kind == 0x90u && second != 0u) {
                if (++note_count > KTECH_MIDI_EVENT_CAPACITY) {
                    set_error(error, error_size, "MIDI note capacity exceeded");
                    return false;
                }
                uint8_t channel = status & 0x0fu;
                uint8_t target_track = (uint8_t)(channel % KTECH_TRACK_COUNT);
                uint64_t row = ((uint64_t)tick * 4u + division / 2u) /
                               division;
                if (row < KTECH_STEP_COUNT) {
                    ktech_step *step = &temporary.scenes[0]
                        .tracks[target_track].steps[row];
                    step->enabled = 1u;
                    step->note = first;
                    step->velocity = second;
                }
                active_tick[channel][first] = tick;
            } else if (kind == 0x80u || (kind == 0x90u && second == 0u)) {
                uint8_t channel = status & 0x0fu;
                uint32_t started = active_tick[channel][first];
                if (started != UINT32_MAX && tick >= started) {
                    uint8_t target_track =
                        (uint8_t)(channel % KTECH_TRACK_COUNT);
                    uint64_t row = ((uint64_t)started * 4u + division / 2u) /
                                   division;
                    uint32_t duration = tick - started;
                    if (row < KTECH_STEP_COUNT) {
                        uint64_t gate = ((uint64_t)duration * 400u +
                                         division / 2u) / division;
                        if (gate < 1u) gate = 1u;
                        if (gate > 100u) gate = 100u;
                        temporary.scenes[0].tracks[target_track]
                            .steps[row].gate = (uint8_t)gate;
                    }
                    active_tick[channel][first] = UINT32_MAX;
                }
            }
        }
        if (!ended || offset != end) {
            set_error(error, error_size, "MIDI track has no canonical end");
            return false;
        }
    }
    if (offset != size || !ktech_project_validate(&temporary, error,
                                                   error_size)) return false;
    *project = temporary;
    if (error && error_size > 0u) error[0] = '\0';
    return true;
}

static bool valid_component(const char *component)
{
    return component && component[0] != '\0' &&
           strcmp(component, ".") != 0 && strcmp(component, "..") != 0;
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
            close(directory);
            errno = EINVAL;
            return -1;
        }
        int next = openat(directory, cursor,
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        close(directory);
        if (next < 0) return -1;
        directory = next;
        if (!slash) break;
        cursor = slash + 1u;
        while (*cursor == '/') ++cursor;
    }
    return directory;
}

static bool write_all_fd(int descriptor, const uint8_t *bytes, size_t size)
{
    while (size > 0u) {
        ssize_t count = write(descriptor, bytes, size);
        if (count > 0) {
            bytes += (size_t)count;
            size -= (size_t)count;
        } else if (count < 0 && errno == EINTR) continue;
        else return false;
    }
    return true;
}

bool ktech_midi_save(const ktech_project *project, const char *absolute_path,
                     bool replace_existing, char *error,
                     size_t error_size)
{
    if (!project || !absolute_path || absolute_path[0] != '/') {
        set_error(error, error_size, "invalid MIDI output path");
        return false;
    }
    uint8_t *bytes = malloc(KTECH_MIDI_MAX_BYTES);
    if (!bytes) {
        set_error(error, error_size, "cannot allocate MIDI workspace");
        return false;
    }
    size_t size = 0u;
    if (!ktech_midi_encode(project, bytes, KTECH_MIDI_MAX_BYTES, &size,
                           error, error_size)) {
        free(bytes);
        return false;
    }
    char basename[256];
    int directory = open_parent_secure(absolute_path, basename,
                                       sizeof basename);
    if (directory < 0) {
        free(bytes);
        set_error(error, error_size, "unsafe MIDI output path: %s",
                  strerror(errno));
        return false;
    }
    struct stat status;
    if (fstatat(directory, basename, &status, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!replace_existing || !S_ISREG(status.st_mode)) {
            close(directory);
            free(bytes);
            set_error(error, error_size, replace_existing ?
                      "unsafe MIDI output target" :
                      "MIDI output already exists");
            return false;
        }
    } else if (errno != ENOENT) {
        close(directory);
        free(bytes);
        set_error(error, error_size, "cannot inspect MIDI output target");
        return false;
    }
    static atomic_uint temporary_counter;
    unsigned counter = atomic_fetch_add_explicit(&temporary_counter, 1u,
                                                  memory_order_relaxed) + 1u;
    char temporary[320];
    int length = snprintf(temporary, sizeof temporary,
                          ".%s.ktech-midi-%ld-%u", basename,
                          (long)getpid(), counter);
    int descriptor = length > 0 && (size_t)length < sizeof temporary ?
        openat(directory, temporary,
               O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
               S_IRUSR | S_IWUSR) : -1;
    bool ok = descriptor >= 0 && write_all_fd(descriptor, bytes, size) &&
              fsync(descriptor) == 0;
    if (descriptor >= 0 && close(descriptor) != 0) ok = false;
    bool published = false;
    if (ok && replace_existing) {
        ok = renameat(directory, temporary, directory, basename) == 0;
        published = ok;
    } else if (ok) {
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
    if (ok && fsync(directory) != 0) ok = false;
    int failure = ok ? 0 : errno;
    if (!ok || !published) (void)unlinkat(directory, temporary, 0);
    close(directory);
    free(bytes);
    if (!ok) {
        set_error(error, error_size, "MIDI output failed: %s",
                  strerror(failure ? failure : EIO));
        return false;
    }
    if (error && error_size > 0u) error[0] = '\0';
    return true;
}

bool ktech_midi_load(ktech_project *project, const char *absolute_path,
                     char *error, size_t error_size)
{
    if (!project || !absolute_path || absolute_path[0] != '/') {
        set_error(error, error_size, "invalid MIDI input path");
        return false;
    }
    char basename[256];
    int directory = open_parent_secure(absolute_path, basename,
                                       sizeof basename);
    int descriptor = directory >= 0 ?
        openat(directory, basename, O_RDONLY | O_CLOEXEC | O_NOFOLLOW) : -1;
    if (directory >= 0) close(directory);
    struct stat status;
    if (descriptor < 0 || fstat(descriptor, &status) != 0 ||
        !S_ISREG(status.st_mode) || status.st_size <= 0 ||
        status.st_size > (off_t)KTECH_MIDI_MAX_BYTES) {
        if (descriptor >= 0) close(descriptor);
        set_error(error, error_size, "MIDI input is unsafe or too large");
        return false;
    }
    size_t size = (size_t)status.st_size;
    uint8_t *bytes = malloc(size);
    if (!bytes) {
        close(descriptor);
        set_error(error, error_size, "cannot allocate MIDI workspace");
        return false;
    }
    size_t used = 0u;
    bool ok = true;
    while (used < size) {
        ssize_t count = read(descriptor, bytes + used, size - used);
        if (count > 0) used += (size_t)count;
        else if (count < 0 && errno == EINTR) continue;
        else { ok = false; break; }
    }
    close(descriptor);
    if (ok) ok = ktech_midi_decode(project, bytes, size, error, error_size);
    free(bytes);
    if (!ok && error && error_size > 0u && error[0] == '\0')
        set_error(error, error_size, "cannot read MIDI input");
    return ok;
}
