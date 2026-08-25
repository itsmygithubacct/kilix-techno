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
    bool step_paint_active;
    bool step_paint_changed;
    bool range_active;
    ktech_step_range range;
    bool clipboard_valid;
    uint8_t clipboard_tracks;
    uint8_t clipboard_steps;
    ktech_step clipboard[KTECH_TRACK_COUNT][KTECH_STEP_COUNT];
    uint64_t favorites;
    uint8_t view;
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

static ktech_pattern *track_pattern(ktech_editor *editor, uint8_t track)
{
    uint8_t reference = editor->project.scene_patterns[editor->scene][track];
    return reference < editor->project.pattern_count ?
        &editor->project.patterns[reference] : NULL;
}

static uint8_t content_length(ktech_editor *editor, uint8_t track)
{
    ktech_pattern *pattern = track_pattern(editor, track);
    return pattern ? pattern->length :
        editor->project.scenes[editor->scene].tracks[track].length;
}

static ktech_step *content_steps(ktech_editor *editor, uint8_t track)
{
    ktech_pattern *pattern = track_pattern(editor, track);
    return pattern ? pattern->steps :
        editor->project.scenes[editor->scene].tracks[track].steps;
}

static ktech_step *selected_step(ktech_editor *editor)
{
    return &content_steps(editor, editor->track)[editor->step];
}

static void pattern_set_length(ktech_pattern *pattern, uint8_t length)
{
    pattern->length = length;
    ktech_step reset = {0u, ktech_role_default_note(
                               (ktech_role)pattern->role),
                        100u, 80u, 100u, 1u};
    for (uint8_t step = length; step < KTECH_STEP_COUNT; ++step)
        pattern->steps[step] = reset;
}

static void pattern_set_role(ktech_pattern *pattern, ktech_role role)
{
    pattern->role = (uint8_t)role;
    pattern_set_length(pattern, pattern->length);
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
        ktech_pattern *pattern = track_pattern(editor, editor->track);
        uint8_t current_role = pattern ? pattern->role : track->role;
        int role = (int)current_role + direction;
        if (role < 0) role = KTECH_ROLE_COUNT - 1;
        if (role >= KTECH_ROLE_COUNT) role = 0;
        if (pattern) pattern_set_role(pattern, (ktech_role)role);
        else (void)ktech_project_set_track_role(
                 &editor->project, editor->scene, editor->track,
                 (ktech_role)role);
        break;
    }
    case KTECH_FIELD_TRACK_LENGTH: {
        ktech_pattern *pattern = track_pattern(editor, editor->track);
        uint8_t length = next_length(pattern ? pattern->length : track->length,
                                     direction);
        if (pattern) pattern_set_length(pattern, length);
        else (void)ktech_project_set_track_length(
                 &editor->project, editor->scene, editor->track, length);
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
    if (editor->step >= content_length(editor, editor->track))
        editor->step = (uint8_t)(content_length(editor, editor->track) - 1u);
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
    if (editor->step >= content_length(editor, editor->track))
        editor->step = (uint8_t)(content_length(editor, editor->track) - 1u);
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
    uint8_t length = content_length(editor, editor->track);
    switch (action) {
    case KTECH_ACTION_LEFT:
        editor->step = editor->step == 0u ? (uint8_t)(length - 1u) :
                                           (uint8_t)(editor->step - 1u);
        return KTECH_EFFECT_REDRAW;
    case KTECH_ACTION_RIGHT:
        editor->step = (uint8_t)((editor->step + 1u) % length);
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
        if (editor->step >= content_length(editor, editor->track))
            editor->step = (uint8_t)(content_length(editor, editor->track) - 1u);
        return KTECH_EFFECT_REDRAW;
    case KTECH_ACTION_PREVIOUS_TRACK:
        editor->track = editor->track == 0u ? KTECH_TRACK_COUNT - 1u :
                                             (uint8_t)(editor->track - 1u);
        if (editor->step >= content_length(editor, editor->track))
            editor->step = (uint8_t)(content_length(editor, editor->track) - 1u);
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
    ktech_step *steps = content_steps(editor, track);
    if (step >= content_length(editor, track)) return false;
    editor->track = track;
    editor->step = step;
    if (toggle) {
        begin_change(editor);
        steps[step].enabled ^= 1u;
        (void)finish_change(editor, 0u);
    }
    return true;
}

bool ktech_editor_set_step_enabled(ktech_editor *editor, uint8_t track,
                                   uint8_t step, bool enabled)
{
    if (!editor || track >= KTECH_TRACK_COUNT) return false;
    ktech_step *steps = content_steps(editor, track);
    if (step >= content_length(editor, track)) return false;
    editor->track = track;
    editor->step = step;
    uint8_t canonical = enabled ? 1u : 0u;
    if (steps[step].enabled == canonical) return true;
    if (!editor->step_paint_active || !editor->step_paint_changed) {
        begin_change(editor);
        if (editor->step_paint_active) editor->step_paint_changed = true;
    }
    steps[step].enabled = canonical;
    (void)finish_change(editor, 0u);
    return true;
}

bool ktech_editor_set_step_note(ktech_editor *editor, uint8_t track,
                                uint8_t step, uint8_t note)
{
    if (!editor || track >= KTECH_TRACK_COUNT || note > 127u) return false;
    ktech_step *steps = content_steps(editor, track);
    if (step >= content_length(editor, track)) return false;
    editor->track = track;
    editor->step = step;
    if (steps[step].note == note) return true;
    if (!editor->step_paint_active || !editor->step_paint_changed) {
        begin_change(editor);
        if (editor->step_paint_active) editor->step_paint_changed = true;
    }
    steps[step].note = note;
    (void)finish_change(editor, 0u);
    return true;
}

void ktech_editor_begin_step_paint(ktech_editor *editor)
{
    if (!editor) return;
    editor->step_paint_active = true;
    editor->step_paint_changed = false;
}

void ktech_editor_end_step_paint(ktech_editor *editor)
{
    if (!editor) return;
    editor->step_paint_active = false;
    editor->step_paint_changed = false;
}

bool ktech_editor_select_track(ktech_editor *editor, uint8_t track)
{
    if (!editor || track >= KTECH_TRACK_COUNT || editor->track == track)
        return false;
    editor->track = track;
    if (editor->step >= content_length(editor, editor->track))
        editor->step = (uint8_t)(content_length(editor, editor->track) - 1u);
    return true;
}

bool ktech_editor_set_track_role(ktech_editor *editor, uint8_t track,
                                 ktech_role role)
{
    if (!editor || track >= KTECH_TRACK_COUNT || role >= KTECH_ROLE_COUNT)
        return false;
    editor->track = track;
    ktech_track *selected = selected_track(editor);
    ktech_pattern *pattern = track_pattern(editor, track);
    uint8_t old_role = pattern ? pattern->role : selected->role;
    if (old_role == (uint8_t)role) return true;
    begin_change(editor);
    if (pattern) pattern_set_role(pattern, role);
    else (void)ktech_project_set_track_role(&editor->project, editor->scene,
                                            track, role);
    (void)finish_change(editor, 0u);
    return true;
}

bool ktech_editor_select_scene(ktech_editor *editor, uint8_t scene)
{
    if (!editor || scene >= KTECH_SCENE_COUNT || editor->scene == scene)
        return false;
    editor->scene = scene;
    if (editor->step >= content_length(editor, editor->track))
        editor->step = (uint8_t)(content_length(editor, editor->track) - 1u);
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

static void normalize_range(ktech_step_range *range)
{
    if (range->first_track > range->last_track) {
        uint8_t swap = range->first_track;
        range->first_track = range->last_track;
        range->last_track = swap;
    }
    if (range->first_step > range->last_step) {
        uint8_t swap = range->first_step;
        range->first_step = range->last_step;
        range->last_step = swap;
    }
}

bool ktech_editor_select_range(ktech_editor *editor, uint8_t first_track,
                               uint8_t first_step, uint8_t last_track,
                               uint8_t last_step)
{
    if (!editor || first_track >= KTECH_TRACK_COUNT ||
        last_track >= KTECH_TRACK_COUNT || first_step >= KTECH_STEP_COUNT ||
        last_step >= KTECH_STEP_COUNT)
        return false;
    ktech_step_range range = {first_track, last_track, first_step, last_step};
    normalize_range(&range);
    for (uint8_t track = range.first_track; track <= range.last_track;
         ++track) {
        if (range.last_step >= content_length(editor, track))
            return false;
    }
    editor->range = range;
    editor->range_active = true;
    editor->track = last_track;
    editor->step = last_step;
    return true;
}

void ktech_editor_clear_range(ktech_editor *editor)
{
    if (editor) editor->range_active = false;
}

bool ktech_editor_range(const ktech_editor *editor, ktech_step_range *range)
{
    if (!editor || !editor->range_active) return false;
    if (range) *range = editor->range;
    return true;
}

bool ktech_editor_step_selected(const ktech_editor *editor, uint8_t track,
                                uint8_t step)
{
    return editor && editor->range_active &&
           track >= editor->range.first_track &&
           track <= editor->range.last_track &&
           step >= editor->range.first_step &&
           step <= editor->range.last_step;
}

bool ktech_editor_copy_range(ktech_editor *editor)
{
    if (!editor || !editor->range_active) return false;
    const ktech_step_range *range = &editor->range;
    editor->clipboard_tracks = (uint8_t)(range->last_track -
                                         range->first_track + 1u);
    editor->clipboard_steps = (uint8_t)(range->last_step -
                                        range->first_step + 1u);
    memset(editor->clipboard, 0, sizeof editor->clipboard);
    for (uint8_t track = 0u; track < editor->clipboard_tracks; ++track)
        for (uint8_t step = 0u; step < editor->clipboard_steps; ++step)
            editor->clipboard[track][step] =
                content_steps(editor, (uint8_t)(range->first_track + track))
                    [range->first_step + step];
    editor->clipboard_valid = true;
    return true;
}

static bool selection_begin_change(ktech_editor *editor)
{
    if (!editor || !editor->range_active) return false;
    begin_change(editor);
    return true;
}

static bool selection_finish_change(ktech_editor *editor)
{
    if (memcmp(&editor->project, &editor->undo[editor->undo_count - 1u],
               sizeof editor->project) == 0) {
        --editor->undo_count;
        return false;
    }
    (void)finish_change(editor, 0u);
    return true;
}

bool ktech_editor_cut_range(ktech_editor *editor)
{
    if (!ktech_editor_copy_range(editor) || !selection_begin_change(editor))
        return false;
    for (uint8_t track = editor->range.first_track;
         track <= editor->range.last_track; ++track)
        for (uint8_t step = editor->range.first_step;
             step <= editor->range.last_step; ++step)
            content_steps(editor, track)[step].enabled = 0u;
    return selection_finish_change(editor);
}

bool ktech_editor_paste_range(ktech_editor *editor, uint8_t track,
                              uint8_t step)
{
    if (!editor || !editor->clipboard_valid || track >= KTECH_TRACK_COUNT ||
        step >= KTECH_STEP_COUNT ||
        (unsigned)track + editor->clipboard_tracks > KTECH_TRACK_COUNT)
        return false;
    for (uint8_t row = 0u; row < editor->clipboard_tracks; ++row) {
        if ((unsigned)step + editor->clipboard_steps >
            content_length(editor, (uint8_t)(track + row)))
            return false;
    }
    begin_change(editor);
    for (uint8_t row = 0u; row < editor->clipboard_tracks; ++row)
        for (uint8_t column = 0u; column < editor->clipboard_steps; ++column)
            content_steps(editor, (uint8_t)(track + row))[step + column] =
                editor->clipboard[row][column];
    editor->range = (ktech_step_range){
        track, (uint8_t)(track + editor->clipboard_tracks - 1u), step,
        (uint8_t)(step + editor->clipboard_steps - 1u)};
    editor->range_active = true;
    return selection_finish_change(editor);
}

bool ktech_editor_apply_tool(ktech_editor *editor, ktech_step_tool tool)
{
    if (!editor || !editor->range_active || tool >= KTECH_TOOL_COUNT)
        return false;
    if (!selection_begin_change(editor)) return false;
    const ktech_step_range range = editor->range;
    for (uint8_t track = range.first_track; track <= range.last_track;
         ++track) {
        ktech_step *steps = content_steps(editor, track);
        uint8_t width = (uint8_t)(range.last_step - range.first_step + 1u);
        if (tool == KTECH_TOOL_FILL || tool == KTECH_TOOL_CLEAR ||
            tool == KTECH_TOOL_INVERT) {
            for (uint8_t step = range.first_step; step <= range.last_step;
                 ++step) {
                if (tool == KTECH_TOOL_FILL) steps[step].enabled = 1u;
                else if (tool == KTECH_TOOL_CLEAR) steps[step].enabled = 0u;
                else steps[step].enabled ^= 1u;
            }
        } else if (tool == KTECH_TOOL_MIRROR) {
            for (uint8_t offset = 0u; offset < width / 2u; ++offset) {
                ktech_step swap = steps[range.first_step + offset];
                steps[range.first_step + offset] =
                    steps[range.last_step - offset];
                steps[range.last_step - offset] = swap;
            }
        } else {
            ktech_step saved = tool == KTECH_TOOL_ROTATE_LEFT ?
                steps[range.first_step] : steps[range.last_step];
            if (tool == KTECH_TOOL_ROTATE_LEFT) {
                memmove(&steps[range.first_step],
                        &steps[range.first_step + 1u],
                        (size_t)(width - 1u) * sizeof steps[0]);
                steps[range.last_step] = saved;
            } else {
                memmove(&steps[range.first_step + 1u],
                        &steps[range.first_step],
                        (size_t)(width - 1u) * sizeof steps[0]);
                steps[range.first_step] = saved;
            }
        }
    }
    return selection_finish_change(editor);
}

bool ktech_editor_probability_scale(ktech_editor *editor, uint8_t percent)
{
    if (!editor || !editor->range_active || percent > 200u ||
        !selection_begin_change(editor))
        return false;
    for (uint8_t track = editor->range.first_track;
         track <= editor->range.last_track; ++track)
        for (uint8_t step = editor->range.first_step;
             step <= editor->range.last_step; ++step) {
            ktech_step *value = &content_steps(editor, track)[step];
            unsigned scaled = ((unsigned)value->probability * percent + 50u) /
                              100u;
            value->probability = (uint8_t)(scaled > 100u ? 100u : scaled);
        }
    return selection_finish_change(editor);
}

bool ktech_editor_velocity_ramp(ktech_editor *editor, uint8_t first,
                                uint8_t last)
{
    if (!editor || !editor->range_active || first > 127u || last > 127u ||
        !selection_begin_change(editor))
        return false;
    unsigned width = (unsigned)editor->range.last_step -
                     editor->range.first_step + 1u;
    for (uint8_t track = editor->range.first_track;
         track <= editor->range.last_track; ++track)
        for (unsigned offset = 0u; offset < width; ++offset) {
            int delta = (int)last - (int)first;
            int value = width == 1u ? first :
                (int)first + (delta * (int)offset) / (int)(width - 1u);
            content_steps(editor, track)
                [editor->range.first_step + offset].velocity =
                    (uint8_t)value;
        }
    return selection_finish_change(editor);
}

bool ktech_editor_euclidean_fill(ktech_editor *editor, uint8_t pulses,
                                 uint8_t rotation)
{
    if (!editor || !editor->range_active ||
        !selection_begin_change(editor))
        return false;
    unsigned width = (unsigned)editor->range.last_step -
                     editor->range.first_step + 1u;
    if (pulses > width) {
        --editor->undo_count;
        return false;
    }
    for (uint8_t track = editor->range.first_track;
         track <= editor->range.last_track; ++track)
        for (unsigned offset = 0u; offset < width; ++offset) {
            unsigned position = (offset + width - rotation % width) % width;
            content_steps(editor, track)
                [editor->range.first_step + offset].enabled =
                    (uint8_t)(((position * pulses) % width) < pulses);
        }
    return selection_finish_change(editor);
}

bool ktech_editor_set_favorite(ktech_editor *editor, ktech_role role,
                               bool favorite)
{
    if (!editor || role >= KTECH_ROLE_COUNT || role >= 64u) return false;
    uint64_t bit = UINT64_C(1) << role;
    if (favorite) editor->favorites |= bit;
    else editor->favorites &= ~bit;
    return true;
}

bool ktech_editor_favorite(const ktech_editor *editor, ktech_role role)
{
    return editor && role < KTECH_ROLE_COUNT && role < 64u &&
           (editor->favorites & (UINT64_C(1) << role)) != 0u;
}

bool ktech_editor_create_pattern(ktech_editor *editor, const char *name,
                                 uint8_t *pattern)
{
    if (!editor || !name) return false;
    begin_change(editor);
    if (!ktech_project_pattern_create(&editor->project, editor->scene,
                                      editor->track, name, pattern)) {
        --editor->undo_count;
        return false;
    }
    (void)finish_change(editor, 0u);
    return true;
}

bool ktech_editor_duplicate_pattern(ktech_editor *editor, bool linked,
                                    uint8_t *pattern)
{
    if (!editor) return false;
    uint8_t source = editor->project.scene_patterns[editor->scene]
                                                  [editor->track];
    if (source == KTECH_PATTERN_NONE) return false;
    begin_change(editor);
    uint8_t duplicate;
    if (!ktech_project_pattern_duplicate(&editor->project, source, linked,
                                         &duplicate) ||
        !ktech_project_pattern_assign(&editor->project, editor->scene,
                                      editor->track, duplicate)) {
        --editor->undo_count;
        return false;
    }
    if (linked) {
        --editor->undo_count;
    } else {
        (void)finish_change(editor, 0u);
    }
    if (pattern) *pattern = duplicate;
    return true;
}

bool ktech_editor_append_arrangement(ktech_editor *editor, uint8_t bars)
{
    if (!editor) return false;
    begin_change(editor);
    if (!ktech_project_arrangement_add(&editor->project, editor->scene,
                                       bars, NULL)) {
        --editor->undo_count;
        return false;
    }
    (void)finish_change(editor, 0u);
    return true;
}

ktech_editor_view ktech_editor_view_current(const ktech_editor *editor)
{
    return editor && editor->view < KTECH_VIEW_COUNT ?
        (ktech_editor_view)editor->view : KTECH_VIEW_STEPS;
}

bool ktech_editor_set_view(ktech_editor *editor, ktech_editor_view view)
{
    if (!editor || view >= KTECH_VIEW_COUNT) return false;
    editor->view = (uint8_t)view;
    return true;
}

bool ktech_editor_paint_range_value(ktech_editor *editor,
                                    ktech_edit_field field, uint8_t value)
{
    if (!editor || !editor->range_active ||
        (field != KTECH_FIELD_NOTE && field != KTECH_FIELD_VELOCITY &&
         field != KTECH_FIELD_GATE && field != KTECH_FIELD_PROBABILITY &&
         field != KTECH_FIELD_RATCHET) ||
        ((field == KTECH_FIELD_NOTE || field == KTECH_FIELD_VELOCITY) &&
         value > 127u) ||
        ((field == KTECH_FIELD_GATE || field == KTECH_FIELD_RATCHET) &&
         value == 0u) ||
        ((field == KTECH_FIELD_GATE || field == KTECH_FIELD_PROBABILITY) &&
         value > 100u) ||
        (field == KTECH_FIELD_RATCHET && value > 4u) ||
        !selection_begin_change(editor)) return false;
    for (uint8_t track = editor->range.first_track;
         track <= editor->range.last_track; ++track)
        for (uint8_t step = editor->range.first_step;
             step <= editor->range.last_step; ++step) {
            ktech_step *target = &content_steps(editor, track)[step];
            if (field == KTECH_FIELD_NOTE) target->note = value;
            else if (field == KTECH_FIELD_VELOCITY) target->velocity = value;
            else if (field == KTECH_FIELD_GATE) target->gate = value;
            else if (field == KTECH_FIELD_PROBABILITY)
                target->probability = value;
            else target->ratchet = value;
        }
    return selection_finish_change(editor);
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
