#ifndef KILIX_TECHNO_PROJECT_H
#define KILIX_TECHNO_PROJECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KTECH_VERSION_MAJOR 0u
#define KTECH_VERSION_MINOR 1u
#define KTECH_VERSION_PATCH 0u
#define KTECH_PROJECT_SCHEMA 1u

#define KTECH_TRACK_COUNT 8u
#define KTECH_SCENE_COUNT 16u
#define KTECH_STEP_COUNT 64u
#define KTECH_NAME_CAPACITY 64u
#define KTECH_PROJECT_MAX_BYTES (1024u * 1024u)
#define KTECH_PROJECT_ENCODED_MAX 65536u
#define KTECH_BINDING_CAPACITY 96u
#define KTECH_ANY_GAMEPAD UINT8_MAX

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
    KTECH_ROLE_COUNT
} ktech_role;

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
/* Move a damaged autosave to the fixed diagnostic sibling
 * autosave.corrupt.state. Existing diagnostic bytes are never replaced. */
ktech_project_result ktech_project_quarantine_autosave(
    const char *base_directory, char *path, size_t path_size);

const char *ktech_project_result_name(ktech_project_result result);
const char *ktech_role_name(ktech_role role);
bool ktech_track_length_allowed(uint8_t length);
bool ktech_project_set_track_length(ktech_project *project, uint8_t scene,
                                    uint8_t track, uint8_t length);
bool ktech_project_set_track_role(ktech_project *project, uint8_t scene,
                                  uint8_t track, ktech_role role);
bool ktech_binding_validate(const ktech_binding *binding);

#ifdef __cplusplus
}
#endif

#endif
