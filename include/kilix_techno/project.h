#ifndef KILIX_TECHNO_PROJECT_H
#define KILIX_TECHNO_PROJECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KTECH_VERSION_MAJOR 0u
#define KTECH_VERSION_MINOR 2u
#define KTECH_VERSION_PATCH 1u
#define KTECH_PROJECT_SCHEMA 3u

#define KTECH_TRACK_COUNT 8u
#define KTECH_SCENE_COUNT 16u
#define KTECH_STEP_COUNT 64u
#define KTECH_NAME_CAPACITY 64u
#define KTECH_PROJECT_MAX_BYTES (1024u * 1024u)
#define KTECH_PROJECT_ENCODED_MAX KTECH_PROJECT_MAX_BYTES
#define KTECH_BINDING_CAPACITY 96u
#define KTECH_ANY_GAMEPAD UINT8_MAX

#define KTECH_PATTERN_CAPACITY 128u
#define KTECH_PATTERN_NAME_CAPACITY 32u
#define KTECH_ARRANGEMENT_CAPACITY 1024u
#define KTECH_SONG_BAR_CAPACITY 256u
#define KTECH_PATTERN_NONE UINT8_MAX
#define KTECH_AUTOMATION_LANE_CAPACITY 32u
#define KTECH_AUTOMATION_POINT_CAPACITY 256u
#define KTECH_AUTOMATION_POINT_TOTAL_CAPACITY 8192u
#define KTECH_MACRO_CAPACITY 8u
#define KTECH_MACRO_TARGET_CAPACITY 8u
#define KTECH_BUS_CAPACITY 4u
#define KTECH_EFFECT_CAPACITY 16u
#define KTECH_EFFECT_CHAIN_CAPACITY 4u
#define KTECH_EXPORT_PRESET_CAPACITY 8u
#define KTECH_RESOURCE_CAPACITY 64u
#define KTECH_RESOURCE_PATH_CAPACITY 192u
#define KTECH_LICENSE_CAPACITY 32u

typedef enum ktech_action {
    KTECH_ACTION_LEFT = 0,
    KTECH_ACTION_RIGHT,
    KTECH_ACTION_UP,
    KTECH_ACTION_DOWN,
    KTECH_ACTION_NEXT_TRACK,
    KTECH_ACTION_PREVIOUS_TRACK,
    KTECH_ACTION_ACTIVATE,
    KTECH_ACTION_DECREASE,
    KTECH_ACTION_INCREASE,
    KTECH_ACTION_PLAY_PAUSE,
    KTECH_ACTION_STOP,
    KTECH_ACTION_RESTART,
    KTECH_ACTION_TAP_TEMPO,
    KTECH_ACTION_MUTE,
    KTECH_ACTION_SOLO,
    KTECH_ACTION_UNDO,
    KTECH_ACTION_REDO,
    KTECH_ACTION_SAVE,
    KTECH_ACTION_EXPORT,
    KTECH_ACTION_HELP,
    KTECH_ACTION_QUIT,
    KTECH_ACTION_PREVIOUS_SCENE,
    KTECH_ACTION_NEXT_SCENE,
    KTECH_ACTION_LAUNCH_SCENE,
    KTECH_ACTION_COUNT
} ktech_action;

typedef enum ktech_binding_kind {
    KTECH_BINDING_KEY = 1,
    KTECH_BINDING_GAMEPAD_BUTTON = 2,
    KTECH_BINDING_GAMEPAD_AXIS = 3
} ktech_binding_kind;

typedef struct ktech_binding {
    uint32_t key;
    uint32_t modifiers;
    uint16_t action;
    uint8_t kind;
    uint8_t gamepad;
    uint8_t control;
    int8_t direction;
    uint8_t reserved[2];
} ktech_binding;

typedef enum ktech_role {
    KTECH_ROLE_KICK = 0,
    KTECH_ROLE_SNARE,
    KTECH_ROLE_CLAP,
    KTECH_ROLE_CLOSED_HAT,
    KTECH_ROLE_OPEN_HAT,
    KTECH_ROLE_PERCUSSION,
    KTECH_ROLE_BASS,
    KTECH_ROLE_CHORD,
    KTECH_ROLE_TEXTURE,

    KTECH_ROLE_TIDAL_KICK,
    KTECH_ROLE_TIDAL_SNARE,
    KTECH_ROLE_TIDAL_CLAP,
    KTECH_ROLE_TIDAL_CLOSED_HAT,
    KTECH_ROLE_TIDAL_OPEN_HAT,
    KTECH_ROLE_TIDAL_COWBELL,

    KTECH_ROLE_STARGATE_KICK,
    KTECH_ROLE_STARGATE_SNARE,
    KTECH_ROLE_STARGATE_CLOSED_HAT,
    KTECH_ROLE_STARGATE_OPEN_HAT,
    KTECH_ROLE_STARGATE_FM_PERC,
    KTECH_ROLE_STARGATE_SUB,

    KTECH_ROLE_FORGE_GROWL,
    KTECH_ROLE_FORGE_HOUSE_STAB,

    KTECH_ROLE_MECH_PLUCK_BASS,
    KTECH_ROLE_MECH_FUZZ_BASS,
    KTECH_ROLE_MECH_WOBBLE,
    KTECH_ROLE_MECH_TIME_PAD,
    KTECH_ROLE_MECH_INDUSTRIAL_HIT,
    KTECH_ROLE_MECH_GLITCH_VOX,

    KTECH_ROLE_KARORYFER_A2,
    KTECH_ROLE_KARORYFER_C4,
    KTECH_ROLE_KARORYFER_F3,
    KTECH_ROLE_KARORYFER_G2,

    KTECH_ROLE_VCSL_CLAVISYNTH,
    KTECH_ROLE_VCSL_FM_PIANO,
    KTECH_ROLE_VCSL_BRAKE_DRUM,
    KTECH_ROLE_VCSL_OCEAN_DRUM,

    KTECH_ROLE_ONGENET_KICK,
    KTECH_ROLE_ONGENET_SNARE,
    KTECH_ROLE_ONGENET_CLAP,
    KTECH_ROLE_ONGENET_CLOSED_HAT,
    KTECH_ROLE_ONGENET_OPEN_HAT,
    KTECH_ROLE_ONGENET_RIDE,

    KTECH_ROLE_VCSL_ACOUSTIC_KICK,
    KTECH_ROLE_VCSL_ACOUSTIC_SNARE,
    KTECH_ROLE_VCSL_STRUMSTICK,
    KTECH_ROLE_VCSL_KAWAI_PIANO,

    KTECH_ROLE_VSCO_VIOLIN_PIZZ,
    KTECH_ROLE_VSCO_CELLO_PIZZ,
    KTECH_ROLE_VSCO_CLARINET_STAC,
    KTECH_ROLE_VSCO_HORN_STAC,

    KTECH_ROLE_SF2_CHAOS_PIANO,
    KTECH_ROLE_SF2_JNS_SYNTH_BASS,
    KTECH_ROLE_SF2_MASTERPIECE_PAD,
    KTECH_ROLE_SF2_UNISON_LEAD,
    KTECH_ROLE_COUNT
} ktech_role;

#define KTECH_NATIVE_ROLE_COUNT 9u
#define KTECH_SAMPLE_BANK_COUNT 13u

typedef enum ktech_launch_quantization {
    KTECH_LAUNCH_STEP = 0,
    KTECH_LAUNCH_BEAT,
    KTECH_LAUNCH_BAR,
    KTECH_LAUNCH_COUNT
} ktech_launch_quantization;

typedef struct ktech_step {
    uint8_t enabled;
    uint8_t note;
    uint8_t velocity;
    uint8_t gate;
    uint8_t probability;
    uint8_t ratchet;
} ktech_step;

typedef struct ktech_track {
    uint8_t role;
    uint8_t length;
    uint8_t level;
    uint8_t pan;
    uint8_t mute;
    uint8_t solo;
    uint8_t reserved[2];
    ktech_step steps[KTECH_STEP_COUNT];
} ktech_track;

typedef struct ktech_scene {
    ktech_track tracks[KTECH_TRACK_COUNT];
} ktech_scene;

typedef struct ktech_pattern {
    char name[KTECH_PATTERN_NAME_CAPACITY];
    uint8_t role;
    uint8_t length;
    uint8_t reserved[2];
    ktech_step steps[KTECH_STEP_COUNT];
} ktech_pattern;

typedef struct ktech_arrangement_entry {
    uint8_t scene;
    uint8_t bars;
    uint8_t patterns[KTECH_TRACK_COUNT];
    uint8_t reserved[2];
} ktech_arrangement_entry;

typedef enum ktech_automation_target {
    KTECH_AUTOMATION_TRACK_LEVEL = 0,
    KTECH_AUTOMATION_TRACK_PAN,
    KTECH_AUTOMATION_MASTER_LEVEL,
    KTECH_AUTOMATION_MASTER_TONE,
    KTECH_AUTOMATION_MASTER_DRIVE,
    KTECH_AUTOMATION_MASTER_DELAY,
    KTECH_AUTOMATION_BUS_LEVEL,
    KTECH_AUTOMATION_EFFECT_WET,
    KTECH_AUTOMATION_MACRO,
    KTECH_AUTOMATION_TARGET_COUNT
} ktech_automation_target;

typedef struct ktech_automation_point {
    uint16_t row;
    int16_t value;
} ktech_automation_point;

typedef struct ktech_automation_lane {
    uint8_t target;
    uint8_t target_index;
    uint16_t point_count;
    uint8_t reserved[4];
    ktech_automation_point points[KTECH_AUTOMATION_POINT_CAPACITY];
} ktech_automation_lane;

typedef struct ktech_macro_target {
    uint8_t target;
    uint8_t target_index;
    int8_t depth;
    uint8_t reserved;
} ktech_macro_target;

typedef struct ktech_macro {
    char name[24];
    uint8_t value;
    uint8_t target_count;
    uint8_t reserved[2];
    ktech_macro_target targets[KTECH_MACRO_TARGET_CAPACITY];
} ktech_macro;

typedef enum ktech_effect_kind {
    KTECH_EFFECT_FILTER = 0,
    KTECH_EFFECT_SATURATOR,
    KTECH_EFFECT_TAPE_DELAY,
    KTECH_EFFECT_ROOM,
    KTECH_EFFECT_COMPRESSOR,
    KTECH_EFFECT_BITCRUSH,
    KTECH_EFFECT_KIND_COUNT
} ktech_effect_kind;

typedef struct ktech_effect {
    uint8_t kind;
    uint8_t enabled;
    uint8_t wet;
    uint8_t parameter_count;
    uint8_t parameters[4];
} ktech_effect;

typedef struct ktech_bus {
    char name[16];
    uint8_t level;
    uint8_t pan;
    uint8_t mute;
    uint8_t solo;
    uint8_t effect_count;
    uint8_t reserved[3];
    ktech_effect effects[KTECH_EFFECT_CHAIN_CAPACITY];
} ktech_bus;

typedef enum ktech_export_format {
    KTECH_EXPORT_WAV = 0,
    KTECH_EXPORT_STEMS,
    KTECH_EXPORT_MIDI,
    KTECH_EXPORT_FORMAT_COUNT
} ktech_export_format;

typedef struct ktech_export_preset {
    char name[24];
    uint8_t format;
    uint8_t bit_depth;
    uint8_t normalize;
    uint8_t include_master;
    uint16_t bars;
    uint16_t tail_ms;
} ktech_export_preset;

typedef enum ktech_resource_format {
    KTECH_RESOURCE_WAV = 0,
    KTECH_RESOURCE_OGG,
    KTECH_RESOURCE_SFZ,
    KTECH_RESOURCE_SF2,
    KTECH_RESOURCE_FORMAT_COUNT
} ktech_resource_format;

typedef struct ktech_resource {
    char path[KTECH_RESOURCE_PATH_CAPACITY];
    char license[KTECH_LICENSE_CAPACITY];
    uint8_t sha256[32];
    uint32_t bytes;
    uint8_t format;
    uint8_t role;
    uint8_t reserved[2];
} ktech_resource;

typedef struct ktech_project {
    uint32_t seed;
    uint16_t tempo;
    uint8_t swing;
    uint8_t master_level;
    uint8_t master_tone;
    uint8_t master_drive;
    uint8_t master_delay;
    uint8_t active_scene;
    uint8_t launch_quantization;
    uint8_t reduced_motion;
    uint16_t binding_count;
    char name[KTECH_NAME_CAPACITY];
    ktech_binding bindings[KTECH_BINDING_CAPACITY];
    ktech_scene scenes[KTECH_SCENE_COUNT];
    uint16_t pattern_count;
    uint16_t arrangement_count;
    uint16_t arrangement_selected;
    uint16_t arrangement_queued;
    uint16_t arrangement_audible;
    uint8_t automation_lane_count;
    uint8_t macro_count;
    uint8_t bus_count;
    uint8_t export_preset_count;
    uint8_t resource_count;
    uint8_t performance_fill;
    uint8_t performance_variation;
    uint8_t reserved_workstation;
    uint8_t scene_patterns[KTECH_SCENE_COUNT][KTECH_TRACK_COUNT];
    uint8_t track_bus[KTECH_TRACK_COUNT];
    uint8_t mute_groups[KTECH_MACRO_CAPACITY];
    ktech_pattern patterns[KTECH_PATTERN_CAPACITY];
    ktech_arrangement_entry arrangement[KTECH_ARRANGEMENT_CAPACITY];
    ktech_automation_lane automation[KTECH_AUTOMATION_LANE_CAPACITY];
    ktech_macro macros[KTECH_MACRO_CAPACITY];
    ktech_bus buses[KTECH_BUS_CAPACITY];
    ktech_export_preset export_presets[KTECH_EXPORT_PRESET_CAPACITY];
    ktech_resource resources[KTECH_RESOURCE_CAPACITY];
} ktech_project;

typedef enum ktech_project_result {
    KTECH_PROJECT_OK = 0,
    KTECH_PROJECT_NOT_FOUND,
    KTECH_PROJECT_INVALID,
    KTECH_PROJECT_TOO_LARGE,
    KTECH_PROJECT_TRUNCATED,
    KTECH_PROJECT_CORRUPT,
    KTECH_PROJECT_UNKNOWN_VERSION,
    KTECH_PROJECT_NONCANONICAL,
    KTECH_PROJECT_IO_ERROR,
    KTECH_PROJECT_SECURITY_ERROR,
    KTECH_PROJECT_EXISTS
} ktech_project_result;

void ktech_project_init(ktech_project *project, uint32_t seed);
bool ktech_project_validate(const ktech_project *project, char *error,
                            size_t error_size);

ktech_project_result ktech_project_encode(const ktech_project *project,
                                          void *bytes, size_t capacity,
                                          size_t *encoded_size);
ktech_project_result ktech_project_decode(ktech_project *project,
                                          const void *bytes, size_t size);

/* Both paths must be absolute. Save is private, fsync-backed, and atomic.
 * Existing destinations require replace_existing == true. */
ktech_project_result ktech_project_save(const ktech_project *project,
                                        const char *absolute_path,
                                        bool replace_existing);
ktech_project_result ktech_project_load(ktech_project *project,
                                        const char *absolute_path);

/* Durable XDG records use kilix-state's CRC envelope. */
ktech_project_result ktech_project_save_autosave(
    const ktech_project *project, const char *base_directory);
ktech_project_result ktech_project_load_autosave(
    ktech_project *project, const char *base_directory);
ktech_project_result ktech_project_remove_autosave(
    const char *base_directory);
ktech_project_result ktech_project_autosave_path(
    const char *base_directory, char *path, size_t path_size);
/* Named sibling slots beside autosave.state for sessions that must not write
 * the shared slot. `slot` is a bare file name in the same directory and is
 * validated by the store; a path never reaches the filesystem. */
ktech_project_result ktech_project_save_autosave_slot(
    const ktech_project *project, const char *base_directory,
    const char *slot);
ktech_project_result ktech_project_load_autosave_slot(
    ktech_project *project, const char *base_directory, const char *slot);
ktech_project_result ktech_project_remove_autosave_slot(
    const char *base_directory, const char *slot);
ktech_project_result ktech_project_autosave_slot_path(
    const char *base_directory, const char *slot, char *path,
    size_t path_size);
/* Move a damaged autosave to the fixed diagnostic sibling
 * autosave.corrupt.state. Existing diagnostic bytes are never replaced. */
ktech_project_result ktech_project_quarantine_autosave(
    const char *base_directory, char *path, size_t path_size);

const char *ktech_project_result_name(ktech_project_result result);
const char *ktech_role_name(ktech_role role);
ktech_role ktech_role_fallback(ktech_role role);
uint8_t ktech_role_default_note(ktech_role role);
bool ktech_track_length_allowed(uint8_t length);
bool ktech_project_set_track_length(ktech_project *project, uint8_t scene,
                                    uint8_t track, uint8_t length);
bool ktech_project_set_track_role(ktech_project *project, uint8_t scene,
                                  uint8_t track, ktech_role role);
bool ktech_binding_validate(const ktech_binding *binding);

bool ktech_project_pattern_create(ktech_project *project, uint8_t scene,
                                  uint8_t track, const char *name,
                                  uint8_t *pattern);
bool ktech_project_pattern_assign(ktech_project *project, uint8_t scene,
                                  uint8_t track, uint8_t pattern);
bool ktech_project_pattern_duplicate(ktech_project *project, uint8_t pattern,
                                     bool linked, uint8_t *duplicate);
const ktech_track *ktech_project_resolve_track(const ktech_project *project,
                                               uint8_t scene, uint8_t track,
                                               ktech_track *scratch);
bool ktech_project_arrangement_add(ktech_project *project, uint8_t scene,
                                   uint8_t bars,
                                   const uint8_t patterns[KTECH_TRACK_COUNT]);
uint16_t ktech_project_arrangement_bars(const ktech_project *project);

#ifdef __cplusplus
}
#endif

#endif
