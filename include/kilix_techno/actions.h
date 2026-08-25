#ifndef KILIX_TECHNO_ACTIONS_H
#define KILIX_TECHNO_ACTIONS_H

#include "kilix_techno/project.h"

#include "kitty_input.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KTECH_UNDO_CAPACITY 32u

typedef enum ktech_edit_field {
    KTECH_FIELD_NOTE = 0,
    KTECH_FIELD_VELOCITY,
    KTECH_FIELD_GATE,
    KTECH_FIELD_PROBABILITY,
    KTECH_FIELD_RATCHET,
    KTECH_FIELD_INSTRUMENT,
    KTECH_FIELD_TRACK_LENGTH,
    KTECH_FIELD_TRACK_LEVEL,
    KTECH_FIELD_TRACK_PAN,
    KTECH_FIELD_TEMPO,
    KTECH_FIELD_SWING,
    KTECH_FIELD_LAUNCH_QUANTIZATION,
    KTECH_FIELD_REDUCED_MOTION,
    KTECH_FIELD_MASTER_LEVEL,
    KTECH_FIELD_MASTER_TONE,
    KTECH_FIELD_MASTER_DRIVE,
    KTECH_FIELD_MASTER_DELAY,
    KTECH_FIELD_COUNT
} ktech_edit_field;

enum ktech_action_effect {
    KTECH_EFFECT_NONE = 0u,
    KTECH_EFFECT_REDRAW = 1u << 0,
    KTECH_EFFECT_PROJECT_CHANGED = 1u << 1,
    KTECH_EFFECT_MIX_CHANGED = 1u << 2,
    KTECH_EFFECT_PLAY_PAUSE = 1u << 3,
    KTECH_EFFECT_STOP = 1u << 4,
    KTECH_EFFECT_SAVE = 1u << 5,
    KTECH_EFFECT_EXPORT = 1u << 6,
    KTECH_EFFECT_QUIT = 1u << 7,
    KTECH_EFFECT_SCENE_LAUNCH = 1u << 8
    ,KTECH_EFFECT_RESTART = 1u << 9
    ,KTECH_EFFECT_TAP_TEMPO = 1u << 10
};

typedef struct ktech_tap_tempo {
    int64_t previous_ms;
    uint32_t intervals[4];
    uint8_t interval_count;
    uint8_t interval_next;
} ktech_tap_tempo;

typedef struct ktech_editor ktech_editor;

ktech_editor *ktech_editor_create(const ktech_project *project);
void ktech_editor_free(ktech_editor *editor);
ktech_project *ktech_editor_project(ktech_editor *editor);
const ktech_project *ktech_editor_project_const(const ktech_editor *editor);

unsigned ktech_editor_apply(ktech_editor *editor, ktech_action action,
                            kittyin_action_phase phase);
bool ktech_editor_select_step(ktech_editor *editor, uint8_t track,
                              uint8_t step, bool toggle);
bool ktech_editor_set_step_enabled(ktech_editor *editor, uint8_t track,
                                   uint8_t step, bool enabled);
void ktech_editor_begin_step_paint(ktech_editor *editor);
void ktech_editor_end_step_paint(ktech_editor *editor);
bool ktech_editor_select_track(ktech_editor *editor, uint8_t track);
bool ktech_editor_set_track_role(ktech_editor *editor, uint8_t track,
                                 ktech_role role);
bool ktech_editor_select_scene(ktech_editor *editor, uint8_t scene);
void ktech_editor_mark_saved(ktech_editor *editor);
bool ktech_editor_dirty(const ktech_editor *editor);
uint64_t ktech_editor_revision(const ktech_editor *editor);
uint8_t ktech_editor_track(const ktech_editor *editor);
uint8_t ktech_editor_step(const ktech_editor *editor);
uint8_t ktech_editor_scene(const ktech_editor *editor);
ktech_edit_field ktech_editor_field(const ktech_editor *editor);
bool ktech_editor_help_visible(const ktech_editor *editor);
bool ktech_editor_set_tempo(ktech_editor *editor, uint16_t tempo);

void ktech_tap_tempo_init(ktech_tap_tempo *tap);
bool ktech_tap_tempo_press(ktech_tap_tempo *tap, int64_t now_ms,
                           uint16_t *tempo);

void ktech_actions_bind_defaults(kittyin_action_map *map);
bool ktech_actions_bind_project(kittyin_action_map *map,
                                const ktech_project *project);
/* Map one key with exact-modifier bindings taking precedence over the input
 * library's legacy unshifted-letter compatibility aliases. */
size_t ktech_actions_map_key(kittyin_action_map *map,
                             const kittykb_event *key,
                             kittyin_action_event *events,
                             size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
