#include "kilix_techno/resource.h"

#include "kilix_techno/workstation.h"

#include "pcmmix_asset.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { RESOURCE_MAX_BYTES = 64 * 1024 * 1024 };

typedef struct sha256_state {
    uint32_t value[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} sha256_state;

static uint32_t rotate_right(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32u - bits));
}

static void sha256_transform(sha256_state *state, const uint8_t block[64])
{
    static const uint32_t constants[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    uint32_t words[64];
    for (unsigned index = 0u; index < 16u; ++index)
        words[index] = ((uint32_t)block[index * 4u] << 24) |
                       ((uint32_t)block[index * 4u + 1u] << 16) |
                       ((uint32_t)block[index * 4u + 2u] << 8) |
                       block[index * 4u + 3u];
    for (unsigned index = 16u; index < 64u; ++index) {
        uint32_t s0 = rotate_right(words[index - 15u], 7u) ^
                      rotate_right(words[index - 15u], 18u) ^
                      (words[index - 15u] >> 3);
        uint32_t s1 = rotate_right(words[index - 2u], 17u) ^
                      rotate_right(words[index - 2u], 19u) ^
                      (words[index - 2u] >> 10);
        words[index] = words[index - 16u] + s0 + words[index - 7u] + s1;
    }
    uint32_t a=state->value[0], b=state->value[1], c=state->value[2], d=state->value[3];
    uint32_t e=state->value[4], f=state->value[5], g=state->value[6], h=state->value[7];
    for (unsigned index = 0u; index < 64u; ++index) {
        uint32_t s1 = rotate_right(e,6u)^rotate_right(e,11u)^rotate_right(e,25u);
        uint32_t choice = (e & f) ^ (~e & g);
        uint32_t temporary1 = h + s1 + choice + constants[index] + words[index];
        uint32_t s0 = rotate_right(a,2u)^rotate_right(a,13u)^rotate_right(a,22u);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary2 = s0 + majority;
        h=g; g=f; f=e; e=d+temporary1; d=c; c=b; b=a; a=temporary1+temporary2;
    }
    state->value[0]+=a; state->value[1]+=b; state->value[2]+=c; state->value[3]+=d;
    state->value[4]+=e; state->value[5]+=f; state->value[6]+=g; state->value[7]+=h;
}

static void sha256_init(sha256_state *state)
{
    *state = (sha256_state){{0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                             0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u},0u,{0},0u};
}

static void sha256_update(sha256_state *state, const uint8_t *bytes,
                          size_t count)
{
    state->bytes += count;
    while (count > 0u) {
        size_t take = 64u - state->used;
        if (take > count) take = count;
        memcpy(state->block + state->used, bytes, take);
        state->used += take;
        bytes += take;
        count -= take;
        if (state->used == 64u) {
            sha256_transform(state, state->block);
            state->used = 0u;
        }
    }
}

static void sha256_final(sha256_state *state, uint8_t digest[32])
{
    uint64_t bits = state->bytes * 8u;
    state->block[state->used++] = 0x80u;
    if (state->used > 56u) {
        memset(state->block + state->used, 0, 64u - state->used);
        sha256_transform(state, state->block);
        state->used = 0u;
    }
    memset(state->block + state->used, 0, 56u - state->used);
    for (unsigned index = 0u; index < 8u; ++index)
        state->block[63u - index] = (uint8_t)(bits >> (index * 8u));
    sha256_transform(state, state->block);
    for (unsigned index = 0u; index < 8u; ++index) {
        digest[index * 4u] = (uint8_t)(state->value[index] >> 24);
        digest[index * 4u + 1u] = (uint8_t)(state->value[index] >> 16);
        digest[index * 4u + 2u] = (uint8_t)(state->value[index] >> 8);
        digest[index * 4u + 3u] = (uint8_t)state->value[index];
    }
}

static void set_error(char *error, size_t size, const char *format, ...)
{
    va_list arguments;
    if (!error || size == 0u) return;
    va_start(arguments, format);
    (void)vsnprintf(error, size, format, arguments);
    va_end(arguments);
}

static bool valid_relative(const char *path)
{
    size_t length = path ? strlen(path) : 0u;
    return length > 0u && length < KTECH_RESOURCE_PATH_CAPACITY &&
           path[0] != '/' && !strstr(path, "\\") && !strstr(path, "//") &&
           strcmp(path, ".") != 0 && strcmp(path, "..") != 0 &&
           strncmp(path, "../", 3u) != 0 && !strstr(path, "/../") &&
           !(length >= 3u && strcmp(path + length - 3u, "/..") == 0);
}

static int open_relative(int root, const char *path, int flags, mode_t mode,
                         bool make_directories)
{
    char copy[KTECH_RESOURCE_PATH_CAPACITY];
    if (!valid_relative(path)) { errno = EINVAL; return -1; }
    (void)snprintf(copy, sizeof copy, "%s", path);
    int directory = dup(root);
    if (directory < 0) return -1;
    char *cursor = copy;
    for (;;) {
        char *slash = strchr(cursor, '/');
        if (!slash) break;
        *slash = '\0';
        if (cursor[0] == '\0' || strcmp(cursor, ".") == 0 ||
            strcmp(cursor, "..") == 0) { close(directory); errno=EINVAL; return -1; }
        int next = openat(directory, cursor,
                          O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        if (next < 0 && errno == ENOENT && make_directories &&
            mkdirat(directory, cursor, 0700) == 0)
            next = openat(directory, cursor,
                          O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        close(directory);
        if (next < 0) return -1;
        directory = next;
        cursor = slash + 1u;
    }
    int descriptor = openat(directory, cursor, flags | O_CLOEXEC | O_NOFOLLOW,
                            mode);
    close(directory);
    return descriptor;
}

static bool unlink_relative(int root, const char *path)
{
    char copy[KTECH_RESOURCE_PATH_CAPACITY];
    if (!valid_relative(path)) return false;
    (void)snprintf(copy, sizeof copy, "%s", path);
    int directory = dup(root);
    if (directory < 0) return false;
    char *cursor = copy;
    for (;;) {
        char *slash = strchr(cursor, '/');
        if (!slash) break;
        *slash = '\0';
        int next = openat(directory, cursor,
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        close(directory);
        if (next < 0) return false;
        directory = next;
        cursor = slash + 1u;
    }
    bool removed = unlinkat(directory, cursor, 0) == 0;
    close(directory);
    return removed;
}

static bool write_all(int descriptor, const void *data, size_t count)
{
    const uint8_t *bytes = data;
    size_t written = 0u;
    while (written < count) {
        ssize_t result = write(descriptor, bytes + written, count - written);
        if (result > 0) written += (size_t)result;
        else if (result < 0 && errno == EINTR) continue;
        else return false;
    }
    return true;
}

static void store_u16le(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void store_u32le(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static bool write_mono_wav(int descriptor, const int16_t *samples,
                           size_t frames, uint32_t sample_rate)
{
    if (!samples || frames == 0u || frames > (UINT32_MAX - 44u) / 2u)
        return false;
    uint32_t pcm_bytes = (uint32_t)frames * 2u;
    uint8_t header[44] = {0};
    memcpy(header, "RIFF", 4u);
    store_u32le(header + 4u, 36u + pcm_bytes);
    memcpy(header + 8u, "WAVEfmt ", 8u);
    store_u32le(header + 16u, 16u);
    store_u16le(header + 20u, 1u);
    store_u16le(header + 22u, 1u);
    store_u32le(header + 24u, sample_rate);
    store_u32le(header + 28u, sample_rate * 2u);
    store_u16le(header + 32u, 2u);
    store_u16le(header + 34u, 16u);
    memcpy(header + 36u, "data", 4u);
    store_u32le(header + 40u, pcm_bytes);
    if (!write_all(descriptor, header, sizeof header)) return false;
    uint8_t buffer[8192];
    size_t offset = 0u;
    while (offset < frames) {
        size_t count = frames - offset;
        if (count > sizeof buffer / 2u) count = sizeof buffer / 2u;
        for (size_t index = 0u; index < count; ++index)
            store_u16le(buffer + index * 2u,
                        (uint16_t)samples[offset + index]);
        if (!write_all(descriptor, buffer, count * 2u)) return false;
        offset += count;
    }
    return true;
}

static bool ends_with_wav(const char *path)
{
    size_t length = path ? strlen(path) : 0u;
    if (length < 4u) return false;
    const char *extension = path + length - 4u;
    return extension[0] == '.' &&
           (extension[1] == 'w' || extension[1] == 'W') &&
           (extension[2] == 'a' || extension[2] == 'A') &&
           (extension[3] == 'v' || extension[3] == 'V');
}

static bool hash_descriptor(int descriptor, uint8_t digest[32],
                            uint32_t *bytes_out)
{
    if (lseek(descriptor, 0, SEEK_SET) != 0) return false;
    sha256_state hash;
    sha256_init(&hash);
    uint8_t buffer[16384];
    uint64_t total = 0u;
    for (;;) {
        ssize_t count = read(descriptor, buffer, sizeof buffer);
        if (count > 0) {
            total += (size_t)count;
            if (total > RESOURCE_MAX_BYTES) return false;
            sha256_update(&hash, buffer, (size_t)count);
        } else if (count == 0) break;
        else if (errno != EINTR) return false;
    }
    sha256_final(&hash, digest);
    if (bytes_out) *bytes_out = (uint32_t)total;
    return lseek(descriptor, 0, SEEK_SET) == 0;
}

static ktech_resource_format project_format(pcmmix_asset_format format)
{
    return format == PCMMIX_ASSET_WAV ? KTECH_RESOURCE_WAV :
           format == PCMMIX_ASSET_OGG ? KTECH_RESOURCE_OGG :
           format == PCMMIX_ASSET_SFZ ? KTECH_RESOURCE_SFZ :
                                        KTECH_RESOURCE_SF2;
}

bool ktech_sample_import(ktech_project *project, const char *source,
                         const char *resource_root,
                         const char *destination, const char *license,
                         ktech_role role, char *error, size_t error_size)
{
    if (!project || !source || source[0] != '/' || !resource_root ||
        resource_root[0] != '/' || !valid_relative(destination) || !license ||
        license[0] == '\0' || role >= KTECH_ROLE_COUNT) {
        set_error(error, error_size, "invalid sample import request");
        return false;
    }
    int source_fd = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat status;
    if (source_fd < 0 || fstat(source_fd, &status) != 0 ||
        !S_ISREG(status.st_mode) || status.st_size <= 0 ||
        status.st_size > RESOURCE_MAX_BYTES) {
        if (source_fd >= 0) close(source_fd);
        set_error(error, error_size, "source is not a bounded regular file");
        return false;
    }
    pcmmix_asset_format detected = pcmmix_asset_detect(source);
    if (detected == PCMMIX_ASSET_UNKNOWN) {
        close(source_fd);
        set_error(error, error_size, "source is not WAV, OGG, SFZ, or SF2");
        return false;
    }
    pcmmix_asset_options options;
    pcmmix_asset_options_init(&options);
    options.max_seconds = 30u;
    size_t frames;
    uint8_t root_note;
    char decode_error[128];
    int16_t *decoded = pcmmix_asset_load(source, &options, &frames,
                                          &root_note, decode_error,
                                          sizeof decode_error);
    if (!decoded || frames == 0u) {
        pcmmix_asset_free(decoded);
        close(source_fd);
        set_error(error, error_size, "sample validation failed: %s",
                  decode_error);
        return false;
    }
    bool render_dependency = detected == PCMMIX_ASSET_SFZ ||
                             detected == PCMMIX_ASSET_SF2;
    if (render_dependency && !ends_with_wav(destination)) {
        pcmmix_asset_free(decoded);
        close(source_fd);
        set_error(error, error_size,
                  "SFZ and SF2 imports require a .wav destination");
        return false;
    }
    int root = open(resource_root,
                    O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (root < 0) {
        pcmmix_asset_free(decoded);
        close(source_fd);
        set_error(error, error_size, "resource root is unsafe or missing");
        return false;
    }
    int destination_fd = open_relative(
        root, destination, O_RDWR | O_CREAT | O_EXCL, 0600, true);
    bool success = destination_fd >= 0;
    if (success && render_dependency) {
        success = write_mono_wav(destination_fd, decoded, frames,
                                 options.target_rate);
    } else {
        uint8_t buffer[16384];
        while (success) {
            ssize_t count = read(source_fd, buffer, sizeof buffer);
            if (count == 0) break;
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) { success = false; break; }
            success = write_all(destination_fd, buffer, (size_t)count);
        }
    }
    if (success && fsync(destination_fd) != 0) success = false;
    uint8_t digest[32] = {0};
    uint32_t byte_count = 0u;
    if (success && !hash_descriptor(destination_fd, digest, &byte_count))
        success = false;
    if (destination_fd >= 0 && close(destination_fd) != 0) success = false;
    pcmmix_asset_free(decoded);
    close(source_fd);
    if (!success) {
        (void)unlink_relative(root, destination);
        close(root);
        set_error(error, error_size, "cannot publish imported sample");
        return false;
    }
    if (!ktech_resource_add(project, destination, license, digest, byte_count,
                            render_dependency ? KTECH_RESOURCE_WAV :
                                                project_format(detected),
                            role)) {
        (void)unlink_relative(root, destination);
        close(root);
        set_error(error, error_size, "resource manifest is full or invalid");
        return false;
    }
    close(root);
    if (error && error_size > 0u) error[0] = '\0';
    return true;
}

bool ktech_resource_check(const ktech_project *project,
                          const char *resource_root,
                          ktech_resource_report *report,
                          char *error, size_t error_size)
{
    if (report) memset(report, 0, sizeof *report);
    if (!project || !resource_root || resource_root[0] != '/' || !report) {
        set_error(error, error_size, "invalid resource report request");
        return false;
    }
    int root = open(resource_root,
                    O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (root < 0) {
        set_error(error, error_size, "resource root is unsafe or missing");
        return false;
    }
    for (uint8_t index = 0u; index < project->resource_count; ++index) {
        const ktech_resource *resource = &project->resources[index];
        int descriptor = open_relative(root, resource->path, O_RDONLY, 0, false);
        if (descriptor < 0) {
            if (errno == ENOENT) ++report->missing;
            else ++report->unsafe;
            continue;
        }
        struct stat status;
        uint8_t digest[32];
        uint32_t bytes;
        bool good = fstat(descriptor, &status) == 0 &&
                    S_ISREG(status.st_mode) &&
                    hash_descriptor(descriptor, digest, &bytes);
        close(descriptor);
        if (!good) ++report->unsafe;
        else if (bytes != resource->bytes ||
                 memcmp(digest, resource->sha256, 32u) != 0)
            ++report->changed;
        else
            ++report->available;
    }
    close(root);
    if (error && error_size > 0u) error[0] = '\0';
    return true;
}
