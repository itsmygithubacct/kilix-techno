#include "kilix_techno/actions.h"

#include "kitty_keyboard.h"

#include <stdlib.h>
#include <string.h>

struct ktech_editor {
    ktech_project project;
    ktech_project saved;
    ktech_project undo[KTECH_UNDO_CAPACITY];
    ktech_project redo[KTECH_UNDO_CAPACITY];
    size_t undo_count;
    size_t redo_count;
    uint64_t revision;
    uint64_t saved_revision;
    uint8_t scene;
    uint8_t track;
    uint8_t step;
    uint8_t field;
    bool help;
};

static void push_snapshot(ktech_project *stack, size_t *count,
                          const ktech_project *project)
{
    if (*count == KTECH_UNDO_CAPACITY) {
        memmove(&stack[0], &stack[1],
                (KTECH_UNDO_CAPACITY - 1u) * sizeof stack[0]);
        *count = KTECH_UNDO_CAPACITY - 1u;
    }
    stack[*count] = *project;
    ++*count;
}

static void begin_change(ktech_editor *editor)
{
    push_snapshot(editor->undo, &editor->undo_count, &editor->project);
    editor->redo_count = 0u;
}

static unsigned finish_change(ktech_editor *editor, unsigned extra)
{
    ++editor->revision;
    return KTECH_EFFECT_REDRAW | KTECH_EFFECT_PROJECT_CHANGED | extra;
}

static ktech_track *selected_track(ktech_editor *editor)
{
    return &editor->project.scenes[editor->scene].tracks[editor->track];
}

static ktech_step *selected_step(ktech_editor *editor)
{
    return &selected_track(editor)->steps[editor->step];
}

ktech_editor *ktech_editor_create(const ktech_project *project)
{
    if (!project || !ktech_project_validate(project, NULL, 0u)) return NULL;
    ktech_editor *editor = calloc(1u, sizeof *editor);
    if (!editor) return NULL;
    editor->project = *project;
    editor->saved = *project;
    editor->scene = project->active_scene;
    editor->saved_revision = 0u;
    return editor;
}

void ktech_editor_free(ktech_editor *editor)
{
    if (!editor) return;
    memset(editor, 0, sizeof *editor);
    free(editor);
}

ktech_project *ktech_editor_project(ktech_editor *editor)
{
    return editor ? &editor->project : NULL;
}

const ktech_project *ktech_editor_project_const(const ktech_editor *editor)
{
    return editor ? &editor->project : NULL;
}

static uint8_t clamp_add_u8(uint8_t value, int change,
                            uint8_t minimum, uint8_t maximum)
{
    int next = (int)value + change;
    if (next < minimum) next = minimum;
    if (next > maximum) next = maximum;
    return (uint8_t)next;
}

static uint16_t clamp_add_u16(uint16_t value, int change,
                              uint16_t minimum, uint16_t maximum)
{
    int next = (int)value + change;
    if (next < minimum) next = minimum;
    if (next > maximum) next = maximum;
    return (uint16_t)next;
}

static uint8_t next_length(uint8_t current, int direction)
{
    static const uint8_t lengths[] = {
        1u, 2u, 3u, 4u, 6u, 8u, 12u, 16u, 24u, 32u, 48u, 64u
    };
    size_t index = 0u;
    while (index + 1u < sizeof lengths / sizeof lengths[0] &&
           lengths[index] != current)
        ++index;
    if (direction < 0 && index > 0u) --index;
    if (direction > 0 && index + 1u < sizeof lengths / sizeof lengths[0])
        ++index;
    return lengths[index];
}

static unsigned adjust_field(ktech_editor *editor, int direction)
{
    ktech_track *track = selected_track(editor);
    ktech_step *step = selected_step(editor);
    begin_change(editor);
    unsigned extra = 0u;
    switch ((ktech_edit_field)editor->field) {
    case KTECH_FIELD_NOTE:
        step->note = clamp_add_u8(step->note, direction, 0u, 127u);
        break;
    case KTECH_FIELD_VELOCITY:
        step->velocity = clamp_add_u8(step->velocity, direction, 0u, 127u);
        break;
    case KTECH_FIELD_GATE:
        step->gate = clamp_add_u8(step->gate, direction, 1u, 100u);
        break;
    case KTECH_FIELD_PROBABILITY:
        step->probability = clamp_add_u8(step->probability, direction,
                                         0u, 100u);
        break;
    case KTECH_FIELD_RATCHET:
        step->ratchet = clamp_add_u8(step->ratchet, direction, 1u, 4u);
        break;
    case KTECH_FIELD_INSTRUMENT: {
        int role = (int)track->role + direction;
        if (role < 0) role = KTECH_ROLE_COUNT - 1;
        if (role >= KTECH_ROLE_COUNT) role = 0;
        (void)ktech_project_set_track_role(
            &editor->project, editor->scene, editor->track,
            (ktech_role)role);
        break;
    }
    case KTECH_FIELD_TRACK_LENGTH: {
        uint8_t length = next_length(track->length, direction);
        (void)ktech_project_set_track_length(&editor->project, editor->scene,
                                             editor->track, length);
        if (editor->step >= length) editor->step = (uint8_t)(length - 1u);
        break;
    }
    case KTECH_FIELD_TRACK_LEVEL:
        track->level = clamp_add_u8(track->level, direction, 0u, 100u);
        extra = KTECH_EFFECT_MIX_CHANGED;
        break;
    case KTECH_FIELD_TRACK_PAN:
        track->pan = clamp_add_u8(track->pan, direction * 2, 0u, 255u);
        extra = KTECH_EFFECT_MIX_CHANGED;
        break;
    case KTECH_FIELD_TEMPO:
        editor->project.tempo = clamp_add_u16(editor->project.tempo,
                                              direction, 20u, 255u);
        break;
    case KTECH_FIELD_SWING:
        editor->project.swing = clamp_add_u8(editor->project.swing,
                                             direction, 50u, 75u);
        break;
    case KTECH_FIELD_LAUNCH_QUANTIZATION: {
        int launch = (int)editor->project.launch_quantization + direction;
        if (launch < 0) launch = KTECH_LAUNCH_COUNT - 1;
        if (launch >= KTECH_LAUNCH_COUNT) launch = 0;
        editor->project.launch_quantization = (uint8_t)launch;
        break;
    }
    case KTECH_FIELD_REDUCED_MOTION:
        editor->project.reduced_motion ^= 1u;
        break;
    case KTECH_FIELD_MASTER_LEVEL:
        editor->project.master_level = clamp_add_u8(
            editor->project.master_level, direction, 0u, 100u);
        extra = KTECH_EFFECT_MIX_CHANGED;
        break;
    case KTECH_FIELD_MASTER_TONE:
        editor->project.master_tone = clamp_add_u8(
            editor->project.master_tone, direction, 0u, 100u);
        extra = KTECH_EFFECT_MIX_CHANGED;
        break;
    case KTECH_FIELD_MASTER_DRIVE:
        editor->project.master_drive = clamp_add_u8(
            editor->project.master_drive, direction, 0u, 100u);
        extra = KTECH_EFFECT_MIX_CHANGED;
        break;
    case KTECH_FIELD_MASTER_DELAY:
        editor->project.master_delay = clamp_add_u8(
            editor->project.master_delay, direction, 0u, 100u);
        extra = KTECH_EFFECT_MIX_CHANGED;
        break;
    default:
        --editor->undo_count;
        return KTECH_EFFECT_NONE;
    }
    if (memcmp(&editor->project, &editor->undo[editor->undo_count - 1u],
               sizeof editor->project) == 0) {
        --editor->undo_count;
        return KTECH_EFFECT_NONE;
    }
    return finish_change(editor, extra);
}

static unsigned undo(ktech_editor *editor)
{
    if (editor->undo_count == 0u) return KTECH_EFFECT_NONE;
    push_snapshot(editor->redo, &editor->redo_count, &editor->project);
    uint8_t active_scene = editor->project.active_scene;
    editor->project = editor->undo[--editor->undo_count];
    editor->project.active_scene = active_scene;
    ++editor->revision;
    if (editor->step >= selected_track(editor)->length)
        editor->step = (uint8_t)(selected_track(editor)->length - 1u);
    return KTECH_EFFECT_REDRAW | KTECH_EFFECT_PROJECT_CHANGED |
           KTECH_EFFECT_MIX_CHANGED;
}

static unsigned redo(ktech_editor *editor)
{
    if (editor->redo_count == 0u) return KTECH_EFFECT_NONE;
    push_snapshot(editor->undo, &editor->undo_count, &editor->project);
    uint8_t active_scene = editor->project.active_scene;
    editor->project = editor->redo[--editor->redo_count];
    editor->project.active_scene = active_scene;
    ++editor->revision;
    if (editor->step >= selected_track(editor)->length)
        editor->step = (uint8_t)(selected_track(editor)->length - 1u);
    return KTECH_EFFECT_REDRAW | KTECH_EFFECT_PROJECT_CHANGED |
           KTECH_EFFECT_MIX_CHANGED;
}

unsigned ktech_editor_apply(ktech_editor *editor, ktech_action action,
                            kittyin_action_phase phase)
{
    if (!editor || action >= KTECH_ACTION_COUNT ||
        phase == KITTYIN_ACTION_RELEASE)
        return KTECH_EFFECT_NONE;
    if (phase == KITTYIN_ACTION_REPEAT &&
        action != KTECH_ACTION_LEFT && action != KTECH_ACTION_RIGHT &&
        action != KTECH_ACTION_UP && action != KTECH_ACTION_DOWN &&
        action != KTECH_ACTION_NEXT_TRACK &&
        action != KTECH_ACTION_PREVIOUS_TRACK &&
        action != KTECH_ACTION_DECREASE && action != KTECH_ACTION_INCREASE)
        return KTECH_EFFECT_NONE;
    ktech_track *track = selected_track(editor);
    switch (action) {
    case KTECH_ACTION_LEFT:
        editor->step = editor->step == 0u ? (uint8_t)(track->length - 1u) :
                                           (uint8_t)(editor->step - 1u);
        return KTECH_EFFECT_REDRAW;
    case KTECH_ACTION_RIGHT:
        editor->step = (uint8_t)((editor->step + 1u) % track->length);
        return KTECH_EFFECT_REDRAW;
    case KTECH_ACTION_UP:
        editor->field = editor->field == 0u ? KTECH_FIELD_COUNT - 1u :
                                             (uint8_t)(editor->field - 1u);
        return KTECH_EFFECT_REDRAW;
    case KTECH_ACTION_DOWN:
        editor->field = (uint8_t)((editor->field + 1u) % KTECH_FIELD_COUNT);
        return KTECH_EFFECT_REDRAW;
    case KTECH_ACTION_NEXT_TRACK:
        editor->track = (uint8_t)((editor->track + 1u) % KTECH_TRACK_COUNT);
        if (editor->step >= selected_track(editor)->length)
            editor->step = (uint8_t)(selected_track(editor)->length - 1u);
        return KTECH_EFFECT_REDRAW;
    case KTECH_ACTION_PREVIOUS_TRACK:
        editor->track = editor->track == 0u ? KTECH_TRACK_COUNT - 1u :
                                             (uint8_t)(editor->track - 1u);
        if (editor->step >= selected_track(editor)->length)
            editor->step = (uint8_t)(selected_track(editor)->length - 1u);
        return KTECH_EFFECT_REDRAW;
    case KTECH_ACTION_ACTIVATE:
        begin_change(editor);
        selected_step(editor)->enabled ^= 1u;
        return finish_change(editor, 0u);
    case KTECH_ACTION_DECREASE: return adjust_field(editor, -1);
    case KTECH_ACTION_INCREASE: return adjust_field(editor, 1);
    case KTECH_ACTION_PLAY_PAUSE:
        return KTECH_EFFECT_PLAY_PAUSE | KTECH_EFFECT_REDRAW;
    case KTECH_ACTION_STOP: return KTECH_EFFECT_STOP | KTECH_EFFECT_REDRAW;
    case KTECH_ACTION_RESTART:
        return KTECH_EFFECT_RESTART | KTECH_EFFECT_REDRAW;
    case KTECH_ACTION_TAP_TEMPO:
        return KTECH_EFFECT_TAP_TEMPO | KTECH_EFFECT_REDRAW;
    case KTECH_ACTION_MUTE:
        begin_change(editor);
        selected_track(editor)->mute ^= 1u;
        return finish_change(editor, KTECH_EFFECT_MIX_CHANGED);
    case KTECH_ACTION_SOLO:
        begin_change(editor);
        selected_track(editor)->solo ^= 1u;
        return finish_change(editor, KTECH_EFFECT_MIX_CHANGED);
    case KTECH_ACTION_UNDO: return undo(editor);
    case KTECH_ACTION_REDO: return redo(editor);
    case KTECH_ACTION_SAVE: return KTECH_EFFECT_SAVE;
    case KTECH_ACTION_EXPORT: return KTECH_EFFECT_EXPORT;
    case KTECH_ACTION_HELP:
        editor->help = !editor->help;
        return KTECH_EFFECT_REDRAW;
    case KTECH_ACTION_QUIT: return KTECH_EFFECT_QUIT;
    case KTECH_ACTION_PREVIOUS_SCENE:
        editor->scene = editor->scene == 0u ? KTECH_SCENE_COUNT - 1u :
                                             (uint8_t)(editor->scene - 1u);
        return KTECH_EFFECT_REDRAW;
    case KTECH_ACTION_NEXT_SCENE:
        editor->scene = (uint8_t)((editor->scene + 1u) % KTECH_SCENE_COUNT);
        return KTECH_EFFECT_REDRAW;
    case KTECH_ACTION_LAUNCH_SCENE:
        if (editor->project.active_scene != editor->scene) {
            editor->project.active_scene = editor->scene;
            ++editor->revision;
        }
        return KTECH_EFFECT_SCENE_LAUNCH | KTECH_EFFECT_REDRAW;
    default: return KTECH_EFFECT_NONE;
    }
}

bool ktech_editor_select_step(ktech_editor *editor, uint8_t track,
                              uint8_t step, bool toggle)
{
    if (!editor || track >= KTECH_TRACK_COUNT) return false;
    ktech_track *selected = &editor->project.scenes[editor->scene].tracks[track];
    if (step >= selected->length) return false;
    editor->track = track;
    editor->step = step;
    if (toggle) {
        begin_change(editor);
        selected->steps[step].enabled ^= 1u;
        (void)finish_change(editor, 0u);
    }
    return true;
}

bool ktech_editor_set_step_enabled(ktech_editor *editor, uint8_t track,
                                   uint8_t step, bool enabled)
{
    if (!editor || track >= KTECH_TRACK_COUNT) return false;
    ktech_track *selected = &editor->project.scenes[editor->scene].tracks[track];
    if (step >= selected->length) return false;
    editor->track = track;
    editor->step = step;
    uint8_t canonical = enabled ? 1u : 0u;
    if (selected->steps[step].enabled == canonical) return true;
    begin_change(editor);
    selected->steps[step].enabled = canonical;
    (void)finish_change(editor, 0u);
    return true;
}

bool ktech_editor_select_track(ktech_editor *editor, uint8_t track)
{
    if (!editor || track >= KTECH_TRACK_COUNT || editor->track == track)
        return false;
    editor->track = track;
    if (editor->step >= selected_track(editor)->length)
        editor->step = (uint8_t)(selected_track(editor)->length - 1u);
    return true;
}

bool ktech_editor_select_scene(ktech_editor *editor, uint8_t scene)
{
    if (!editor || scene >= KTECH_SCENE_COUNT || editor->scene == scene)
        return false;
    editor->scene = scene;
    if (editor->step >= selected_track(editor)->length)
        editor->step = (uint8_t)(selected_track(editor)->length - 1u);
    return true;
}

void ktech_editor_mark_saved(ktech_editor *editor)
{
    if (!editor) return;
    editor->saved = editor->project;
    editor->saved_revision = editor->revision;
}

bool ktech_editor_dirty(const ktech_editor *editor)
{
    return editor && memcmp(&editor->project, &editor->saved,
                            sizeof editor->project) != 0;
}

uint64_t ktech_editor_revision(const ktech_editor *editor)
{
    return editor ? editor->revision : 0u;
}

uint8_t ktech_editor_track(const ktech_editor *editor)
{
    return editor ? editor->track : 0u;
}

uint8_t ktech_editor_step(const ktech_editor *editor)
{
    return editor ? editor->step : 0u;
}

uint8_t ktech_editor_scene(const ktech_editor *editor)
{
    return editor ? editor->scene : 0u;
}

ktech_edit_field ktech_editor_field(const ktech_editor *editor)
{
    return editor ? (ktech_edit_field)editor->field : KTECH_FIELD_NOTE;
}

bool ktech_editor_help_visible(const ktech_editor *editor)
{
    return editor && editor->help;
}

bool ktech_editor_set_tempo(ktech_editor *editor, uint16_t tempo)
{
    if (!editor || tempo < 20u || tempo > 255u ||
        editor->project.tempo == tempo)
        return false;
    begin_change(editor);
    editor->project.tempo = tempo;
    (void)finish_change(editor, 0u);
    return true;
}

void ktech_tap_tempo_init(ktech_tap_tempo *tap)
{
    if (tap) memset(tap, 0, sizeof *tap);
}

bool ktech_tap_tempo_press(ktech_tap_tempo *tap, int64_t now_ms,
                           uint16_t *tempo)
{
    if (!tap || !tempo || now_ms <= 0) return false;
    if (tap->previous_ms <= 0 || now_ms <= tap->previous_ms ||
        now_ms - tap->previous_ms > 3000) {
        tap->interval_count = 0u;
        tap->interval_next = 0u;
        tap->previous_ms = now_ms;
        return false;
    }
    int64_t elapsed = now_ms - tap->previous_ms;
    tap->previous_ms = now_ms;
    if (elapsed < 235) elapsed = 235;
    if (elapsed > 3000) elapsed = 3000;
    tap->intervals[tap->interval_next] = (uint32_t)elapsed;
    tap->interval_next = (uint8_t)((tap->interval_next + 1u) % 4u);
    if (tap->interval_count < 4u) ++tap->interval_count;
    uint32_t sum = 0u;
    for (uint8_t index = 0u; index < tap->interval_count; ++index)
        sum += tap->intervals[index];
    uint32_t average = (sum + tap->interval_count / 2u) /
                       tap->interval_count;
    uint32_t bpm = (UINT32_C(60000) + average / 2u) / average;
    if (bpm < 20u) bpm = 20u;
    if (bpm > 255u) bpm = 255u;
    *tempo = (uint16_t)bpm;
    return true;
}

bool ktech_actions_bind_project(kittyin_action_map *map,
                                const ktech_project *project)
{
    if (!map || !ktech_project_validate(project, NULL, 0u)) return false;
    kittyin_action_map temporary;
    kittyin_action_map_init(&temporary);
    for (uint16_t index = 0u; index < project->binding_count; ++index) {
        const ktech_binding *binding = &project->bindings[index];
        bool success;
        if (binding->kind == KTECH_BINDING_KEY) {
            success = kittyin_action_map_bind_key(
                &temporary, binding->action, binding->key,
                binding->modifiers);
        } else if (binding->kind == KTECH_BINDING_GAMEPAD_BUTTON) {
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

void ktech_actions_bind_defaults(kittyin_action_map *map)
{
    if (!map) return;
    ktech_project project;
    ktech_project_init(&project, UINT32_C(0x4b544543));
    if (!ktech_actions_bind_project(map, &project))
        kittyin_action_map_init(map);
}

size_t ktech_actions_map_key(kittyin_action_map *map,
                             const kittykb_event *key,
                             kittyin_action_event *events,
                             size_t capacity)
{
    if (!map || !key) return 0u;
    size_t count = kittyin_action_map_key(map, key, events, capacity);
    uint32_t modifiers = key->modifiers &
        (KITTYKB_MOD_SHIFT | KITTYKB_MOD_ALT | KITTYKB_MOD_CTRL |
         KITTYKB_MOD_SUPER | KITTYKB_MOD_HYPER | KITTYKB_MOD_META);
    bool exact[KTECH_ACTION_COUNT];
    memset(exact, 0, sizeof exact);
    bool have_exact = false;
    for (size_t index = 0u; index < map->binding_count; ++index) {
        const kittyin_action_binding *binding = &map->bindings[index];
        if (binding->kind == KITTYIN_BINDING_KEY &&
            binding->action < KTECH_ACTION_COUNT &&
            binding->modifiers == modifiers &&
            kittykb_event_matches_key(key, binding->key)) {
            exact[binding->action] = true;
            have_exact = true;
        }
    }
    if (!have_exact || !events) return count;
    size_t written = count < capacity ? count : capacity;
    size_t kept = 0u;
    for (size_t index = 0u; index < written; ++index)
        if (events[index].action < KTECH_ACTION_COUNT &&
            exact[events[index].action])
            events[kept++] = events[index];
    return kept;
}
