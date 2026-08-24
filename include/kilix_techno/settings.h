#ifndef KILIX_TECHNO_SETTINGS_H
#define KILIX_TECHNO_SETTINGS_H

#include "kilix_techno/project.h"

#include "kitty_input.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KTECH_SETTINGS_SCHEMA 1u
#define KTECH_SETTINGS_BINDING_CAPACITY KTECH_BINDING_CAPACITY
#define KTECH_SETTINGS_ENCODED_MAX 2048u

typedef ktech_binding ktech_settings_binding;

typedef struct ktech_settings {
    ktech_settings_binding bindings[KTECH_SETTINGS_BINDING_CAPACITY];
    uint16_t binding_count;
    uint16_t repeat_delay_ms;
    uint16_t repeat_interval_ms;
    uint16_t axis_dead_zone;
    uint8_t reduced_motion;
    uint8_t reserved[7];
} ktech_settings;

typedef enum ktech_settings_result {
    KTECH_SETTINGS_OK = 0,
    KTECH_SETTINGS_NOT_FOUND,
    KTECH_SETTINGS_INVALID,
    KTECH_SETTINGS_TOO_LARGE,
    KTECH_SETTINGS_TRUNCATED,
    KTECH_SETTINGS_CORRUPT,
    KTECH_SETTINGS_UNKNOWN_VERSION,
    KTECH_SETTINGS_NONCANONICAL,
    KTECH_SETTINGS_IO_ERROR,
    KTECH_SETTINGS_SECURITY_ERROR
} ktech_settings_result;

void ktech_settings_init(ktech_settings *settings);
bool ktech_settings_validate(const ktech_settings *settings);
bool ktech_settings_from_action_map(ktech_settings *settings,
                                    const kittyin_action_map *map,
                                    bool reduced_motion);
bool ktech_settings_apply_action_map(const ktech_settings *settings,
                                     kittyin_action_map *map);

ktech_settings_result ktech_settings_encode(const ktech_settings *settings,
                                            void *bytes, size_t capacity,
                                            size_t *encoded_size);
ktech_settings_result ktech_settings_decode(ktech_settings *settings,
                                            const void *bytes, size_t size);
ktech_settings_result ktech_settings_save(const ktech_settings *settings,
                                          const char *base_directory);
ktech_settings_result ktech_settings_load(ktech_settings *settings,
                                          const char *base_directory);
ktech_settings_result ktech_settings_path(const char *base_directory,
                                          char *path, size_t path_size);
const char *ktech_settings_result_name(ktech_settings_result result);

#ifdef __cplusplus
}
#endif

#endif
