#include "workstation_internal.h"
#include "kilix_techno/workstation.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { WORKSTATION_HEADER_SIZE = 32 };

static const uint8_t workstation_magic[8] = {
    'K', 'T', 'X', '3', '0', '0', '0', '1'
};

static void set_error(char *error, size_t size, const char *format, ...)
{
    va_list arguments;
    if (!error || size == 0u) return;
    va_start(arguments, format);
    (void)vsnprintf(error, size, format, arguments);
    va_end(arguments);
}

static bool continuation(uint8_t byte)
{
    return byte >= UINT8_C(0x80) && byte <= UINT8_C(0xbf);
}

static bool valid_text(const char *text, size_t capacity, bool allow_empty)
{
    size_t length = strnlen(text, capacity);
    if (length == capacity || (!allow_empty && length == 0u)) return false;
    for (size_t tail = length + 1u; tail < capacity; ++tail)
        if (text[tail] != '\0') return false;
    const uint8_t *bytes = (const uint8_t *)text;
    size_t index = 0u;
    while (index < length) {
        uint8_t first = bytes[index++];
        if (first <= UINT8_C(0x7f)) {
            if (first < UINT8_C(0x20) || first == UINT8_C(0x7f))
                return false;
        } else if (first >= UINT8_C(0xc2) && first <= UINT8_C(0xdf)) {
            if (index >= length || !continuation(bytes[index++])) return false;
        } else if (first >= UINT8_C(0xe0) && first <= UINT8_C(0xef)) {
            if (index + 1u >= length) return false;
            uint8_t second = bytes[index++];
            uint8_t third = bytes[index++];
            if (!continuation(second) || !continuation(third) ||
                (first == UINT8_C(0xe0) && second < UINT8_C(0xa0)) ||
                (first == UINT8_C(0xed) && second > UINT8_C(0x9f)))
                return false;
        } else if (first >= UINT8_C(0xf0) && first <= UINT8_C(0xf4)) {
            if (index + 2u >= length) return false;
            uint8_t second = bytes[index++];
            uint8_t third = bytes[index++];
            uint8_t fourth = bytes[index++];
            if (!continuation(second) || !continuation(third) ||
                !continuation(fourth) ||
                (first == UINT8_C(0xf0) && second < UINT8_C(0x90)) ||
                (first == UINT8_C(0xf4) && second > UINT8_C(0x8f)))
                return false;
        } else {
            return false;
        }
    }
    return true;
}

static bool all_zero(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    for (size_t index = 0u; index < size; ++index)
        if (bytes[index] != 0u) return false;
    return true;
}

static bool valid_step(const ktech_step *step)
{
    return step->enabled <= 1u && step->note <= 127u &&
           step->velocity <= 127u && step->gate >= 1u &&
           step->gate <= 100u && step->probability <= 100u &&
           step->ratchet >= 1u && step->ratchet <= 4u;
}

static ktech_step default_step(ktech_role role)
{
    return (ktech_step){0u, ktech_role_default_note(role), 100u, 80u,
                        100u, 1u};
}

void ktech_workstation_defaults(ktech_project *project)
{
    if (!project) return;
    project->arrangement_selected = UINT16_MAX;
    project->arrangement_queued = UINT16_MAX;
    project->arrangement_audible = UINT16_MAX;
    memset(project->scene_patterns, KTECH_PATTERN_NONE,
           sizeof project->scene_patterns);
    project->bus_count = 1u;
    (void)snprintf(project->buses[0].name,
                   sizeof project->buses[0].name, "%s", "MASTER");
    project->buses[0].level = 100u;
    project->buses[0].pan = 128u;
}

static bool valid_reference(uint8_t reference, uint16_t count)
{
    return reference == KTECH_PATTERN_NONE || reference < count;
}

static bool validate_pattern(const ktech_pattern *pattern)
{
    if (!valid_text(pattern->name, sizeof pattern->name, false) ||
        pattern->role >= KTECH_ROLE_COUNT ||
        !ktech_track_length_allowed(pattern->length) ||
        pattern->reserved[0] != 0u || pattern->reserved[1] != 0u)
        return false;
    ktech_step expected = default_step((ktech_role)pattern->role);
    for (uint8_t step = 0u; step < KTECH_STEP_COUNT; ++step)
        if (!valid_step(&pattern->steps[step]) ||
            (step >= pattern->length &&
             memcmp(&pattern->steps[step], &expected, sizeof expected) != 0))
            return false;
    return true;
}

static bool validate_target(uint8_t target, uint8_t index)
{
    if (target >= KTECH_AUTOMATION_TARGET_COUNT) return false;
    switch ((ktech_automation_target)target) {
    case KTECH_AUTOMATION_TRACK_LEVEL:
    case KTECH_AUTOMATION_TRACK_PAN: return index < KTECH_TRACK_COUNT;
    case KTECH_AUTOMATION_BUS_LEVEL: return index < KTECH_BUS_CAPACITY;
    case KTECH_AUTOMATION_EFFECT_WET: return index < KTECH_EFFECT_CAPACITY;
    case KTECH_AUTOMATION_MACRO: return index < KTECH_MACRO_CAPACITY;
    default: return index == 0u;
    }
}

static bool valid_relative_path(const char *path)
{
    size_t length = strnlen(path, KTECH_RESOURCE_PATH_CAPACITY);
    if (length == 0u || length == KTECH_RESOURCE_PATH_CAPACITY ||
        path[0] == '/' || strstr(path, "//") || strstr(path, "\\") ||
        strcmp(path, ".") == 0 || strcmp(path, "..") == 0 ||
        strncmp(path, "../", 3u) == 0 || strstr(path, "/../") ||
        (length >= 3u && strcmp(path + length - 3u, "/..") == 0))
        return false;
    return valid_text(path, KTECH_RESOURCE_PATH_CAPACITY, false);
}

bool ktech_workstation_validate(const ktech_project *project, char *error,
                                size_t error_size)
{
    if (project->pattern_count > KTECH_PATTERN_CAPACITY ||
        project->arrangement_count > KTECH_ARRANGEMENT_CAPACITY ||
        project->automation_lane_count > KTECH_AUTOMATION_LANE_CAPACITY ||
        project->macro_count > KTECH_MACRO_CAPACITY ||
        project->bus_count == 0u || project->bus_count > KTECH_BUS_CAPACITY ||
        project->export_preset_count > KTECH_EXPORT_PRESET_CAPACITY ||
        project->resource_count > KTECH_RESOURCE_CAPACITY ||
        project->performance_fill > 100u ||
        project->performance_variation > 100u ||
        project->reserved_workstation != 0u) {
        set_error(error, error_size, "workstation count or control is invalid");
        return false;
    }
    for (uint8_t scene = 0u; scene < KTECH_SCENE_COUNT; ++scene)
        for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track)
            if (!valid_reference(project->scene_patterns[scene][track],
                                 project->pattern_count)) {
                set_error(error, error_size, "scene pattern reference is invalid");
                return false;
            }
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track)
        if (project->track_bus[track] >= project->bus_count) {
            set_error(error, error_size, "track bus reference is invalid");
            return false;
        }
    for (uint16_t index = 0u; index < project->pattern_count; ++index)
        if (!validate_pattern(&project->patterns[index])) {
            set_error(error, error_size, "pattern %u is invalid", index);
            return false;
        }
    if (!all_zero(&project->patterns[project->pattern_count],
                  (KTECH_PATTERN_CAPACITY - project->pattern_count) *
                      sizeof project->patterns[0])) {
        set_error(error, error_size, "pattern tail is noncanonical");
        return false;
    }
    uint16_t bars = 0u;
    for (uint16_t index = 0u; index < project->arrangement_count; ++index) {
        const ktech_arrangement_entry *entry = &project->arrangement[index];
        if (entry->scene >= KTECH_SCENE_COUNT || entry->bars == 0u ||
            entry->reserved[0] != 0u || entry->reserved[1] != 0u ||
            (unsigned)bars + entry->bars > KTECH_SONG_BAR_CAPACITY) {
            set_error(error, error_size, "arrangement entry %u is invalid", index);
            return false;
        }
        bars = (uint16_t)(bars + entry->bars);
        for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track)
            if (!valid_reference(entry->patterns[track],
                                 project->pattern_count)) {
                set_error(error, error_size, "arrangement pattern is invalid");
                return false;
            }
    }
    if (!all_zero(&project->arrangement[project->arrangement_count],
                  (KTECH_ARRANGEMENT_CAPACITY - project->arrangement_count) *
                      sizeof project->arrangement[0])) {
        set_error(error, error_size, "arrangement tail is noncanonical");
        return false;
    }
    const uint16_t states[] = {project->arrangement_selected,
                               project->arrangement_queued,
                               project->arrangement_audible};
    for (size_t state = 0u; state < sizeof states / sizeof states[0]; ++state)
        if (states[state] != UINT16_MAX &&
            states[state] >= project->arrangement_count) {
            set_error(error, error_size, "arrangement state is invalid");
            return false;
        }
    unsigned total_points = 0u;
    for (uint8_t index = 0u; index < project->automation_lane_count; ++index) {
        const ktech_automation_lane *lane = &project->automation[index];
        if (!validate_target(lane->target, lane->target_index) ||
            lane->point_count > KTECH_AUTOMATION_POINT_CAPACITY ||
            !all_zero(lane->reserved, sizeof lane->reserved)) {
            set_error(error, error_size, "automation lane %u is invalid", index);
            return false;
        }
        total_points += lane->point_count;
        uint16_t previous = 0u;
        for (uint16_t point = 0u; point < lane->point_count; ++point) {
            const ktech_automation_point *value = &lane->points[point];
            if (value->row >= KTECH_SONG_BAR_CAPACITY * 16u ||
                value->value < -10000 || value->value > 10000 ||
                (point > 0u && value->row <= previous)) {
                set_error(error, error_size, "automation point is invalid");
                return false;
            }
            previous = value->row;
        }
        if (!all_zero(&lane->points[lane->point_count],
                      (KTECH_AUTOMATION_POINT_CAPACITY - lane->point_count) *
                          sizeof lane->points[0])) {
            set_error(error, error_size, "automation point tail is noncanonical");
            return false;
        }
    }
    if (total_points > KTECH_AUTOMATION_POINT_TOTAL_CAPACITY ||
        !all_zero(&project->automation[project->automation_lane_count],
                  (KTECH_AUTOMATION_LANE_CAPACITY -
                   project->automation_lane_count) *
                      sizeof project->automation[0])) {
        set_error(error, error_size, "automation capacity is invalid");
        return false;
    }
    for (uint8_t index = 0u; index < project->macro_count; ++index) {
        const ktech_macro *macro = &project->macros[index];
        if (!valid_text(macro->name, sizeof macro->name, false) ||
            macro->value > 100u ||
            macro->target_count > KTECH_MACRO_TARGET_CAPACITY ||
            !all_zero(macro->reserved, sizeof macro->reserved)) {
            set_error(error, error_size, "macro %u is invalid", index);
            return false;
        }
        for (uint8_t target = 0u; target < macro->target_count; ++target)
            if (!validate_target(macro->targets[target].target,
                                 macro->targets[target].target_index) ||
                macro->targets[target].depth < -100 ||
                macro->targets[target].depth > 100 ||
                macro->targets[target].reserved != 0u) {
                set_error(error, error_size, "macro target is invalid");
                return false;
            }
        if (!all_zero(&macro->targets[macro->target_count],
                      (KTECH_MACRO_TARGET_CAPACITY - macro->target_count) *
                          sizeof macro->targets[0])) {
            set_error(error, error_size, "macro target tail is noncanonical");
            return false;
        }
    }
    if (!all_zero(&project->macros[project->macro_count],
                  (KTECH_MACRO_CAPACITY - project->macro_count) *
                      sizeof project->macros[0])) {
        set_error(error, error_size, "macro tail is noncanonical");
        return false;
    }
    unsigned effects = 0u;
    for (uint8_t index = 0u; index < project->bus_count; ++index) {
        const ktech_bus *bus = &project->buses[index];
        if (!valid_text(bus->name, sizeof bus->name, false) ||
            bus->level > 100u || bus->mute > 1u || bus->solo > 1u ||
            bus->effect_count > KTECH_EFFECT_CHAIN_CAPACITY ||
            !all_zero(bus->reserved, sizeof bus->reserved)) {
            set_error(error, error_size, "bus %u is invalid", index);
            return false;
        }
        effects += bus->effect_count;
        for (uint8_t effect = 0u; effect < bus->effect_count; ++effect) {
            const ktech_effect *value = &bus->effects[effect];
            if (value->kind >= KTECH_EFFECT_KIND_COUNT ||
                value->enabled > 1u || value->wet > 100u ||
                value->parameter_count > 4u ||
                !all_zero(&value->parameters[value->parameter_count],
                          4u - value->parameter_count)) {
                set_error(error, error_size, "effect is invalid");
                return false;
            }
        }
        if (!all_zero(&bus->effects[bus->effect_count],
                      (KTECH_EFFECT_CHAIN_CAPACITY - bus->effect_count) *
                          sizeof bus->effects[0])) {
            set_error(error, error_size, "effect tail is noncanonical");
            return false;
        }
    }
    if (effects > KTECH_EFFECT_CAPACITY ||
        !all_zero(&project->buses[project->bus_count],
                  (KTECH_BUS_CAPACITY - project->bus_count) *
                      sizeof project->buses[0])) {
        set_error(error, error_size, "bus capacity is invalid");
        return false;
    }
    for (uint8_t index = 0u; index < project->export_preset_count; ++index) {
        const ktech_export_preset *preset = &project->export_presets[index];
        if (!valid_text(preset->name, sizeof preset->name, false) ||
            preset->format >= KTECH_EXPORT_FORMAT_COUNT ||
            (preset->bit_depth != 16u && preset->bit_depth != 24u &&
             preset->bit_depth != 32u) || preset->normalize > 1u ||
            preset->include_master > 1u || preset->bars == 0u ||
            preset->bars > KTECH_SONG_BAR_CAPACITY || preset->tail_ms > 10000u) {
            set_error(error, error_size, "export preset is invalid");
            return false;
        }
    }
    if (!all_zero(&project->export_presets[project->export_preset_count],
                  (KTECH_EXPORT_PRESET_CAPACITY -
                   project->export_preset_count) *
                      sizeof project->export_presets[0])) {
        set_error(error, error_size, "preset tail is noncanonical");
        return false;
    }
    for (uint8_t index = 0u; index < project->resource_count; ++index) {
        const ktech_resource *resource = &project->resources[index];
        if (!valid_relative_path(resource->path) ||
            !valid_text(resource->license, sizeof resource->license, false) ||
            all_zero(resource->sha256, sizeof resource->sha256) ||
            resource->bytes == 0u || resource->bytes > 64u * 1024u * 1024u ||
            resource->format >= KTECH_RESOURCE_FORMAT_COUNT ||
            resource->role >= KTECH_ROLE_COUNT ||
            !all_zero(resource->reserved, sizeof resource->reserved)) {
            set_error(error, error_size, "resource %u is invalid", index);
            return false;
        }
    }
    if (!all_zero(&project->resources[project->resource_count],
                  (KTECH_RESOURCE_CAPACITY - project->resource_count) *
                      sizeof project->resources[0])) {
        set_error(error, error_size, "resource tail is noncanonical");
        return false;
    }
    if (error && error_size > 0u) error[0] = '\0';
    return true;
}

static size_t string_size(const char *value)
{
    return 1u + strlen(value);
}

size_t ktech_workstation_encoded_size(const ktech_project *project)
{
    size_t size = WORKSTATION_HEADER_SIZE +
                  sizeof project->scene_patterns + sizeof project->track_bus +
                  sizeof project->mute_groups;
    for (uint16_t index = 0u; index < project->pattern_count; ++index)
        size += string_size(project->patterns[index].name) + 4u +
                KTECH_STEP_COUNT * 6u;
    size += (size_t)project->arrangement_count * 12u;
    for (uint8_t index = 0u; index < project->automation_lane_count; ++index)
        size += 8u + (size_t)project->automation[index].point_count * 4u;
    for (uint8_t index = 0u; index < project->macro_count; ++index)
        size += string_size(project->macros[index].name) + 4u +
                (size_t)project->macros[index].target_count * 4u;
    for (uint8_t index = 0u; index < project->bus_count; ++index)
        size += string_size(project->buses[index].name) + 8u +
                (size_t)project->buses[index].effect_count * 8u;
    for (uint8_t index = 0u; index < project->export_preset_count; ++index)
        size += string_size(project->export_presets[index].name) + 8u;
    for (uint8_t index = 0u; index < project->resource_count; ++index)
        size += string_size(project->resources[index].path) +
                string_size(project->resources[index].license) + 40u;
    return size;
}

static bool write_string(kilixstate_writer *writer, const char *value)
{
    size_t length = strlen(value);
    return length <= UINT8_MAX &&
           kilixstate_write_u8(writer, (uint8_t)length) &&
           kilixstate_write_bytes(writer, value, length);
}

static bool write_step(kilixstate_writer *writer, const ktech_step *step)
{
    return kilixstate_write_bool(writer, step->enabled != 0u) &&
           kilixstate_write_u8(writer, step->note) &&
           kilixstate_write_u8(writer, step->velocity) &&
           kilixstate_write_u8(writer, step->gate) &&
           kilixstate_write_u8(writer, step->probability) &&
           kilixstate_write_u8(writer, step->ratchet);
}

bool ktech_workstation_write(kilixstate_writer *writer,
                             const ktech_project *project)
{
    if (!kilixstate_write_bytes(writer, workstation_magic,
                                sizeof workstation_magic) ||
        !kilixstate_write_u16(writer, WORKSTATION_HEADER_SIZE) ||
        !kilixstate_write_u16(writer, project->pattern_count) ||
        !kilixstate_write_u16(writer, project->arrangement_count) ||
        !kilixstate_write_u8(writer, project->automation_lane_count) ||
        !kilixstate_write_u8(writer, project->macro_count) ||
        !kilixstate_write_u8(writer, project->bus_count) ||
        !kilixstate_write_u8(writer, project->export_preset_count) ||
        !kilixstate_write_u8(writer, project->resource_count) ||
        !kilixstate_write_u8(writer, project->performance_fill) ||
        !kilixstate_write_u8(writer, project->performance_variation) ||
        !kilixstate_write_u8(writer, 0u) ||
        !kilixstate_write_u16(writer, project->arrangement_selected) ||
        !kilixstate_write_u16(writer, project->arrangement_queued) ||
        !kilixstate_write_u16(writer, project->arrangement_audible) ||
        !kilixstate_write_zeroes(writer, 4u) ||
        !kilixstate_write_bytes(writer, project->scene_patterns,
                                sizeof project->scene_patterns) ||
        !kilixstate_write_bytes(writer, project->track_bus,
                                sizeof project->track_bus) ||
        !kilixstate_write_bytes(writer, project->mute_groups,
                                sizeof project->mute_groups))
        return false;
    for (uint16_t index = 0u; index < project->pattern_count; ++index) {
        const ktech_pattern *pattern = &project->patterns[index];
        if (!write_string(writer, pattern->name) ||
            !kilixstate_write_u8(writer, pattern->role) ||
            !kilixstate_write_u8(writer, pattern->length) ||
            !kilixstate_write_zeroes(writer, 2u)) return false;
        for (uint8_t step = 0u; step < KTECH_STEP_COUNT; ++step)
            if (!write_step(writer, &pattern->steps[step])) return false;
    }
    for (uint16_t index = 0u; index < project->arrangement_count; ++index) {
        const ktech_arrangement_entry *entry = &project->arrangement[index];
        if (!kilixstate_write_u8(writer, entry->scene) ||
            !kilixstate_write_u8(writer, entry->bars) ||
            !kilixstate_write_bytes(writer, entry->patterns,
                                    sizeof entry->patterns) ||
            !kilixstate_write_zeroes(writer, 2u)) return false;
    }
    for (uint8_t index = 0u; index < project->automation_lane_count; ++index) {
        const ktech_automation_lane *lane = &project->automation[index];
        if (!kilixstate_write_u8(writer, lane->target) ||
            !kilixstate_write_u8(writer, lane->target_index) ||
            !kilixstate_write_u16(writer, lane->point_count) ||
            !kilixstate_write_zeroes(writer, 4u)) return false;
        for (uint16_t point = 0u; point < lane->point_count; ++point)
            if (!kilixstate_write_u16(writer, lane->points[point].row) ||
                !kilixstate_write_u16(
                    writer, (uint16_t)lane->points[point].value)) return false;
    }
    for (uint8_t index = 0u; index < project->macro_count; ++index) {
        const ktech_macro *macro = &project->macros[index];
        if (!write_string(writer, macro->name) ||
            !kilixstate_write_u8(writer, macro->value) ||
            !kilixstate_write_u8(writer, macro->target_count) ||
            !kilixstate_write_zeroes(writer, 2u)) return false;
        for (uint8_t target = 0u; target < macro->target_count; ++target) {
            const ktech_macro_target *value = &macro->targets[target];
            if (!kilixstate_write_u8(writer, value->target) ||
                !kilixstate_write_u8(writer, value->target_index) ||
                !kilixstate_write_u8(writer, (uint8_t)value->depth) ||
                !kilixstate_write_u8(writer, 0u)) return false;
        }
    }
    for (uint8_t index = 0u; index < project->bus_count; ++index) {
        const ktech_bus *bus = &project->buses[index];
        if (!write_string(writer, bus->name) ||
            !kilixstate_write_u8(writer, bus->level) ||
            !kilixstate_write_u8(writer, bus->pan) ||
            !kilixstate_write_bool(writer, bus->mute != 0u) ||
            !kilixstate_write_bool(writer, bus->solo != 0u) ||
            !kilixstate_write_u8(writer, bus->effect_count) ||
            !kilixstate_write_zeroes(writer, 3u)) return false;
        for (uint8_t effect = 0u; effect < bus->effect_count; ++effect) {
            const ktech_effect *value = &bus->effects[effect];
            if (!kilixstate_write_u8(writer, value->kind) ||
                !kilixstate_write_bool(writer, value->enabled != 0u) ||
                !kilixstate_write_u8(writer, value->wet) ||
                !kilixstate_write_u8(writer, value->parameter_count) ||
                !kilixstate_write_bytes(writer, value->parameters, 4u))
                return false;
        }
    }
    for (uint8_t index = 0u; index < project->export_preset_count; ++index) {
        const ktech_export_preset *preset = &project->export_presets[index];
        if (!write_string(writer, preset->name) ||
            !kilixstate_write_u8(writer, preset->format) ||
            !kilixstate_write_u8(writer, preset->bit_depth) ||
            !kilixstate_write_bool(writer, preset->normalize != 0u) ||
            !kilixstate_write_bool(writer, preset->include_master != 0u) ||
            !kilixstate_write_u16(writer, preset->bars) ||
            !kilixstate_write_u16(writer, preset->tail_ms)) return false;
    }
    for (uint8_t index = 0u; index < project->resource_count; ++index) {
        const ktech_resource *resource = &project->resources[index];
        if (!write_string(writer, resource->path) ||
            !write_string(writer, resource->license) ||
            !kilixstate_write_bytes(writer, resource->sha256, 32u) ||
            !kilixstate_write_u32(writer, resource->bytes) ||
            !kilixstate_write_u8(writer, resource->format) ||
            !kilixstate_write_u8(writer, resource->role) ||
            !kilixstate_write_zeroes(writer, 2u)) return false;
    }
    return true;
}

static bool read_string(kilixstate_reader *reader, char *value,
                        size_t capacity)
{
    uint8_t length;
    if (!kilixstate_read_u8(reader, &length) || length >= capacity ||
        !kilixstate_read_bytes(reader, value, length)) return false;
    value[length] = '\0';
    return true;
}

static bool read_step(kilixstate_reader *reader, ktech_step *step)
{
    bool enabled;
    if (!kilixstate_read_bool(reader, &enabled) ||
        !kilixstate_read_u8(reader, &step->note) ||
        !kilixstate_read_u8(reader, &step->velocity) ||
        !kilixstate_read_u8(reader, &step->gate) ||
        !kilixstate_read_u8(reader, &step->probability) ||
        !kilixstate_read_u8(reader, &step->ratchet)) return false;
    step->enabled = enabled ? 1u : 0u;
    return true;
}

static bool read_zeroes(kilixstate_reader *reader, size_t count)
{
    uint8_t bytes[8];
    if (count > sizeof bytes || !kilixstate_read_bytes(reader, bytes, count))
        return false;
    if (!all_zero(bytes, count)) {
        reader->result = KILIXSTATE_CODEC_NONCANONICAL;
        return false;
    }
    return true;
}

bool ktech_workstation_read(kilixstate_reader *reader,
                            ktech_project *project)
{
    uint8_t magic[8], reserved_byte;
    uint16_t header_size;
    if (!kilixstate_read_bytes(reader, magic, sizeof magic) ||
        !kilixstate_read_u16(reader, &header_size) ||
        !kilixstate_read_u16(reader, &project->pattern_count) ||
        !kilixstate_read_u16(reader, &project->arrangement_count) ||
        !kilixstate_read_u8(reader, &project->automation_lane_count) ||
        !kilixstate_read_u8(reader, &project->macro_count) ||
        !kilixstate_read_u8(reader, &project->bus_count) ||
        !kilixstate_read_u8(reader, &project->export_preset_count) ||
        !kilixstate_read_u8(reader, &project->resource_count) ||
        !kilixstate_read_u8(reader, &project->performance_fill) ||
        !kilixstate_read_u8(reader, &project->performance_variation) ||
        !kilixstate_read_u8(reader, &reserved_byte) ||
        !kilixstate_read_u16(reader, &project->arrangement_selected) ||
        !kilixstate_read_u16(reader, &project->arrangement_queued) ||
        !kilixstate_read_u16(reader, &project->arrangement_audible) ||
        !read_zeroes(reader, 4u)) return false;
    if (memcmp(magic, workstation_magic, sizeof magic) != 0 ||
        header_size != WORKSTATION_HEADER_SIZE || reserved_byte != 0u ||
        project->pattern_count > KTECH_PATTERN_CAPACITY ||
        project->arrangement_count > KTECH_ARRANGEMENT_CAPACITY ||
        project->automation_lane_count > KTECH_AUTOMATION_LANE_CAPACITY ||
        project->macro_count > KTECH_MACRO_CAPACITY ||
        project->bus_count > KTECH_BUS_CAPACITY ||
        project->export_preset_count > KTECH_EXPORT_PRESET_CAPACITY ||
        project->resource_count > KTECH_RESOURCE_CAPACITY) {
        reader->result = KILIXSTATE_CODEC_NONCANONICAL;
        return false;
    }
    if (!kilixstate_read_bytes(reader, project->scene_patterns,
                               sizeof project->scene_patterns) ||
        !kilixstate_read_bytes(reader, project->track_bus,
                               sizeof project->track_bus) ||
        !kilixstate_read_bytes(reader, project->mute_groups,
                               sizeof project->mute_groups)) return false;
    for (uint16_t index = 0u; index < project->pattern_count; ++index) {
        ktech_pattern *pattern = &project->patterns[index];
        if (!read_string(reader, pattern->name, sizeof pattern->name) ||
            !kilixstate_read_u8(reader, &pattern->role) ||
            !kilixstate_read_u8(reader, &pattern->length) ||
            !read_zeroes(reader, 2u)) return false;
        for (uint8_t step = 0u; step < KTECH_STEP_COUNT; ++step)
            if (!read_step(reader, &pattern->steps[step])) return false;
    }
    for (uint16_t index = 0u; index < project->arrangement_count; ++index) {
        ktech_arrangement_entry *entry = &project->arrangement[index];
        if (!kilixstate_read_u8(reader, &entry->scene) ||
            !kilixstate_read_u8(reader, &entry->bars) ||
            !kilixstate_read_bytes(reader, entry->patterns,
                                   sizeof entry->patterns) ||
            !read_zeroes(reader, 2u)) return false;
    }
    for (uint8_t index = 0u; index < project->automation_lane_count; ++index) {
        ktech_automation_lane *lane = &project->automation[index];
        if (!kilixstate_read_u8(reader, &lane->target) ||
            !kilixstate_read_u8(reader, &lane->target_index) ||
            !kilixstate_read_u16(reader, &lane->point_count) ||
            !read_zeroes(reader, 4u) ||
            lane->point_count > KTECH_AUTOMATION_POINT_CAPACITY) return false;
        for (uint16_t point = 0u; point < lane->point_count; ++point) {
            uint16_t signed_value;
            if (!kilixstate_read_u16(reader, &lane->points[point].row) ||
                !kilixstate_read_u16(reader, &signed_value)) return false;
            lane->points[point].value = (int16_t)signed_value;
        }
    }
    for (uint8_t index = 0u; index < project->macro_count; ++index) {
        ktech_macro *macro = &project->macros[index];
        if (!read_string(reader, macro->name, sizeof macro->name) ||
            !kilixstate_read_u8(reader, &macro->value) ||
            !kilixstate_read_u8(reader, &macro->target_count) ||
            !read_zeroes(reader, 2u) ||
            macro->target_count > KTECH_MACRO_TARGET_CAPACITY) return false;
        for (uint8_t target = 0u; target < macro->target_count; ++target) {
            uint8_t depth, reserved;
            if (!kilixstate_read_u8(reader, &macro->targets[target].target) ||
                !kilixstate_read_u8(reader,
                                    &macro->targets[target].target_index) ||
                !kilixstate_read_u8(reader, &depth) ||
                !kilixstate_read_u8(reader, &reserved) || reserved != 0u)
                return false;
            macro->targets[target].depth = (int8_t)depth;
        }
    }
    for (uint8_t index = 0u; index < project->bus_count; ++index) {
        ktech_bus *bus = &project->buses[index];
        bool mute, solo;
        if (!read_string(reader, bus->name, sizeof bus->name) ||
            !kilixstate_read_u8(reader, &bus->level) ||
            !kilixstate_read_u8(reader, &bus->pan) ||
            !kilixstate_read_bool(reader, &mute) ||
            !kilixstate_read_bool(reader, &solo) ||
            !kilixstate_read_u8(reader, &bus->effect_count) ||
            !read_zeroes(reader, 3u) ||
            bus->effect_count > KTECH_EFFECT_CHAIN_CAPACITY) return false;
        bus->mute = mute ? 1u : 0u;
        bus->solo = solo ? 1u : 0u;
        for (uint8_t effect = 0u; effect < bus->effect_count; ++effect) {
            ktech_effect *value = &bus->effects[effect];
            bool enabled;
            if (!kilixstate_read_u8(reader, &value->kind) ||
                !kilixstate_read_bool(reader, &enabled) ||
                !kilixstate_read_u8(reader, &value->wet) ||
                !kilixstate_read_u8(reader, &value->parameter_count) ||
                !kilixstate_read_bytes(reader, value->parameters, 4u))
                return false;
            value->enabled = enabled ? 1u : 0u;
        }
    }
    for (uint8_t index = 0u; index < project->export_preset_count; ++index) {
        ktech_export_preset *preset = &project->export_presets[index];
        bool normalize, include_master;
        if (!read_string(reader, preset->name, sizeof preset->name) ||
            !kilixstate_read_u8(reader, &preset->format) ||
            !kilixstate_read_u8(reader, &preset->bit_depth) ||
            !kilixstate_read_bool(reader, &normalize) ||
            !kilixstate_read_bool(reader, &include_master) ||
            !kilixstate_read_u16(reader, &preset->bars) ||
            !kilixstate_read_u16(reader, &preset->tail_ms)) return false;
        preset->normalize = normalize ? 1u : 0u;
        preset->include_master = include_master ? 1u : 0u;
    }
    for (uint8_t index = 0u; index < project->resource_count; ++index) {
        ktech_resource *resource = &project->resources[index];
        if (!read_string(reader, resource->path, sizeof resource->path) ||
            !read_string(reader, resource->license,
                         sizeof resource->license) ||
            !kilixstate_read_bytes(reader, resource->sha256, 32u) ||
            !kilixstate_read_u32(reader, &resource->bytes) ||
            !kilixstate_read_u8(reader, &resource->format) ||
            !kilixstate_read_u8(reader, &resource->role) ||
            !read_zeroes(reader, 2u)) return false;
    }
    return true;
}

bool ktech_arrangement_set_state(ktech_project *project, uint16_t selected,
                                 uint16_t queued, uint16_t audible)
{
    if (!project ||
        (selected != UINT16_MAX && selected >= project->arrangement_count) ||
        (queued != UINT16_MAX && queued >= project->arrangement_count) ||
        (audible != UINT16_MAX && audible >= project->arrangement_count))
        return false;
    project->arrangement_selected = selected;
    project->arrangement_queued = queued;
    project->arrangement_audible = audible;
    return true;
}

bool ktech_automation_lane_add(ktech_project *project,
                               ktech_automation_target target,
                               uint8_t target_index, uint8_t *lane_out)
{
    if (!project || project->automation_lane_count >=
                        KTECH_AUTOMATION_LANE_CAPACITY ||
        !validate_target((uint8_t)target, target_index)) return false;
    uint8_t index = project->automation_lane_count;
    ktech_automation_lane *lane = &project->automation[index];
    memset(lane, 0, sizeof *lane);
    lane->target = (uint8_t)target;
    lane->target_index = target_index;
    ++project->automation_lane_count;
    if (lane_out) *lane_out = index;
    return true;
}

bool ktech_automation_point_set(ktech_project *project, uint8_t lane_index,
                                uint16_t row, int16_t value)
{
    if (!project || lane_index >= project->automation_lane_count ||
        row >= KTECH_SONG_BAR_CAPACITY * 16u ||
        value < -10000 || value > 10000) return false;
    ktech_automation_lane *lane = &project->automation[lane_index];
    uint16_t at = 0u;
    while (at < lane->point_count && lane->points[at].row < row) ++at;
    if (at < lane->point_count && lane->points[at].row == row) {
        lane->points[at].value = value;
        return true;
    }
    if (lane->point_count >= KTECH_AUTOMATION_POINT_CAPACITY) return false;
    memmove(&lane->points[at + 1u], &lane->points[at],
            (size_t)(lane->point_count - at) * sizeof lane->points[0]);
    lane->points[at] = (ktech_automation_point){row, value};
    ++lane->point_count;
    return true;
}

bool ktech_macro_add(ktech_project *project, const char *name,
                     uint8_t *macro_out)
{
    if (!project || !name || name[0] == '\0' ||
        strnlen(name, sizeof project->macros[0].name) ==
            sizeof project->macros[0].name ||
        project->macro_count >= KTECH_MACRO_CAPACITY) return false;
    uint8_t index = project->macro_count;
    ktech_macro *macro = &project->macros[index];
    memset(macro, 0, sizeof *macro);
    (void)snprintf(macro->name, sizeof macro->name, "%s", name);
    macro->value = 50u;
    ++project->macro_count;
    if (!ktech_workstation_validate(project, NULL, 0u)) {
        --project->macro_count;
        memset(macro, 0, sizeof *macro);
        return false;
    }
    if (macro_out) *macro_out = index;
    return true;
}

bool ktech_macro_target_add(ktech_project *project, uint8_t macro_index,
                            ktech_automation_target target,
                            uint8_t target_index, int8_t depth)
{
    if (!project || macro_index >= project->macro_count ||
        !validate_target((uint8_t)target, target_index) ||
        depth < -100 || depth > 100) return false;
    ktech_macro *macro = &project->macros[macro_index];
    if (macro->target_count >= KTECH_MACRO_TARGET_CAPACITY) return false;
    macro->targets[macro->target_count++] = (ktech_macro_target){
        (uint8_t)target, target_index, depth, 0u
    };
    return true;
}

bool ktech_macro_set_value(ktech_project *project, uint8_t macro,
                           uint8_t value)
{
    if (!project || macro >= project->macro_count || value > 100u)
        return false;
    project->macros[macro].value = value;
    return true;
}

bool ktech_bus_add(ktech_project *project, const char *name, uint8_t *bus_out)
{
    if (!project || !name || name[0] == '\0' ||
        strnlen(name, sizeof project->buses[0].name) ==
            sizeof project->buses[0].name ||
        project->bus_count >= KTECH_BUS_CAPACITY) return false;
    uint8_t index = project->bus_count;
    ktech_bus *bus = &project->buses[index];
    memset(bus, 0, sizeof *bus);
    (void)snprintf(bus->name, sizeof bus->name, "%s", name);
    bus->level = 100u;
    bus->pan = 128u;
    ++project->bus_count;
    if (!ktech_workstation_validate(project, NULL, 0u)) {
        --project->bus_count;
        memset(bus, 0, sizeof *bus);
        return false;
    }
    if (bus_out) *bus_out = index;
    return true;
}

bool ktech_bus_route_track(ktech_project *project, uint8_t track, uint8_t bus)
{
    if (!project || track >= KTECH_TRACK_COUNT || bus >= project->bus_count)
        return false;
    project->track_bus[track] = bus;
    return true;
}

bool ktech_bus_effect_add(ktech_project *project, uint8_t bus_index,
                          ktech_effect_kind kind, uint8_t wet,
                          const uint8_t *parameters,
                          uint8_t parameter_count)
{
    if (!project || bus_index >= project->bus_count ||
        kind >= KTECH_EFFECT_KIND_COUNT || wet > 100u ||
        parameter_count > 4u || (parameter_count > 0u && !parameters))
        return false;
    unsigned total = 0u;
    for (uint8_t bus = 0u; bus < project->bus_count; ++bus)
        total += project->buses[bus].effect_count;
    ktech_bus *bus = &project->buses[bus_index];
    if (total >= KTECH_EFFECT_CAPACITY ||
        bus->effect_count >= KTECH_EFFECT_CHAIN_CAPACITY) return false;
    ktech_effect *effect = &bus->effects[bus->effect_count];
    memset(effect, 0, sizeof *effect);
    effect->kind = (uint8_t)kind;
    effect->enabled = 1u;
    effect->wet = wet;
    effect->parameter_count = parameter_count;
    if (parameter_count > 0u)
        memcpy(effect->parameters, parameters, parameter_count);
    ++bus->effect_count;
    return true;
}

bool ktech_performance_set(ktech_project *project, uint8_t fill,
                           uint8_t variation)
{
    if (!project || fill > 100u || variation > 100u) return false;
    project->performance_fill = fill;
    project->performance_variation = variation;
    return true;
}

bool ktech_mute_group_set(ktech_project *project, uint8_t group,
                          uint8_t track_mask)
{
    if (!project || group >= KTECH_MACRO_CAPACITY) return false;
    project->mute_groups[group] = track_mask;
    return true;
}

bool ktech_mute_group_apply(ktech_project *project, uint8_t scene,
                            uint8_t group, bool mute)
{
    if (!project || scene >= KTECH_SCENE_COUNT ||
        group >= KTECH_MACRO_CAPACITY) return false;
    uint8_t mask = project->mute_groups[group];
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track)
        if ((mask & (uint8_t)(1u << track)) != 0u)
            project->scenes[scene].tracks[track].mute = mute ? 1u : 0u;
    return true;
}

bool ktech_export_preset_add(ktech_project *project, const char *name,
                             ktech_export_format format, uint8_t bit_depth,
                             bool normalize, bool include_master,
                             uint16_t bars, uint16_t tail_ms)
{
    if (!project || !name || name[0] == '\0' ||
        strnlen(name, sizeof project->export_presets[0].name) ==
            sizeof project->export_presets[0].name ||
        project->export_preset_count >= KTECH_EXPORT_PRESET_CAPACITY ||
        format >= KTECH_EXPORT_FORMAT_COUNT ||
        (bit_depth != 16u && bit_depth != 24u && bit_depth != 32u) ||
        bars == 0u || bars > KTECH_SONG_BAR_CAPACITY || tail_ms > 10000u)
        return false;
    ktech_export_preset *preset =
        &project->export_presets[project->export_preset_count];
    memset(preset, 0, sizeof *preset);
    (void)snprintf(preset->name, sizeof preset->name, "%s", name);
    preset->format = (uint8_t)format;
    preset->bit_depth = bit_depth;
    preset->normalize = normalize ? 1u : 0u;
    preset->include_master = include_master ? 1u : 0u;
    preset->bars = bars;
    preset->tail_ms = tail_ms;
    ++project->export_preset_count;
    return true;
}

bool ktech_resource_add(ktech_project *project, const char *relative_path,
                        const char *license, const uint8_t sha256[32],
                        uint32_t bytes, ktech_resource_format format,
                        ktech_role role)
{
    if (!project || !relative_path || !license || !sha256 ||
        project->resource_count >= KTECH_RESOURCE_CAPACITY)
        return false;
    uint8_t index = project->resource_count;
    ktech_resource *resource = &project->resources[index];
    if (strnlen(relative_path, sizeof resource->path) ==
            sizeof resource->path ||
        strnlen(license, sizeof resource->license) ==
            sizeof resource->license) return false;
    memset(resource, 0, sizeof *resource);
    (void)snprintf(resource->path, sizeof resource->path, "%s",
                   relative_path);
    (void)snprintf(resource->license, sizeof resource->license, "%s",
                   license);
    memcpy(resource->sha256, sha256, sizeof resource->sha256);
    resource->bytes = bytes;
    resource->format = (uint8_t)format;
    resource->role = (uint8_t)role;
    ++project->resource_count;
    if (!ktech_workstation_validate(project, NULL, 0u)) {
        --project->resource_count;
        memset(resource, 0, sizeof *resource);
        return false;
    }
    return true;
}

static int lane_value(const ktech_automation_lane *lane, uint16_t row,
                      int base_value)
{
    if (lane->point_count == 0u || row < lane->points[0].row)
        return base_value;
    if (row >= lane->points[lane->point_count - 1u].row)
        return lane->points[lane->point_count - 1u].value / 100;
    uint16_t upper = 1u;
    while (upper < lane->point_count && lane->points[upper].row < row)
        ++upper;
    const ktech_automation_point *left = &lane->points[upper - 1u];
    const ktech_automation_point *right = &lane->points[upper];
    int32_t span = (int32_t)right->row - left->row;
    int32_t offset = (int32_t)row - left->row;
    int32_t value = left->value +
        ((int32_t)right->value - left->value) * offset / span;
    return value / 100;
}

int ktech_control_evaluate(const ktech_project *project,
                           ktech_automation_target target,
                           uint8_t target_index, uint16_t row,
                           int base_value)
{
    if (!project || target >= KTECH_AUTOMATION_TARGET_COUNT ||
        row >= KTECH_SONG_BAR_CAPACITY * 16u) return base_value;
    int value = base_value;
    for (uint8_t lane = 0u; lane < project->automation_lane_count; ++lane)
        if (project->automation[lane].target == (uint8_t)target &&
            project->automation[lane].target_index == target_index)
            value = lane_value(&project->automation[lane], row, value);
    for (uint8_t macro = 0u; macro < project->macro_count; ++macro)
        for (uint8_t mapped = 0u;
             mapped < project->macros[macro].target_count; ++mapped) {
            const ktech_macro_target *mapping =
                &project->macros[macro].targets[mapped];
            if (mapping->target == (uint8_t)target &&
                mapping->target_index == target_index) {
                int centered = (int)project->macros[macro].value - 50;
                value += centered * mapping->depth / 50;
            }
        }
    int maximum = target == KTECH_AUTOMATION_TRACK_PAN ? 255 : 100;
    if (value < 0) value = 0;
    if (value > maximum) value = maximum;
    return value;
}
