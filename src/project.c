#include "kilix_techno/project.h"

#include "kilix_state.h"
#include "kilix_state_codec.h"
#include "kitty_input.h"
#include "kitty_keyboard.h"
#include "workstation_internal.h"

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

enum {
    PROJECT_HEADER_SIZE = 40,
    TRACK_PREFIX_SIZE = 8,
    STEP_SIZE = 6,
    BINDING_SIZE = 16
};

static const uint8_t project_magic[8] = {
    'K', 'T', 'E', 'C', 'H', 'N', '0', '1'
};

static const uint8_t allowed_lengths[] = {
    1u, 2u, 3u, 4u, 6u, 8u, 12u, 16u, 24u, 32u, 48u, 64u
};

static const uint8_t role_default_notes[KTECH_ROLE_COUNT] = {
    36u, 38u, 39u, 42u, 46u, 48u, 36u, 60u, 60u,
    36u, 38u, 39u, 42u, 46u, 48u,
    36u, 38u, 42u, 46u, 48u, 33u,
    36u, 48u,
    60u, 60u, 60u, 60u, 48u, 60u,
    45u, 60u, 53u, 43u,
    60u, 60u, 48u, 60u,
    36u, 38u, 39u, 42u, 46u, 51u,
    36u, 38u, 60u, 60u,
    60u, 60u, 60u, 60u,
    60u, 36u, 60u, 60u
};

uint8_t ktech_role_default_note(ktech_role role)
{
    return role < KTECH_ROLE_COUNT ? role_default_notes[role] : 60u;
}

#define KEY_BIND(action_value, key_value, modifier_value) \
    {(key_value), (modifier_value), (action_value), KTECH_BINDING_KEY, \
     KTECH_ANY_GAMEPAD, 0u, 0, {0u, 0u}}
#define BUTTON_BIND(action_value, button_value) \
    {0u, 0u, (action_value), KTECH_BINDING_GAMEPAD_BUTTON, \
     KTECH_ANY_GAMEPAD, (button_value), 0, {0u, 0u}}
#define AXIS_BIND(action_value, axis_value, direction_value) \
    {0u, 0u, (action_value), KTECH_BINDING_GAMEPAD_AXIS, \
     KTECH_ANY_GAMEPAD, (axis_value), (direction_value), {0u, 0u}}

static const ktech_binding default_bindings[] = {
    KEY_BIND(KTECH_ACTION_LEFT, KITTYKB_KEY_LEFT, 0u),
    KEY_BIND(KTECH_ACTION_RIGHT, KITTYKB_KEY_RIGHT, 0u),
    KEY_BIND(KTECH_ACTION_UP, KITTYKB_KEY_UP, 0u),
    KEY_BIND(KTECH_ACTION_DOWN, KITTYKB_KEY_DOWN, 0u),
    KEY_BIND(KTECH_ACTION_NEXT_TRACK, KITTYKB_KEY_TAB, 0u),
    KEY_BIND(KTECH_ACTION_PREVIOUS_TRACK, (uint32_t)'[', 0u),
    KEY_BIND(KTECH_ACTION_NEXT_TRACK, (uint32_t)']', 0u),
    KEY_BIND(KTECH_ACTION_ACTIVATE, KITTYKB_KEY_ENTER, 0u),
    KEY_BIND(KTECH_ACTION_DECREASE, (uint32_t)'-', 0u),
    KEY_BIND(KTECH_ACTION_INCREASE, (uint32_t)'+', 0u),
    KEY_BIND(KTECH_ACTION_INCREASE, (uint32_t)'+', KITTYKB_MOD_SHIFT),
    KEY_BIND(KTECH_ACTION_PLAY_PAUSE, (uint32_t)' ', 0u),
    KEY_BIND(KTECH_ACTION_STOP, (uint32_t)'.', 0u),
    KEY_BIND(KTECH_ACTION_RESTART, (uint32_t)' ', KITTYKB_MOD_CTRL),
    KEY_BIND(KTECH_ACTION_TAP_TEMPO, (uint32_t)'t', 0u),
    KEY_BIND(KTECH_ACTION_MUTE, (uint32_t)'m', 0u),
    KEY_BIND(KTECH_ACTION_SOLO, (uint32_t)'s', 0u),
    KEY_BIND(KTECH_ACTION_SOLO, (uint32_t)'M', 0u),
    KEY_BIND(KTECH_ACTION_SOLO, (uint32_t)'m', KITTYKB_MOD_SHIFT),
    KEY_BIND(KTECH_ACTION_UNDO, (uint32_t)'u', 0u),
    KEY_BIND(KTECH_ACTION_REDO, (uint32_t)'r', 0u),
    KEY_BIND(KTECH_ACTION_UNDO, (uint32_t)'z', KITTYKB_MOD_CTRL),
    KEY_BIND(KTECH_ACTION_REDO, (uint32_t)'y', KITTYKB_MOD_CTRL),
    KEY_BIND(KTECH_ACTION_SAVE, (uint32_t)'s', KITTYKB_MOD_CTRL),
    KEY_BIND(KTECH_ACTION_EXPORT, (uint32_t)'e', KITTYKB_MOD_CTRL),
    KEY_BIND(KTECH_ACTION_HELP, KITTYKB_KEY_F1, 0u),
    KEY_BIND(KTECH_ACTION_HELP, (uint32_t)'?', 0u),
    KEY_BIND(KTECH_ACTION_HELP, (uint32_t)'?', KITTYKB_MOD_SHIFT),
    KEY_BIND(KTECH_ACTION_QUIT, (uint32_t)'q', 0u),
    KEY_BIND(KTECH_ACTION_PREVIOUS_SCENE, (uint32_t)',', 0u),
    KEY_BIND(KTECH_ACTION_NEXT_SCENE, (uint32_t)'>', 0u),
    KEY_BIND(KTECH_ACTION_NEXT_SCENE, (uint32_t)'>', KITTYKB_MOD_SHIFT),
    KEY_BIND(KTECH_ACTION_LAUNCH_SCENE, (uint32_t)'l', 0u),
    AXIS_BIND(KTECH_ACTION_LEFT, 0u, -1),
    AXIS_BIND(KTECH_ACTION_RIGHT, 0u, 1),
    AXIS_BIND(KTECH_ACTION_UP, 1u, -1),
    AXIS_BIND(KTECH_ACTION_DOWN, 1u, 1),
    BUTTON_BIND(KTECH_ACTION_ACTIVATE, 0u),
    BUTTON_BIND(KTECH_ACTION_PLAY_PAUSE, 7u),
    BUTTON_BIND(KTECH_ACTION_QUIT, 1u),
    BUTTON_BIND(KTECH_ACTION_PREVIOUS_TRACK, 4u),
    BUTTON_BIND(KTECH_ACTION_NEXT_TRACK, 5u)
};

#undef KEY_BIND
#undef BUTTON_BIND
#undef AXIS_BIND

static ktech_step default_step(uint8_t note);

static uint32_t binding_modifier_mask(void)
{
    return KITTYKB_MOD_SHIFT | KITTYKB_MOD_ALT | KITTYKB_MOD_CTRL |
           KITTYKB_MOD_SUPER | KITTYKB_MOD_HYPER | KITTYKB_MOD_META;
}

bool ktech_binding_validate(const ktech_binding *binding)
{
    if (!binding || binding->action >= KTECH_ACTION_COUNT ||
        binding->kind < KTECH_BINDING_KEY ||
        binding->kind > KTECH_BINDING_GAMEPAD_AXIS ||
        binding->reserved[0] != 0u || binding->reserved[1] != 0u)
        return false;
    if (binding->kind == KTECH_BINDING_KEY)
        return binding->key != 0u && binding->gamepad == KTECH_ANY_GAMEPAD &&
               (binding->modifiers & ~binding_modifier_mask()) == 0u &&
               binding->control == 0u && binding->direction == 0;
    if (binding->key != 0u || binding->modifiers != 0u ||
        binding->gamepad != KTECH_ANY_GAMEPAD)
        return false;
    return binding->kind == KTECH_BINDING_GAMEPAD_BUTTON
               ? binding->direction == 0
               : (binding->direction == -1 || binding->direction == 1);
}

static bool binding_equal(const ktech_binding *left,
                          const ktech_binding *right)
{
    return left->key == right->key &&
           left->modifiers == right->modifiers &&
           left->action == right->action && left->kind == right->kind &&
           left->gamepad == right->gamepad &&
           left->control == right->control &&
           left->direction == right->direction;
}

static void set_error(char *error, size_t size, const char *format, ...)
{
    va_list arguments;

    if (!error || size == 0u) return;
    va_start(arguments, format);
    (void)vsnprintf(error, size, format, arguments);
    va_end(arguments);
}

bool ktech_track_length_allowed(uint8_t length)
{
    for (size_t index = 0u;
         index < sizeof allowed_lengths / sizeof allowed_lengths[0]; ++index)
        if (allowed_lengths[index] == length) return true;
    return false;
}

bool ktech_project_set_track_length(ktech_project *project, uint8_t scene,
                                    uint8_t track, uint8_t length)
{
    if (!project || scene >= KTECH_SCENE_COUNT || track >= KTECH_TRACK_COUNT ||
        !ktech_track_length_allowed(length))
        return false;
    ktech_track *current = &project->scenes[scene].tracks[track];
    if (current->role >= KTECH_ROLE_COUNT) return false;
    current->length = length;
    for (uint8_t step = length; step < KTECH_STEP_COUNT; ++step)
        current->steps[step] =
            default_step(role_default_notes[current->role]);
    return true;
}

bool ktech_project_set_track_role(ktech_project *project, uint8_t scene,
                                  uint8_t track, ktech_role role)
{
    if (!project || scene >= KTECH_SCENE_COUNT || track >= KTECH_TRACK_COUNT ||
        role >= KTECH_ROLE_COUNT)
        return false;
    ktech_track *current = &project->scenes[scene].tracks[track];
    if (!ktech_track_length_allowed(current->length)) return false;
    current->role = (uint8_t)role;
    for (uint8_t step = current->length; step < KTECH_STEP_COUNT; ++step)
        current->steps[step] = default_step(role_default_notes[role]);
    return true;
}

const char *ktech_role_name(ktech_role role)
{
    static const char *const names[KTECH_ROLE_COUNT] = {
        "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Percussion",
        "Bass", "Chord Stab", "Texture",
        "808 Kick", "808 Snare", "808 Clap", "808 Closed Hat",
        "808 Open Hat", "808 Cowbell",
        "Stargate Kick", "Stargate Snare", "Stargate Closed Hat",
        "Stargate Open Hat", "Stargate FM Perc", "Stargate Sub",
        "Forge Growl", "Forge House Stab",
        "Mech Pluck Bass", "Mech Fuzz Bass", "Mech Wobble",
        "Mech Time Pad", "Mech Industrial Hit", "Mech Glitch Vox",
        "Karoryfer A2", "Karoryfer C4", "Karoryfer F3", "Karoryfer G2",
        "VCSL Clavisynth", "VCSL FM Piano", "VCSL Brake Drum",
        "VCSL Ocean Drum",
        "Ongenet Kick", "Ongenet Snare", "Ongenet Clap",
        "Ongenet Closed Hat", "Ongenet Open Hat", "Ongenet Ride",
        "VCSL Acoustic Kick", "VCSL Acoustic Snare", "VCSL Strumstick",
        "VCSL Kawai Piano",
        "VSCO Violin Pizz", "VSCO Cello Pizz", "VSCO Clarinet Stac",
        "VSCO Horn Stac",
        "ChaosBank Piano", "JNS Synth Bass", "Masterpiece Pad",
        "Unison Lead"
    };

    return role < KTECH_ROLE_COUNT ? names[role] : "Unknown";
}

ktech_role ktech_role_fallback(ktech_role role)
{
    if (role < KTECH_NATIVE_ROLE_COUNT) return role;
    switch (role) {
    case KTECH_ROLE_TIDAL_KICK:
    case KTECH_ROLE_STARGATE_KICK:
    case KTECH_ROLE_ONGENET_KICK:
    case KTECH_ROLE_VCSL_ACOUSTIC_KICK:
        return KTECH_ROLE_KICK;
    case KTECH_ROLE_TIDAL_SNARE:
    case KTECH_ROLE_STARGATE_SNARE:
    case KTECH_ROLE_ONGENET_SNARE:
    case KTECH_ROLE_VCSL_ACOUSTIC_SNARE:
        return KTECH_ROLE_SNARE;
    case KTECH_ROLE_TIDAL_CLAP:
    case KTECH_ROLE_ONGENET_CLAP:
        return KTECH_ROLE_CLAP;
    case KTECH_ROLE_TIDAL_CLOSED_HAT:
    case KTECH_ROLE_STARGATE_CLOSED_HAT:
    case KTECH_ROLE_ONGENET_CLOSED_HAT:
        return KTECH_ROLE_CLOSED_HAT;
    case KTECH_ROLE_TIDAL_OPEN_HAT:
    case KTECH_ROLE_STARGATE_OPEN_HAT:
    case KTECH_ROLE_ONGENET_OPEN_HAT:
        return KTECH_ROLE_OPEN_HAT;
    case KTECH_ROLE_TIDAL_COWBELL:
    case KTECH_ROLE_STARGATE_FM_PERC:
    case KTECH_ROLE_MECH_INDUSTRIAL_HIT:
    case KTECH_ROLE_VCSL_BRAKE_DRUM:
    case KTECH_ROLE_ONGENET_RIDE:
        return KTECH_ROLE_PERCUSSION;
    case KTECH_ROLE_STARGATE_SUB:
    case KTECH_ROLE_FORGE_GROWL:
    case KTECH_ROLE_MECH_PLUCK_BASS:
    case KTECH_ROLE_MECH_FUZZ_BASS:
    case KTECH_ROLE_MECH_WOBBLE:
    case KTECH_ROLE_KARORYFER_A2:
    case KTECH_ROLE_KARORYFER_F3:
    case KTECH_ROLE_KARORYFER_G2:
    case KTECH_ROLE_SF2_JNS_SYNTH_BASS:
        return KTECH_ROLE_BASS;
    case KTECH_ROLE_FORGE_HOUSE_STAB:
    case KTECH_ROLE_KARORYFER_C4:
    case KTECH_ROLE_VCSL_CLAVISYNTH:
    case KTECH_ROLE_VCSL_FM_PIANO:
    case KTECH_ROLE_VCSL_STRUMSTICK:
    case KTECH_ROLE_VCSL_KAWAI_PIANO:
    case KTECH_ROLE_VSCO_CLARINET_STAC:
    case KTECH_ROLE_VSCO_HORN_STAC:
    case KTECH_ROLE_SF2_CHAOS_PIANO:
        return KTECH_ROLE_CHORD;
    case KTECH_ROLE_MECH_TIME_PAD:
    case KTECH_ROLE_MECH_GLITCH_VOX:
    case KTECH_ROLE_VCSL_OCEAN_DRUM:
    case KTECH_ROLE_VSCO_VIOLIN_PIZZ:
    case KTECH_ROLE_VSCO_CELLO_PIZZ:
    case KTECH_ROLE_SF2_MASTERPIECE_PAD:
    case KTECH_ROLE_SF2_UNISON_LEAD:
        return KTECH_ROLE_TEXTURE;
    default:
        return KTECH_ROLE_TEXTURE;
    }
}

static ktech_step default_step(uint8_t note)
{
    ktech_step step = {0u, note, 100u, 80u, 100u, 1u};
    return step;
}

void ktech_project_init(ktech_project *project, uint32_t seed)
{
    if (!project) return;
    memset(project, 0, sizeof *project);
    ktech_workstation_defaults(project);
    project->seed = seed ? seed : UINT32_C(0x4b544543);
    project->tempo = 126u;
    project->swing = 54u;
    project->master_level = 84u;
    project->master_tone = 72u;
    project->master_drive = 18u;
    project->master_delay = 20u;
    project->launch_quantization = KTECH_LAUNCH_BEAT;
    project->binding_count = (uint16_t)(
        sizeof default_bindings / sizeof default_bindings[0]);
    memcpy(project->bindings, default_bindings, sizeof default_bindings);
    (void)snprintf(project->name, sizeof project->name, "%s", "New Circuit");

    for (uint8_t scene = 0u; scene < KTECH_SCENE_COUNT; ++scene) {
        for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
            ktech_track *current = &project->scenes[scene].tracks[track];
            static const uint8_t instruments[KTECH_TRACK_COUNT] = {
                KTECH_ROLE_ONGENET_KICK,
                KTECH_ROLE_ONGENET_SNARE,
                KTECH_ROLE_ONGENET_CLAP,
                KTECH_ROLE_ONGENET_CLOSED_HAT,
                KTECH_ROLE_ONGENET_OPEN_HAT,
                KTECH_ROLE_ONGENET_RIDE,
                KTECH_ROLE_SF2_JNS_SYNTH_BASS,
                KTECH_ROLE_VCSL_STRUMSTICK
            };
            current->role = instruments[track];
            current->length = track == 6u ? 16u :
                              track == 7u ? 12u : 16u;
            current->level = track < 6u ? 86u : 76u;
            current->pan = track == 2u ? 92u :
                           track == 3u ? 168u :
                           track == 4u ? 188u : 128u;
            for (uint8_t step = 0u; step < KTECH_STEP_COUNT; ++step)
                current->steps[step] =
                    default_step(role_default_notes[current->role]);
        }
    }

    /* A small original starting rhythm; other scenes remain empty. */
    ktech_scene *first = &project->scenes[0];
    first->tracks[0].steps[0].enabled = 1u;
    first->tracks[0].steps[4].enabled = 1u;
    first->tracks[0].steps[8].enabled = 1u;
    first->tracks[0].steps[12].enabled = 1u;
    first->tracks[1].steps[4].enabled = 1u;
    first->tracks[1].steps[12].enabled = 1u;
    for (uint8_t step = 0u; step < 16u; step += 2u)
        first->tracks[3].steps[step].enabled = 1u;
    first->tracks[4].steps[7].enabled = 1u;
    first->tracks[4].steps[15].enabled = 1u;
    first->tracks[6].steps[0].enabled = 1u;
    first->tracks[6].steps[3].enabled = 1u;
    first->tracks[6].steps[6].enabled = 1u;
    first->tracks[6].steps[10].enabled = 1u;
    first->tracks[7].steps[2].enabled = 1u;
    first->tracks[7].steps[6].enabled = 1u;
    first->tracks[7].steps[10].enabled = 1u;
}

static bool step_is_default(const ktech_step *step, uint8_t note)
{
    const ktech_step expected = default_step(note);
    return memcmp(step, &expected, sizeof expected) == 0;
}

static bool continuation(uint8_t byte)
{
    return byte >= UINT8_C(0x80) && byte <= UINT8_C(0xbf);
}

static bool valid_name(const char name[KTECH_NAME_CAPACITY])
{
    size_t length = strnlen(name, KTECH_NAME_CAPACITY);
    if (length == KTECH_NAME_CAPACITY) return false;
    for (size_t tail = length + 1u; tail < KTECH_NAME_CAPACITY; ++tail)
        if (name[tail] != '\0') return false;
    const uint8_t *bytes = (const uint8_t *)name;
    size_t index = 0u;
    while (index < length) {
        uint8_t first = bytes[index++];
        if (first <= UINT8_C(0x7f)) {
            if (first < UINT8_C(0x20) || first == UINT8_C(0x7f))
                return false;
            continue;
        }
        if (first >= UINT8_C(0xc2) && first <= UINT8_C(0xdf)) {
            if (index >= length || !continuation(bytes[index++]))
                return false;
            continue;
        }
        if (first >= UINT8_C(0xe0) && first <= UINT8_C(0xef)) {
            if (index + 1u >= length) return false;
            uint8_t second = bytes[index++];
            uint8_t third = bytes[index++];
            if (!continuation(third) ||
                (first == UINT8_C(0xe0) && second < UINT8_C(0xa0)) ||
                (first == UINT8_C(0xed) && second > UINT8_C(0x9f)) ||
                (first != UINT8_C(0xe0) && first != UINT8_C(0xed) &&
                 !continuation(second)) ||
                ((first == UINT8_C(0xe0) || first == UINT8_C(0xed)) &&
                 !continuation(second)))
                return false;
            continue;
        }
        if (first >= UINT8_C(0xf0) && first <= UINT8_C(0xf4)) {
            if (index + 2u >= length) return false;
            uint8_t second = bytes[index++];
            uint8_t third = bytes[index++];
            uint8_t fourth = bytes[index++];
            if (!continuation(third) || !continuation(fourth) ||
                (first == UINT8_C(0xf0) && second < UINT8_C(0x90)) ||
                (first == UINT8_C(0xf4) && second > UINT8_C(0x8f)) ||
                (first != UINT8_C(0xf0) && first != UINT8_C(0xf4) &&
                 !continuation(second)) ||
                ((first == UINT8_C(0xf0) || first == UINT8_C(0xf4)) &&
                 !continuation(second)))
                return false;
            continue;
        }
        return false;
    }
    return true;
}

bool ktech_project_validate(const ktech_project *project, char *error,
                            size_t error_size)
{
    if (!project) {
        set_error(error, error_size, "project is null");
        return false;
    }
    if (project->tempo < 20u || project->tempo > 255u) {
        set_error(error, error_size, "tempo %u is outside 20..255",
                  project->tempo);
        return false;
    }
    if (project->swing < 50u || project->swing > 75u ||
        project->master_level > 100u || project->master_tone > 100u ||
        project->master_drive > 100u || project->master_delay > 100u ||
        project->active_scene >= KTECH_SCENE_COUNT ||
        project->launch_quantization >= KTECH_LAUNCH_COUNT ||
        project->reduced_motion > 1u) {
        set_error(error, error_size, "global control is outside its bound");
        return false;
    }
    if (!valid_name(project->name)) {
        set_error(error, error_size,
                  "project name is not canonical printable UTF-8");
        return false;
    }
    if (project->binding_count > KTECH_BINDING_CAPACITY) {
        set_error(error, error_size, "binding count exceeds capacity");
        return false;
    }
    for (uint16_t index = 0u; index < project->binding_count; ++index) {
        if (!ktech_binding_validate(&project->bindings[index])) {
            set_error(error, error_size, "binding %u is invalid", index);
            return false;
        }
        for (uint16_t previous = 0u; previous < index; ++previous) {
            if (binding_equal(&project->bindings[index],
                              &project->bindings[previous])) {
                set_error(error, error_size, "binding %u is duplicated",
                          index);
                return false;
            }
        }
    }
    for (uint16_t index = project->binding_count;
         index < KTECH_BINDING_CAPACITY; ++index) {
        static const ktech_binding zero;
        if (memcmp(&project->bindings[index], &zero, sizeof zero) != 0) {
            set_error(error, error_size, "binding tail is noncanonical");
            return false;
        }
    }

    for (uint8_t scene = 0u; scene < KTECH_SCENE_COUNT; ++scene) {
        for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
            const ktech_track *current =
                &project->scenes[scene].tracks[track];
            if (current->role >= KTECH_ROLE_COUNT ||
                !ktech_track_length_allowed(current->length) ||
                current->level > 100u || current->mute > 1u ||
                current->solo > 1u || current->reserved[0] != 0u ||
                current->reserved[1] != 0u) {
                set_error(error, error_size,
                          "scene %u track %u has invalid controls",
                          scene, track);
                return false;
            }
            for (uint8_t step = 0u; step < KTECH_STEP_COUNT; ++step) {
                const ktech_step *value = &current->steps[step];
                if (value->enabled > 1u || value->note > 127u ||
                    value->velocity > 127u || value->gate < 1u ||
                    value->gate > 100u || value->probability > 100u ||
                    value->ratchet < 1u || value->ratchet > 4u) {
                    set_error(error, error_size,
                              "scene %u track %u step %u is invalid",
                              scene, track, step);
                    return false;
                }
                if (step >= current->length &&
                    !step_is_default(value,
                                     role_default_notes[current->role])) {
                    set_error(error, error_size,
                              "scene %u track %u step %u is noncanonical",
                              scene, track, step);
                    return false;
                }
            }
        }
    }
    if (!ktech_workstation_validate(project, error, error_size)) return false;
    if (error && error_size) error[0] = '\0';
    return true;
}

static size_t encoded_size_for(const ktech_project *project)
{
    size_t name_length = strlen(project->name);
    return PROJECT_HEADER_SIZE +
           KTECH_SCENE_COUNT * KTECH_TRACK_COUNT *
               (TRACK_PREFIX_SIZE + KTECH_STEP_COUNT * STEP_SIZE) +
           (size_t)project->binding_count * BINDING_SIZE +
           1u + name_length + ktech_workstation_encoded_size(project);
}

static bool write_header(kilixstate_writer *writer,
                         const ktech_project *project, uint32_t total,
                         uint32_t crc)
{
    return kilixstate_write_bytes(writer, project_magic,
                                  sizeof project_magic) &&
           kilixstate_write_u16(writer, PROJECT_HEADER_SIZE) &&
           kilixstate_write_u16(writer, KTECH_PROJECT_SCHEMA) &&
           kilixstate_write_u32(writer, total) &&
           kilixstate_write_u32(writer, crc) &&
           kilixstate_write_u32(writer, project->seed) &&
           kilixstate_write_u16(writer, project->tempo) &&
           kilixstate_write_u8(writer, project->swing) &&
           kilixstate_write_u8(writer, project->master_level) &&
           kilixstate_write_u8(writer, project->master_tone) &&
           kilixstate_write_u8(writer, project->master_drive) &&
           kilixstate_write_u8(writer, project->master_delay) &&
           kilixstate_write_u8(writer, project->active_scene) &&
           kilixstate_write_u8(writer, project->launch_quantization) &&
           kilixstate_write_u8(writer, project->reduced_motion) &&
           kilixstate_write_u16(writer, project->binding_count) &&
           kilixstate_write_zeroes(writer, 4u);
}

static bool write_payload(kilixstate_writer *writer,
                          const ktech_project *project)
{
    for (uint8_t scene = 0u; scene < KTECH_SCENE_COUNT; ++scene) {
        for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
            const ktech_track *current =
                &project->scenes[scene].tracks[track];
            if (!kilixstate_write_u8(writer, current->role) ||
                !kilixstate_write_u8(writer, current->length) ||
                !kilixstate_write_u8(writer, current->level) ||
                !kilixstate_write_u8(writer, current->pan) ||
                !kilixstate_write_bool(writer, current->mute != 0u) ||
                !kilixstate_write_bool(writer, current->solo != 0u) ||
                !kilixstate_write_zeroes(writer, 2u))
                return false;
            for (uint8_t step = 0u; step < KTECH_STEP_COUNT; ++step) {
                const ktech_step *value = &current->steps[step];
                if (!kilixstate_write_bool(writer, value->enabled != 0u) ||
                    !kilixstate_write_u8(writer, value->note) ||
                    !kilixstate_write_u8(writer, value->velocity) ||
                    !kilixstate_write_u8(writer, value->gate) ||
                    !kilixstate_write_u8(writer, value->probability) ||
                    !kilixstate_write_u8(writer, value->ratchet))
                    return false;
            }
        }
    }
    for (uint16_t index = 0u; index < project->binding_count; ++index) {
        const ktech_binding *binding = &project->bindings[index];
        if (!kilixstate_write_u32(writer, binding->key) ||
            !kilixstate_write_u32(writer, binding->modifiers) ||
            !kilixstate_write_u16(writer, binding->action) ||
            !kilixstate_write_u8(writer, binding->kind) ||
            !kilixstate_write_u8(writer, binding->gamepad) ||
            !kilixstate_write_u8(writer, binding->control) ||
            !kilixstate_write_u8(writer, (uint8_t)binding->direction) ||
            !kilixstate_write_zeroes(writer, 2u))
            return false;
    }
    const size_t length = strlen(project->name);
    return kilixstate_write_u8(writer, (uint8_t)length) &&
           kilixstate_write_bytes(writer, project->name, length) &&
           ktech_workstation_write(writer, project);
}

ktech_project_result ktech_project_encode(const ktech_project *project,
                                          void *bytes, size_t capacity,
                                          size_t *encoded_size)
{
    char error[128];
    if (encoded_size) *encoded_size = 0u;
    if (!bytes || !ktech_project_validate(project, error, sizeof error))
        return KTECH_PROJECT_INVALID;
    const size_t needed = encoded_size_for(project);
    if (needed > KTECH_PROJECT_MAX_BYTES || needed > UINT32_MAX)
        return KTECH_PROJECT_TOO_LARGE;
    if (capacity < needed) return KTECH_PROJECT_TOO_LARGE;

    kilixstate_writer writer;
    kilixstate_writer_init(&writer, bytes, capacity);
    if (!write_header(&writer, project, (uint32_t)needed, 0u) ||
        !write_payload(&writer, project) ||
        kilixstate_writer_size(&writer) != needed)
        return KTECH_PROJECT_TOO_LARGE;

    uint8_t *raw = bytes;
    const uint32_t crc = kilixstate_crc32(raw + PROJECT_HEADER_SIZE,
                                          needed - PROJECT_HEADER_SIZE);
    raw[16] = (uint8_t)(crc & UINT32_C(0xff));
    raw[17] = (uint8_t)((crc >> 8) & UINT32_C(0xff));
    raw[18] = (uint8_t)((crc >> 16) & UINT32_C(0xff));
    raw[19] = (uint8_t)(crc >> 24);
    if (encoded_size) *encoded_size = needed;
    return KTECH_PROJECT_OK;
}

static bool read_header(kilixstate_reader *reader, ktech_project *project,
                        uint32_t *stored_crc, uint16_t *schema_out)
{
    uint8_t magic[8];
    uint16_t header_size, schema;
    uint32_t total;
    uint8_t reserved[4];
    bool reduced_motion;

    if (!kilixstate_read_bytes(reader, magic, sizeof magic) ||
        !kilixstate_read_u16(reader, &header_size) ||
        !kilixstate_read_u16(reader, &schema) ||
        !kilixstate_read_u32(reader, &total) ||
        !kilixstate_read_u32(reader, stored_crc) ||
        !kilixstate_read_u32(reader, &project->seed) ||
        !kilixstate_read_u16(reader, &project->tempo) ||
        !kilixstate_read_u8(reader, &project->swing) ||
        !kilixstate_read_u8(reader, &project->master_level) ||
        !kilixstate_read_u8(reader, &project->master_tone) ||
        !kilixstate_read_u8(reader, &project->master_drive) ||
        !kilixstate_read_u8(reader, &project->master_delay) ||
        !kilixstate_read_u8(reader, &project->active_scene) ||
        !kilixstate_read_u8(reader, &project->launch_quantization) ||
        !kilixstate_read_bool(reader, &reduced_motion) ||
        !kilixstate_read_u16(reader, &project->binding_count) ||
        !kilixstate_read_bytes(reader, reserved, sizeof reserved))
        return false;
    project->reduced_motion = reduced_motion ? 1u : 0u;
    if (memcmp(magic, project_magic, sizeof magic) != 0 ||
        header_size != PROJECT_HEADER_SIZE || total != reader->size)
        return false;
    if (schema != 1u && schema != 2u && schema != KTECH_PROJECT_SCHEMA) {
        reader->result = KILIXSTATE_CODEC_UNKNOWN_VERSION;
        return false;
    }
    *schema_out = schema;
    for (size_t index = 0u; index < sizeof reserved; ++index)
        if (reserved[index] != 0u) {
            reader->result = KILIXSTATE_CODEC_NONCANONICAL;
            return false;
        }
    return true;
}

static bool read_payload(kilixstate_reader *reader, ktech_project *project,
                         uint16_t schema)
{
    for (uint8_t scene = 0u; scene < KTECH_SCENE_COUNT; ++scene) {
        for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
            ktech_track *current = &project->scenes[scene].tracks[track];
            bool mute, solo;
            uint8_t reserved[2];
            if (!kilixstate_read_u8(reader, &current->role) ||
                !kilixstate_read_u8(reader, &current->length) ||
                !kilixstate_read_u8(reader, &current->level) ||
                !kilixstate_read_u8(reader, &current->pan) ||
                !kilixstate_read_bool(reader, &mute) ||
                !kilixstate_read_bool(reader, &solo) ||
                !kilixstate_read_bytes(reader, reserved, sizeof reserved))
                return false;
            current->mute = mute ? 1u : 0u;
            current->solo = solo ? 1u : 0u;
            if ((schema == 1u && current->role >= KTECH_NATIVE_ROLE_COUNT) ||
                reserved[0] != 0u || reserved[1] != 0u) {
                reader->result = KILIXSTATE_CODEC_NONCANONICAL;
                return false;
            }
            for (uint8_t step = 0u; step < KTECH_STEP_COUNT; ++step) {
                ktech_step *value = &current->steps[step];
                bool enabled;
                if (!kilixstate_read_bool(reader, &enabled) ||
                    !kilixstate_read_u8(reader, &value->note) ||
                    !kilixstate_read_u8(reader, &value->velocity) ||
                    !kilixstate_read_u8(reader, &value->gate) ||
                    !kilixstate_read_u8(reader, &value->probability) ||
                    !kilixstate_read_u8(reader, &value->ratchet))
                    return false;
                value->enabled = enabled ? 1u : 0u;
            }
        }
    }
    if (project->binding_count > KTECH_BINDING_CAPACITY) {
        reader->result = KILIXSTATE_CODEC_NONCANONICAL;
        return false;
    }
    for (uint16_t index = 0u; index < project->binding_count; ++index) {
        ktech_binding *binding = &project->bindings[index];
        uint8_t direction, reserved[2];
        if (!kilixstate_read_u32(reader, &binding->key) ||
            !kilixstate_read_u32(reader, &binding->modifiers) ||
            !kilixstate_read_u16(reader, &binding->action) ||
            !kilixstate_read_u8(reader, &binding->kind) ||
            !kilixstate_read_u8(reader, &binding->gamepad) ||
            !kilixstate_read_u8(reader, &binding->control) ||
            !kilixstate_read_u8(reader, &direction) ||
            !kilixstate_read_bytes(reader, reserved, sizeof reserved))
            return false;
        binding->direction = (int8_t)direction;
        if (reserved[0] != 0u || reserved[1] != 0u) {
            reader->result = KILIXSTATE_CODEC_NONCANONICAL;
            return false;
        }
    }
    uint8_t name_length;
    if (!kilixstate_read_u8(reader, &name_length) ||
        name_length >= KTECH_NAME_CAPACITY ||
        !kilixstate_read_bytes(reader, project->name, name_length))
        return false;
    project->name[name_length] = '\0';
    if (schema < 3u) {
        ktech_workstation_defaults(project);
        return kilixstate_reader_require_finished(reader);
    }
    return ktech_workstation_read(reader, project) &&
           kilixstate_reader_require_finished(reader);
}

ktech_project_result ktech_project_decode(ktech_project *project,
                                          const void *bytes, size_t size)
{
    if (!project || !bytes) return KTECH_PROJECT_INVALID;
    if (size > KTECH_PROJECT_MAX_BYTES) return KTECH_PROJECT_TOO_LARGE;
    if (size < PROJECT_HEADER_SIZE) return KTECH_PROJECT_TRUNCATED;

    ktech_project temporary;
    memset(&temporary, 0, sizeof temporary);
    kilixstate_reader reader;
    kilixstate_reader_init(&reader, bytes, size);
    uint32_t stored_crc;
    uint16_t schema;
    if (!read_header(&reader, &temporary, &stored_crc, &schema)) {
        return kilixstate_reader_result(&reader) ==
                       KILIXSTATE_CODEC_UNKNOWN_VERSION
                   ? KTECH_PROJECT_UNKNOWN_VERSION
                   : KTECH_PROJECT_NONCANONICAL;
    }
    const uint8_t *raw = bytes;
    if (kilixstate_crc32(raw + PROJECT_HEADER_SIZE,
                         size - PROJECT_HEADER_SIZE) != stored_crc)
        return KTECH_PROJECT_CORRUPT;
    if (!read_payload(&reader, &temporary, schema)) {
        return kilixstate_reader_result(&reader) ==
                       KILIXSTATE_CODEC_TRUNCATED
                   ? KTECH_PROJECT_TRUNCATED
                   : KTECH_PROJECT_NONCANONICAL;
    }
    if (!ktech_project_validate(&temporary, NULL, 0u))
        return KTECH_PROJECT_INVALID;
    *project = temporary;
    return KTECH_PROJECT_OK;
}

bool ktech_project_pattern_create(ktech_project *project, uint8_t scene,
                                  uint8_t track, const char *name,
                                  uint8_t *pattern_out)
{
    if (!project || !name || name[0] == '\0' ||
        scene >= KTECH_SCENE_COUNT || track >= KTECH_TRACK_COUNT ||
        project->pattern_count >= KTECH_PATTERN_CAPACITY)
        return false;
    size_t length = strnlen(name, KTECH_PATTERN_NAME_CAPACITY);
    if (length == KTECH_PATTERN_NAME_CAPACITY) return false;
    uint16_t index = project->pattern_count;
    ktech_pattern *pattern = &project->patterns[index];
    memset(pattern, 0, sizeof *pattern);
    (void)snprintf(pattern->name, sizeof pattern->name, "%s", name);
    const ktech_track *source = &project->scenes[scene].tracks[track];
    pattern->role = source->role;
    pattern->length = source->length;
    memcpy(pattern->steps, source->steps, sizeof pattern->steps);
    ++project->pattern_count;
    project->scene_patterns[scene][track] = (uint8_t)index;
    if (!ktech_project_validate(project, NULL, 0u)) {
        project->scene_patterns[scene][track] = KTECH_PATTERN_NONE;
        --project->pattern_count;
        memset(pattern, 0, sizeof *pattern);
        return false;
    }
    if (pattern_out) *pattern_out = (uint8_t)index;
    return true;
}

bool ktech_project_pattern_assign(ktech_project *project, uint8_t scene,
                                  uint8_t track, uint8_t pattern)
{
    if (!project || scene >= KTECH_SCENE_COUNT ||
        track >= KTECH_TRACK_COUNT ||
        (pattern != KTECH_PATTERN_NONE && pattern >= project->pattern_count))
        return false;
    project->scene_patterns[scene][track] = pattern;
    return true;
}

bool ktech_project_pattern_duplicate(ktech_project *project, uint8_t pattern,
                                     bool linked, uint8_t *duplicate)
{
    if (!project || pattern >= project->pattern_count) return false;
    if (linked) {
        if (duplicate) *duplicate = pattern;
        return true;
    }
    if (project->pattern_count >= KTECH_PATTERN_CAPACITY) return false;
    uint16_t index = project->pattern_count;
    project->patterns[index] = project->patterns[pattern];
    char base[KTECH_PATTERN_NAME_CAPACITY];
    (void)snprintf(base, sizeof base, "%s", project->patterns[pattern].name);
    int count = snprintf(project->patterns[index].name,
                         sizeof project->patterns[index].name,
                         "%.26s copy", base);
    if (count < 0 || (size_t)count >= sizeof project->patterns[index].name) {
        memset(&project->patterns[index], 0, sizeof project->patterns[index]);
        return false;
    }
    ++project->pattern_count;
    if (duplicate) *duplicate = (uint8_t)index;
    return true;
}

const ktech_track *ktech_project_resolve_track(const ktech_project *project,
                                               uint8_t scene, uint8_t track,
                                               ktech_track *scratch)
{
    if (!project || scene >= KTECH_SCENE_COUNT || track >= KTECH_TRACK_COUNT)
        return NULL;
    const ktech_track *source = &project->scenes[scene].tracks[track];
    uint8_t reference = project->scene_patterns[scene][track];
    if (reference == KTECH_PATTERN_NONE) return source;
    if (reference >= project->pattern_count || !scratch) return NULL;
    *scratch = *source;
    const ktech_pattern *pattern = &project->patterns[reference];
    scratch->role = pattern->role;
    scratch->length = pattern->length;
    memcpy(scratch->steps, pattern->steps, sizeof scratch->steps);
    return scratch;
}

uint16_t ktech_project_arrangement_bars(const ktech_project *project)
{
    if (!project) return 0u;
    uint16_t bars = 0u;
    for (uint16_t index = 0u; index < project->arrangement_count; ++index)
        bars = (uint16_t)(bars + project->arrangement[index].bars);
    return bars;
}

bool ktech_project_arrangement_add(ktech_project *project, uint8_t scene,
                                   uint8_t bars,
                                   const uint8_t patterns[KTECH_TRACK_COUNT])
{
    if (!project || scene >= KTECH_SCENE_COUNT || bars == 0u ||
        project->arrangement_count >= KTECH_ARRANGEMENT_CAPACITY ||
        (unsigned)ktech_project_arrangement_bars(project) + bars >
            KTECH_SONG_BAR_CAPACITY)
        return false;
    ktech_arrangement_entry *entry =
        &project->arrangement[project->arrangement_count];
    memset(entry, 0, sizeof *entry);
    entry->scene = scene;
    entry->bars = bars;
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
        uint8_t reference = patterns ? patterns[track] :
                                       project->scene_patterns[scene][track];
        if (reference != KTECH_PATTERN_NONE &&
            reference >= project->pattern_count) {
            memset(entry, 0, sizeof *entry);
            return false;
        }
        entry->patterns[track] = reference;
    }
    ++project->arrangement_count;
    if (project->arrangement_selected == UINT16_MAX)
        project->arrangement_selected = 0u;
    return true;
}

static ktech_project_result from_store_result(kilixstate_result result)
{
    switch (result) {
    case KILIXSTATE_OK: return KTECH_PROJECT_OK;
    case KILIXSTATE_TOO_LARGE:
    case KILIXSTATE_BUFFER_TOO_SMALL: return KTECH_PROJECT_TOO_LARGE;
    case KILIXSTATE_CORRUPT: return KTECH_PROJECT_CORRUPT;
    case KILIXSTATE_SECURITY: return KTECH_PROJECT_SECURITY_ERROR;
    case KILIXSTATE_NOT_FOUND: return KTECH_PROJECT_NOT_FOUND;
    default: return KTECH_PROJECT_IO_ERROR;
    }
}

static ktech_project_result save_store(const ktech_project *project,
                                       kilixstate_options *options)
{
    uint8_t *bytes = malloc(KTECH_PROJECT_ENCODED_MAX);
    if (!bytes) return KTECH_PROJECT_IO_ERROR;
    size_t size;
    ktech_project_result encoded = ktech_project_encode(
        project, bytes, KTECH_PROJECT_ENCODED_MAX, &size);
    if (encoded != KTECH_PROJECT_OK) {
        free(bytes);
        return encoded;
    }
    kilixstate_store store;
    kilixstate_result result = kilixstate_store_init(&store, options);
    if (result != KILIXSTATE_OK) {
        free(bytes);
        return from_store_result(result);
    }
    result = kilixstate_save(&store, bytes, size);
    kilixstate_store_close(&store);
    free(bytes);
    return from_store_result(result);
}

static ktech_project_result save_store_no_replace(
    const ktech_project *project, const char *absolute_path)
{
    uint8_t *bytes = malloc(KTECH_PROJECT_ENCODED_MAX);
    if (!bytes) return KTECH_PROJECT_IO_ERROR;
    size_t size;
    ktech_project_result encoded = ktech_project_encode(
        project, bytes, KTECH_PROJECT_ENCODED_MAX, &size);
    if (encoded != KTECH_PROJECT_OK) {
        free(bytes);
        return encoded;
    }

    kilixstate_options final_options;
    kilixstate_options_init(&final_options);
    final_options.absolute_path = absolute_path;
    final_options.max_payload = KTECH_PROJECT_MAX_BYTES;
    final_options.format = KILIXSTATE_FORMAT_RAW;
    kilixstate_store final_store;
    kilixstate_result stored = kilixstate_store_init(&final_store,
                                                      &final_options);
    if (stored != KILIXSTATE_OK) {
        free(bytes);
        return from_store_result(stored);
    }

    static atomic_uint temporary_counter;
    unsigned counter = atomic_fetch_add_explicit(&temporary_counter, 1u,
                                                  memory_order_relaxed) + 1u;
    char staging_name[KILIXSTATE_FILENAME_CAPACITY];
    int name_length = snprintf(staging_name, sizeof staging_name,
        ".%s.ktech-stage-%ld-%u", final_store.filename, (long)getpid(),
        counter);
    char staging_path[KILIXSTATE_PATH_CAPACITY];
    int path_length = snprintf(staging_path, sizeof staging_path, "%s/%s",
                               final_store.directory_path, staging_name);
    if (name_length < 0 || (size_t)name_length >= sizeof staging_name ||
        path_length < 0 || (size_t)path_length >= sizeof staging_path) {
        kilixstate_store_close(&final_store);
        free(bytes);
        return KTECH_PROJECT_IO_ERROR;
    }

    kilixstate_options staging_options;
    kilixstate_options_init(&staging_options);
    staging_options.absolute_path = staging_path;
    staging_options.max_payload = KTECH_PROJECT_MAX_BYTES;
    staging_options.format = KILIXSTATE_FORMAT_RAW;
    kilixstate_store staging_store;
    stored = kilixstate_store_init(&staging_store, &staging_options);
    if (stored == KILIXSTATE_OK) {
        stored = kilixstate_save(&staging_store, bytes, size);
        kilixstate_store_close(&staging_store);
    }
    free(bytes);
    if (stored != KILIXSTATE_OK) {
        kilixstate_store_close(&final_store);
        return from_store_result(stored);
    }

    ktech_project_result result = KTECH_PROJECT_OK;
#ifdef SYS_renameat2
    if (syscall(SYS_renameat2, final_store.directory_fd, staging_name,
                final_store.directory_fd, final_store.filename,
                RENAME_NOREPLACE) != 0) {
        result = errno == EEXIST ? KTECH_PROJECT_EXISTS :
                                  KTECH_PROJECT_IO_ERROR;
    }
#else
    if (linkat(final_store.directory_fd, staging_name,
               final_store.directory_fd, final_store.filename, 0) != 0) {
        result = errno == EEXIST ? KTECH_PROJECT_EXISTS :
                                  KTECH_PROJECT_IO_ERROR;
    }
    if (result == KTECH_PROJECT_OK &&
        unlinkat(final_store.directory_fd, staging_name, 0) != 0)
        result = KTECH_PROJECT_IO_ERROR;
#endif
    if (result != KTECH_PROJECT_OK)
        (void)unlinkat(final_store.directory_fd, staging_name, 0);
    if (fsync(final_store.directory_fd) != 0 &&
        result == KTECH_PROJECT_OK)
        result = KTECH_PROJECT_IO_ERROR;
    kilixstate_store_close(&final_store);
    return result;
}

static ktech_project_result load_store(ktech_project *project,
                                       kilixstate_options *options)
{
    uint8_t *bytes = malloc(KTECH_PROJECT_ENCODED_MAX);
    if (!bytes) return KTECH_PROJECT_IO_ERROR;
    size_t size = 0u;
    kilixstate_store store;
    kilixstate_result result = kilixstate_store_init(&store, options);
    if (result != KILIXSTATE_OK) {
        free(bytes);
        return from_store_result(result);
    }
    result = kilixstate_load(&store, bytes, KTECH_PROJECT_ENCODED_MAX, &size);
    kilixstate_store_close(&store);
    if (result != KILIXSTATE_OK) {
        free(bytes);
        return from_store_result(result);
    }
    ktech_project_result decoded = ktech_project_decode(project, bytes, size);
    free(bytes);
    return decoded;
}

ktech_project_result ktech_project_save(const ktech_project *project,
                                        const char *absolute_path,
                                        bool replace_existing)
{
    if (!absolute_path || absolute_path[0] != '/')
        return KTECH_PROJECT_SECURITY_ERROR;
    struct stat status;
    if (lstat(absolute_path, &status) == 0) {
        if (!replace_existing) return KTECH_PROJECT_EXISTS;
        if (!S_ISREG(status.st_mode)) return KTECH_PROJECT_SECURITY_ERROR;
    } else if (errno != ENOENT) {
        return KTECH_PROJECT_IO_ERROR;
    }
    if (!replace_existing)
        return save_store_no_replace(project, absolute_path);
    kilixstate_options options;
    kilixstate_options_init(&options);
    options.absolute_path = absolute_path;
    options.max_payload = KTECH_PROJECT_MAX_BYTES;
    options.format = KILIXSTATE_FORMAT_RAW;
    return save_store(project, &options);
}

ktech_project_result ktech_project_load(ktech_project *project,
                                        const char *absolute_path)
{
    if (!absolute_path || absolute_path[0] != '/')
        return KTECH_PROJECT_SECURITY_ERROR;
    kilixstate_options options;
    kilixstate_options_init(&options);
    options.absolute_path = absolute_path;
    options.max_payload = KTECH_PROJECT_MAX_BYTES;
    options.format = KILIXSTATE_FORMAT_RAW;
    return load_store(project, &options);
}

static void autosave_options(kilixstate_options *options,
                             const char *base_directory)
{
    kilixstate_options_init(options);
    options->app_id = "kilix-techno";
    options->filename = "autosave.state";
    options->base_directory = base_directory;
    options->max_payload = KTECH_PROJECT_MAX_BYTES;
    options->format = KILIXSTATE_FORMAT_CRC32;
}

ktech_project_result ktech_project_save_autosave(
    const ktech_project *project, const char *base_directory)
{
    kilixstate_options options;
    autosave_options(&options, base_directory);
    return save_store(project, &options);
}

ktech_project_result ktech_project_load_autosave(
    ktech_project *project, const char *base_directory)
{
    kilixstate_options options;
    autosave_options(&options, base_directory);
    return load_store(project, &options);
}

ktech_project_result ktech_project_remove_autosave(
    const char *base_directory)
{
    kilixstate_options options;
    autosave_options(&options, base_directory);
    kilixstate_store store;
    kilixstate_result result = kilixstate_store_init(&store, &options);
    if (result != KILIXSTATE_OK) return from_store_result(result);
    result = kilixstate_remove(&store);
    kilixstate_store_close(&store);
    return result == KILIXSTATE_NOT_FOUND ? KTECH_PROJECT_OK :
                                           from_store_result(result);
}

ktech_project_result ktech_project_autosave_path(
    const char *base_directory, char *path, size_t path_size)
{
    kilixstate_options options;
    autosave_options(&options, base_directory);
    kilixstate_store store;
    kilixstate_result result = kilixstate_store_init(&store, &options);
    if (result != KILIXSTATE_OK) return from_store_result(result);
    result = kilixstate_store_path(&store, path, path_size);
    kilixstate_store_close(&store);
    return from_store_result(result);
}

ktech_project_result ktech_project_quarantine_autosave(
    const char *base_directory, char *path, size_t path_size)
{
    static const char diagnostic[] = "autosave.corrupt.state";
    kilixstate_options options;
    autosave_options(&options, base_directory);
    kilixstate_store store;
    kilixstate_result stored = kilixstate_store_init(&store, &options);
    if (stored != KILIXSTATE_OK) return from_store_result(stored);
    struct stat status;
    ktech_project_result result = KTECH_PROJECT_IO_ERROR;
    if (fstatat(store.directory_fd, diagnostic, &status,
                AT_SYMLINK_NOFOLLOW) == 0) {
        result = KTECH_PROJECT_EXISTS;
    } else if (errno != ENOENT) {
        result = KTECH_PROJECT_IO_ERROR;
    } else if (renameat(store.directory_fd, store.filename,
                        store.directory_fd, diagnostic) != 0) {
        result = errno == ENOENT ? KTECH_PROJECT_NOT_FOUND :
                                  KTECH_PROJECT_IO_ERROR;
    } else if (fsync(store.directory_fd) != 0) {
        result = KTECH_PROJECT_IO_ERROR;
    } else {
        result = KTECH_PROJECT_OK;
    }
    if (path && path_size > 0u) {
        int count = snprintf(path, path_size, "%s/%s", store.directory_path,
                             result == KTECH_PROJECT_OK ? diagnostic :
                                                         store.filename);
        if (count < 0 || (size_t)count >= path_size)
            result = KTECH_PROJECT_IO_ERROR;
    }
    kilixstate_store_close(&store);
    return result;
}

const char *ktech_project_result_name(ktech_project_result result)
{
    static const char *const names[] = {
        "ok", "not-found", "invalid", "too-large", "truncated", "corrupt",
        "unknown-version", "noncanonical", "io-error", "security-error",
        "exists"
    };
    return (unsigned)result < sizeof names / sizeof names[0]
               ? names[result] : "unknown";
}
