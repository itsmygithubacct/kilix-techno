#include "kilix_techno/settings.h"

#include "kilix_techno/actions.h"

#include "kilix_state.h"
#include "kilix_state_codec.h"

#include <string.h>

enum {
    SETTINGS_HEADER_SIZE = 32,
    SETTINGS_BINDING_SIZE = 16
};

static const uint8_t settings_magic[8] = {
    'K', 'S', 'E', 'T', 'N', '0', '0', '1'
};

static bool binding_valid(const ktech_settings_binding *binding)
{
    return ktech_binding_validate(binding);
}

static bool binding_equal(const ktech_settings_binding *left,
                          const ktech_settings_binding *right)
{
    return left->key == right->key &&
           left->modifiers == right->modifiers &&
           left->action == right->action && left->kind == right->kind &&
           left->gamepad == right->gamepad &&
           left->control == right->control &&
           left->direction == right->direction;
}

bool ktech_settings_validate(const ktech_settings *settings)
{
    if (!settings ||
        settings->binding_count > KTECH_SETTINGS_BINDING_CAPACITY ||
        settings->repeat_delay_ms < 100u ||
        settings->repeat_delay_ms > 2000u ||
        settings->repeat_interval_ms < 10u ||
        settings->repeat_interval_ms > 1000u ||
        settings->axis_dead_zone > 32767u ||
        settings->reduced_motion > 1u)
        return false;
    for (size_t index = 0u; index < sizeof settings->reserved; ++index)
        if (settings->reserved[index] != 0u) return false;
    for (uint16_t index = 0u; index < settings->binding_count; ++index) {
        if (!binding_valid(&settings->bindings[index])) return false;
        for (uint16_t previous = 0u; previous < index; ++previous)
            if (binding_equal(&settings->bindings[index],
                              &settings->bindings[previous]))
                return false;
    }
    for (uint16_t index = settings->binding_count;
         index < KTECH_SETTINGS_BINDING_CAPACITY; ++index) {
        static const ktech_settings_binding zero;
        if (memcmp(&settings->bindings[index], &zero, sizeof zero) != 0)
            return false;
    }
    return true;
}

bool ktech_settings_from_action_map(ktech_settings *settings,
                                    const kittyin_action_map *map,
                                    bool reduced_motion)
{
    if (!settings || !map ||
        map->binding_count > KTECH_SETTINGS_BINDING_CAPACITY ||
        map->repeat_delay_ms > UINT16_MAX ||
        map->repeat_interval_ms > UINT16_MAX || map->axis_dead_zone < 0)
        return false;
    ktech_settings temporary;
    memset(&temporary, 0, sizeof temporary);
    temporary.binding_count = (uint16_t)map->binding_count;
    temporary.repeat_delay_ms = (uint16_t)map->repeat_delay_ms;
    temporary.repeat_interval_ms = (uint16_t)map->repeat_interval_ms;
    temporary.axis_dead_zone = (uint16_t)map->axis_dead_zone;
    temporary.reduced_motion = reduced_motion ? 1u : 0u;
    for (uint16_t index = 0u; index < temporary.binding_count; ++index) {
        const kittyin_action_binding *source = &map->bindings[index];
        ktech_settings_binding *destination = &temporary.bindings[index];
        destination->key = source->key;
        destination->modifiers = source->modifiers;
        destination->action = source->action;
        destination->kind = source->kind;
        destination->gamepad = source->kind == KITTYIN_BINDING_KEY
                                   ? KITTYIN_GAMEPAD_ANY : source->gamepad;
        destination->control = source->control;
        destination->direction = source->direction;
    }
    if (!ktech_settings_validate(&temporary)) return false;
    *settings = temporary;
    return true;
}

void ktech_settings_init(ktech_settings *settings)
{
    if (!settings) return;
    kittyin_action_map map;
    ktech_actions_bind_defaults(&map);
    (void)ktech_settings_from_action_map(settings, &map, false);
}

bool ktech_settings_apply_action_map(const ktech_settings *settings,
                                     kittyin_action_map *map)
{
    if (!map || !ktech_settings_validate(settings)) return false;
    kittyin_action_map temporary;
    kittyin_action_map_init(&temporary);
    if (!kittyin_action_map_set_repeat(&temporary,
                                       settings->repeat_delay_ms,
                                       settings->repeat_interval_ms) ||
        !kittyin_action_map_set_dead_zone(&temporary,
                                          settings->axis_dead_zone))
        return false;
    for (uint16_t index = 0u; index < settings->binding_count; ++index) {
        const ktech_settings_binding *binding = &settings->bindings[index];
        bool success;
        if (binding->kind == KITTYIN_BINDING_KEY) {
            success = kittyin_action_map_bind_key(
                &temporary, binding->action, binding->key,
                binding->modifiers);
        } else if (binding->kind == KITTYIN_BINDING_GAMEPAD_BUTTON) {
            success = kittyin_action_map_bind_gamepad_button(
                &temporary, binding->action, KITTYIN_GAMEPAD_ANY,
                binding->control);
        } else {
            success = kittyin_action_map_bind_gamepad_axis(
                &temporary, binding->action, KITTYIN_GAMEPAD_ANY,
                binding->control, binding->direction);
        }
        if (!success) return false;
    }
    *map = temporary;
    return true;
}

static size_t encoded_size(const ktech_settings *settings)
{
    return SETTINGS_HEADER_SIZE +
           (size_t)settings->binding_count * SETTINGS_BINDING_SIZE;
}

ktech_settings_result ktech_settings_encode(const ktech_settings *settings,
                                            void *bytes, size_t capacity,
                                            size_t *encoded_size_value)
{
    if (encoded_size_value) *encoded_size_value = 0u;
    if (!bytes || !ktech_settings_validate(settings))
        return KTECH_SETTINGS_INVALID;
    const size_t needed = encoded_size(settings);
    if (needed > KTECH_SETTINGS_ENCODED_MAX || capacity < needed)
        return KTECH_SETTINGS_TOO_LARGE;
    kilixstate_writer writer;
    kilixstate_writer_init(&writer, bytes, capacity);
    if (!kilixstate_write_bytes(&writer, settings_magic,
                                sizeof settings_magic) ||
        !kilixstate_write_u16(&writer, SETTINGS_HEADER_SIZE) ||
        !kilixstate_write_u16(&writer, KTECH_SETTINGS_SCHEMA) ||
        !kilixstate_write_u32(&writer, (uint32_t)needed) ||
        !kilixstate_write_u16(&writer, settings->binding_count) ||
        !kilixstate_write_u16(&writer, settings->repeat_delay_ms) ||
        !kilixstate_write_u16(&writer, settings->repeat_interval_ms) ||
        !kilixstate_write_u16(&writer, settings->axis_dead_zone) ||
        !kilixstate_write_bool(&writer, settings->reduced_motion != 0u) ||
        !kilixstate_write_zeroes(&writer, 7u))
        return KTECH_SETTINGS_TOO_LARGE;
    for (uint16_t index = 0u; index < settings->binding_count; ++index) {
        const ktech_settings_binding *binding = &settings->bindings[index];
        if (!kilixstate_write_u32(&writer, binding->key) ||
            !kilixstate_write_u32(&writer, binding->modifiers) ||
            !kilixstate_write_u16(&writer, binding->action) ||
            !kilixstate_write_u8(&writer, binding->kind) ||
            !kilixstate_write_u8(&writer, binding->gamepad) ||
            !kilixstate_write_u8(&writer, binding->control) ||
            !kilixstate_write_u8(&writer, (uint8_t)binding->direction) ||
            !kilixstate_write_zeroes(&writer, 2u))
            return KTECH_SETTINGS_TOO_LARGE;
    }
    if (kilixstate_writer_size(&writer) != needed)
        return KTECH_SETTINGS_TOO_LARGE;
    if (encoded_size_value) *encoded_size_value = needed;
    return KTECH_SETTINGS_OK;
}

ktech_settings_result ktech_settings_decode(ktech_settings *settings,
                                            const void *bytes, size_t size)
{
    if (!settings || !bytes) return KTECH_SETTINGS_INVALID;
    if (size > KTECH_SETTINGS_ENCODED_MAX) return KTECH_SETTINGS_TOO_LARGE;
    if (size < SETTINGS_HEADER_SIZE) return KTECH_SETTINGS_TRUNCATED;
    ktech_settings temporary;
    memset(&temporary, 0, sizeof temporary);
    kilixstate_reader reader;
    kilixstate_reader_init(&reader, bytes, size);
    uint8_t magic[8], reserved[7];
    uint16_t header_size, schema;
    uint32_t total;
    bool reduced_motion;
    if (!kilixstate_read_bytes(&reader, magic, sizeof magic) ||
        !kilixstate_read_u16(&reader, &header_size) ||
        !kilixstate_read_u16(&reader, &schema) ||
        !kilixstate_read_u32(&reader, &total) ||
        !kilixstate_read_u16(&reader, &temporary.binding_count) ||
        !kilixstate_read_u16(&reader, &temporary.repeat_delay_ms) ||
        !kilixstate_read_u16(&reader, &temporary.repeat_interval_ms) ||
        !kilixstate_read_u16(&reader, &temporary.axis_dead_zone) ||
        !kilixstate_read_bool(&reader, &reduced_motion) ||
        !kilixstate_read_bytes(&reader, reserved, sizeof reserved))
        return KTECH_SETTINGS_TRUNCATED;
    temporary.reduced_motion = reduced_motion ? 1u : 0u;
    if (memcmp(magic, settings_magic, sizeof magic) != 0 ||
        header_size != SETTINGS_HEADER_SIZE || total != size)
        return KTECH_SETTINGS_NONCANONICAL;
    if (schema != KTECH_SETTINGS_SCHEMA)
        return KTECH_SETTINGS_UNKNOWN_VERSION;
    for (size_t index = 0u; index < sizeof reserved; ++index)
        if (reserved[index] != 0u) return KTECH_SETTINGS_NONCANONICAL;
    if (temporary.binding_count > KTECH_SETTINGS_BINDING_CAPACITY ||
        encoded_size(&temporary) != size)
        return KTECH_SETTINGS_NONCANONICAL;
    for (uint16_t index = 0u; index < temporary.binding_count; ++index) {
        ktech_settings_binding *binding = &temporary.bindings[index];
        uint8_t direction, binding_reserved[2];
        if (!kilixstate_read_u32(&reader, &binding->key) ||
            !kilixstate_read_u32(&reader, &binding->modifiers) ||
            !kilixstate_read_u16(&reader, &binding->action) ||
            !kilixstate_read_u8(&reader, &binding->kind) ||
            !kilixstate_read_u8(&reader, &binding->gamepad) ||
            !kilixstate_read_u8(&reader, &binding->control) ||
            !kilixstate_read_u8(&reader, &direction) ||
            !kilixstate_read_bytes(&reader, binding_reserved,
                                   sizeof binding_reserved))
            return KTECH_SETTINGS_TRUNCATED;
        binding->direction = (int8_t)direction;
        if (binding_reserved[0] != 0u || binding_reserved[1] != 0u)
            return KTECH_SETTINGS_NONCANONICAL;
    }
    if (!kilixstate_reader_require_finished(&reader) ||
        !ktech_settings_validate(&temporary))
        return KTECH_SETTINGS_NONCANONICAL;
    *settings = temporary;
    return KTECH_SETTINGS_OK;
}

static void settings_options(kilixstate_options *options,
                             const char *base_directory)
{
    kilixstate_options_init(options);
    options->app_id = "kilix-techno";
    options->filename = "settings.state";
    options->base_directory = base_directory;
    options->max_payload = KTECH_SETTINGS_ENCODED_MAX;
    options->format = KILIXSTATE_FORMAT_CRC32;
}

static ktech_settings_result store_result(kilixstate_result result)
{
    switch (result) {
    case KILIXSTATE_OK: return KTECH_SETTINGS_OK;
    case KILIXSTATE_NOT_FOUND: return KTECH_SETTINGS_NOT_FOUND;
    case KILIXSTATE_TOO_LARGE:
    case KILIXSTATE_BUFFER_TOO_SMALL: return KTECH_SETTINGS_TOO_LARGE;
    case KILIXSTATE_CORRUPT: return KTECH_SETTINGS_CORRUPT;
    case KILIXSTATE_SECURITY: return KTECH_SETTINGS_SECURITY_ERROR;
    default: return KTECH_SETTINGS_IO_ERROR;
    }
}

ktech_settings_result ktech_settings_save(const ktech_settings *settings,
                                          const char *base_directory)
{
    uint8_t bytes[KTECH_SETTINGS_ENCODED_MAX];
    size_t size;
    ktech_settings_result result = ktech_settings_encode(
        settings, bytes, sizeof bytes, &size);
    if (result != KTECH_SETTINGS_OK) return result;
    kilixstate_options options;
    settings_options(&options, base_directory);
    kilixstate_store store;
    kilixstate_result stored = kilixstate_store_init(&store, &options);
    if (stored != KILIXSTATE_OK) return store_result(stored);
    stored = kilixstate_save(&store, bytes, size);
    kilixstate_store_close(&store);
    return store_result(stored);
}

ktech_settings_result ktech_settings_load(ktech_settings *settings,
                                          const char *base_directory)
{
    if (!settings) return KTECH_SETTINGS_INVALID;
    uint8_t bytes[KTECH_SETTINGS_ENCODED_MAX];
    size_t size = 0u;
    kilixstate_options options;
    settings_options(&options, base_directory);
    kilixstate_store store;
    kilixstate_result stored = kilixstate_store_init(&store, &options);
    if (stored != KILIXSTATE_OK) return store_result(stored);
    stored = kilixstate_load(&store, bytes, sizeof bytes, &size);
    kilixstate_store_close(&store);
    if (stored != KILIXSTATE_OK) return store_result(stored);
    return ktech_settings_decode(settings, bytes, size);
}

ktech_settings_result ktech_settings_path(const char *base_directory,
                                          char *path, size_t path_size)
{
    kilixstate_options options;
    settings_options(&options, base_directory);
    kilixstate_store store;
    kilixstate_result stored = kilixstate_store_init(&store, &options);
    if (stored != KILIXSTATE_OK) return store_result(stored);
    stored = kilixstate_store_path(&store, path, path_size);
    kilixstate_store_close(&store);
    return store_result(stored);
}

const char *ktech_settings_result_name(ktech_settings_result result)
{
    static const char *const names[] = {
        "ok", "not-found", "invalid", "too-large", "truncated",
        "corrupt", "unknown-version", "noncanonical", "io-error",
        "security-error"
    };
    return (unsigned)result < sizeof names / sizeof names[0]
               ? names[result] : "unknown";
}
