#include "kilix_techno/audio.h"
#include "kilix_techno/project.h"

#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

static atomic_bool armed = ATOMIC_VAR_INIT(false);
static atomic_uint_fast64_t memory_operations = ATOMIC_VAR_INIT(0u);
static atomic_uint_fast64_t mutex_locks = ATOMIC_VAR_INIT(0u);
static atomic_uint_fast64_t mutex_unlocks = ATOMIC_VAR_INIT(0u);
static atomic_uint_fast64_t file_operations = ATOMIC_VAR_INIT(0u);
static atomic_uint_fast64_t process_operations = ATOMIC_VAR_INIT(0u);
static atomic_uint_fast64_t log_operations = ATOMIC_VAR_INIT(0u);

void *__wrap_malloc(size_t size);
void *__wrap_calloc(size_t count_value, size_t size);
void *__wrap_realloc(void *pointer, size_t size);
void __wrap_free(void *pointer);
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex);
int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex);
ssize_t __wrap_read(int descriptor, void *buffer, size_t size);
ssize_t __wrap_write(int descriptor, const void *buffer, size_t size);
int __wrap_close(int descriptor);
int __wrap_open(const char *path, int flags, ...);
int __wrap_openat(int directory, const char *path, int flags, ...);
FILE *__wrap_fopen(const char *path, const char *mode);
pid_t __wrap_fork(void);
int __wrap_execve(const char *path, char *const arguments[],
                  char *const environment[]);
int __wrap_system(const char *command);
int __wrap_printf(const char *format, ...);
int __wrap_fprintf(FILE *stream, const char *format, ...);
int __wrap_puts(const char *text);
int __wrap_fputs(const char *text, FILE *stream);

static void count(atomic_uint_fast64_t *counter)
{
    if (atomic_load_explicit(&armed, memory_order_relaxed))
        (void)atomic_fetch_add_explicit(counter, 1u, memory_order_relaxed);
}

void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size)
{
    count(&memory_operations);
    return __real_malloc(size);
}

void *__real_calloc(size_t count_value, size_t size);
void *__wrap_calloc(size_t count_value, size_t size)
{
    count(&memory_operations);
    return __real_calloc(count_value, size);
}

void *__real_realloc(void *pointer, size_t size);
void *__wrap_realloc(void *pointer, size_t size)
{
    count(&memory_operations);
    return __real_realloc(pointer, size);
}

void __real_free(void *pointer);
void __wrap_free(void *pointer)
{
    count(&memory_operations);
    __real_free(pointer);
}

int __real_pthread_mutex_lock(pthread_mutex_t *mutex);
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex)
{
    count(&mutex_locks);
    return __real_pthread_mutex_lock(mutex);
}

int __real_pthread_mutex_unlock(pthread_mutex_t *mutex);
int __wrap_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    count(&mutex_unlocks);
    return __real_pthread_mutex_unlock(mutex);
}

ssize_t __real_read(int descriptor, void *buffer, size_t size);
ssize_t __wrap_read(int descriptor, void *buffer, size_t size)
{
    count(&file_operations);
    return __real_read(descriptor, buffer, size);
}

ssize_t __real_write(int descriptor, const void *buffer, size_t size);
ssize_t __wrap_write(int descriptor, const void *buffer, size_t size)
{
    count(&file_operations);
    return __real_write(descriptor, buffer, size);
}

int __real_close(int descriptor);
int __wrap_close(int descriptor)
{
    count(&file_operations);
    return __real_close(descriptor);
}

int __real_open(const char *path, int flags, ...);
int __wrap_open(const char *path, int flags, ...)
{
    count(&file_operations);
    if ((flags & O_CREAT) != 0) {
        va_list arguments;
        va_start(arguments, flags);
        mode_t mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
        return __real_open(path, flags, mode);
    }
    return __real_open(path, flags);
}

int __real_openat(int directory, const char *path, int flags, ...);
int __wrap_openat(int directory, const char *path, int flags, ...)
{
    count(&file_operations);
    if ((flags & O_CREAT) != 0) {
        va_list arguments;
        va_start(arguments, flags);
        mode_t mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
        return __real_openat(directory, path, flags, mode);
    }
    return __real_openat(directory, path, flags);
}

FILE *__real_fopen(const char *path, const char *mode);
FILE *__wrap_fopen(const char *path, const char *mode)
{
    count(&file_operations);
    return __real_fopen(path, mode);
}

pid_t __real_fork(void);
pid_t __wrap_fork(void)
{
    count(&process_operations);
    return __real_fork();
}

int __real_execve(const char *path, char *const arguments[],
                  char *const environment[]);
int __wrap_execve(const char *path, char *const arguments[],
                  char *const environment[])
{
    count(&process_operations);
    return __real_execve(path, arguments, environment);
}

int __real_system(const char *command);
int __wrap_system(const char *command)
{
    count(&process_operations);
    return __real_system(command);
}

int __wrap_printf(const char *format, ...)
{
    count(&log_operations);
    va_list arguments;
    va_start(arguments, format);
    int result = vprintf(format, arguments);
    va_end(arguments);
    return result;
}

int __wrap_fprintf(FILE *stream, const char *format, ...)
{
    count(&log_operations);
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int __real_puts(const char *text);
int __wrap_puts(const char *text)
{
    count(&log_operations);
    return __real_puts(text);
}

int __real_fputs(const char *text, FILE *stream);
int __wrap_fputs(const char *text, FILE *stream)
{
    count(&log_operations);
    return __real_fputs(text, stream);
}

int main(void)
{
    enum { RUNS = 1000, FRAMES = 512 };
    ktech_project project;
    ktech_project_init(&project, UINT32_C(0x72746764));
    ktech_audio_options options;
    ktech_audio_options_init(&options);
    options.sample_rate = 44100u;
    options.offline = true;
    char error[160];
    ktech_audio *audio = ktech_audio_create(&project, &options,
                                             error, sizeof error);
    if (!audio) {
        (void)fprintf(stderr, "test-realtime: %s\n", error);
        return 1;
    }

    int16_t samples[FRAMES * 2u];
    atomic_store_explicit(&armed, true, memory_order_release);
    for (unsigned run = 0u; run < RUNS; ++run)
        ktech_audio_mix(audio, samples, FRAMES);
    atomic_store_explicit(&armed, false, memory_order_release);

    uint64_t memory = atomic_load_explicit(&memory_operations,
                                           memory_order_relaxed);
    uint64_t locks = atomic_load_explicit(&mutex_locks, memory_order_relaxed);
    uint64_t unlocks = atomic_load_explicit(&mutex_unlocks,
                                             memory_order_relaxed);
    uint64_t files = atomic_load_explicit(&file_operations,
                                          memory_order_relaxed);
    uint64_t processes = atomic_load_explicit(&process_operations,
                                              memory_order_relaxed);
    uint64_t logs = atomic_load_explicit(&log_operations,
                                         memory_order_relaxed);
    ktech_audio_free(audio);

    /* pcmmix_mix_block owns exactly one outer transport mutex per pull. The
     * generator is forbidden from acquiring any additional mutex. */
    if (memory != 0u || locks != RUNS || unlocks != RUNS || files != 0u ||
        processes != 0u || logs != 0u) {
        (void)fprintf(stderr,
            "test-realtime: memory=%" PRIu64 " locks=%" PRIu64
            " unlocks=%" PRIu64 " files=%" PRIu64 " processes=%" PRIu64
            " logs=%" PRIu64 "\n",
            memory, locks, unlocks, files, processes, logs);
        return 1;
    }

    (void)printf("test-realtime: OK (%u callback blocks)\n", RUNS);
    return 0;
}
