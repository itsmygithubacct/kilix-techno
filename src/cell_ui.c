#include "kilix_techno/ui.h"

#include "kilix_techno/soundbank.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct cell_writer {
    char *output;
    size_t capacity;
    size_t used;
} cell_writer;

static void append(cell_writer *writer, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int count;
    if (writer->used < writer->capacity) {
        count = vsnprintf(writer->output + writer->used,
                          writer->capacity - writer->used, format, arguments);
    } else {
        char scratch[1];
        count = vsnprintf(scratch, sizeof scratch, format, arguments);
    }
    va_end(arguments);
    if (count > 0) writer->used += (size_t)count;
}

static const char *field_name(ktech_edit_field field)
{
    static const char *const names[KTECH_FIELD_COUNT] = {
        "note", "velocity", "gate", "probability", "ratchet",
        "instrument", "length", "track level", "track pan", "tempo",
        "swing", "scene quantize", "reduced motion", "master level",
        "master tone", "master drive", "master delay"
    };
    return field < KTECH_FIELD_COUNT ? names[field] : "field";
}

static const char *launch_name(uint8_t launch)
{
    return launch == KTECH_LAUNCH_STEP ? "step" :
           launch == KTECH_LAUNCH_BEAT ? "beat" : "bar";
}

static void resolve_scene(const ktech_project *project, uint8_t scene,
                          ktech_scene *resolved)
{
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
        ktech_track scratch;
        const ktech_track *source = ktech_project_resolve_track(
            project, scene, track, &scratch);
        resolved->tracks[track] = source ? *source :
            project->scenes[scene].tracks[track];
    }
}

static size_t finish(cell_writer *writer)
{
    if (writer->capacity > 0u) {
        size_t end = writer->used < writer->capacity ? writer->used :
                                                      writer->capacity - 1u;
        writer->output[end] = '\0';
    }
    return writer->used;
}

static const char *note_name(uint8_t note, char output[8])
{
    static const char *const pitch[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    (void)snprintf(output, 8u, "%s%d", pitch[note % 12u],
                   (int)note / 12 - 1);
    return output;
}

static size_t render_piano(char *output, size_t capacity, int columns,
                           const ktech_editor *editor)
{
    cell_writer writer = {output, capacity, 0u};
    const ktech_project *project = ktech_editor_project_const(editor);
    uint8_t scene = ktech_editor_scene(editor);
    uint8_t track_index = ktech_editor_track(editor);
    ktech_track scratch;
    const ktech_track *track = ktech_project_resolve_track(
        project, scene, track_index, &scratch);
    if (!track) return finish(&writer);
    uint8_t selected = ktech_editor_step(editor);
    unsigned visible = columns >= 100 ? 16u : columns >= 72 ? 12u : 8u;
    unsigned page = selected / visible * visible;
    uint8_t center = track->steps[selected].note;
    uint8_t low = center > 5u ? (uint8_t)(center - 5u) : 0u;
    if (low > 116u) low = 116u;
    append(&writer,
           "KILIX TECHNO | PIANO ROLL | SCENE %u TRACK %u %s | 1 grid 2 piano 3 arrange 4 mixer\n",
           (unsigned)scene + 1u, (unsigned)track_index + 1u,
           ktech_role_name((ktech_role)track->role));
    append(&writer, "NOTE  ");
    for (unsigned column = 0u; column < visible; ++column)
        append(&writer, " %02u", page + column + 1u);
    append(&writer, "\n");
    for (int note = (int)low + 11; note >= (int)low; --note) {
        char label[8];
        append(&writer, "%-5s ", note_name((uint8_t)note, label));
        for (unsigned column = 0u; column < visible; ++column) {
            unsigned step = page + column;
            char mark = ' ';
            if (step < track->length && track->steps[step].enabled &&
                track->steps[step].note == (uint8_t)note)
                mark = track->steps[step].ratchet > 1u ? '*' : 'X';
            if (step == selected && (uint8_t)note == center)
                append(&writer, "[%c]", mark == ' ' ? '@' : mark);
            else
                append(&writer, " %c ", mark);
        }
        append(&writer, "\n");
    }
    const ktech_step *step = &track->steps[selected];
    append(&writer,
           "EXPRESSION step %u: note %u | velocity %u | probability %u%% | gate %u%% | ratchet x%u\n",
           (unsigned)selected + 1u, step->note, step->velocity,
           step->probability, step->gate, step->ratchet);
    append(&writer,
           "Arrows choose step/field; +/- edits. Shift-drag selects a range; range painting applies one undo.\n");
    return finish(&writer);
}

static size_t render_arrangement(char *output, size_t capacity, int columns,
                                 const ktech_editor *editor)
{
    cell_writer writer = {output, capacity, 0u};
    const ktech_project *project = ktech_editor_project_const(editor);
    (void)columns;
    append(&writer,
           "KILIX TECHNO | ARRANGEMENT | %u entries / %u bars | Alt+A append | Alt+Enter launch\n",
           (unsigned)project->arrangement_count,
           (unsigned)ktech_project_arrangement_bars(project));
    append(&writer, "STATE   #   SCENE  BARS  TRACK PATTERNS (.-scene, number=linked)\n");
    size_t shown = project->arrangement_count < 20u ?
        project->arrangement_count : 20u;
    for (size_t index = 0u; index < shown; ++index) {
        const ktech_arrangement_entry *entry = &project->arrangement[index];
        append(&writer, "%c%c%c   %3u   %3u   %3u  ",
               index == project->arrangement_selected ? 'S' : '-',
               index == project->arrangement_queued ? 'Q' : '-',
               index == project->arrangement_audible ? 'A' : '-',
               (unsigned)index + 1u, (unsigned)entry->scene + 1u,
               (unsigned)entry->bars);
        for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track)
            if (entry->patterns[track] == KTECH_PATTERN_NONE)
                append(&writer, "  .");
            else
                append(&writer, " %2u", (unsigned)entry->patterns[track] + 1u);
        append(&writer, "\n");
    }
    if (shown < project->arrangement_count)
        append(&writer, "... %u more entries\n",
               (unsigned)(project->arrangement_count - shown));
    append(&writer, "PATTERN LIBRARY %u/%u\n",
           (unsigned)project->pattern_count, KTECH_PATTERN_CAPACITY);
    for (uint16_t index = 0u; index < project->pattern_count && index < 10u;
         ++index)
        append(&writer, "  #%u %-28s %s / %u steps\n",
               (unsigned)index + 1u, project->patterns[index].name,
               ktech_role_name((ktech_role)project->patterns[index].role),
               project->patterns[index].length);
    return finish(&writer);
}

static const char *effect_name(uint8_t kind)
{
    static const char *const names[KTECH_EFFECT_KIND_COUNT] = {
        "filter", "saturator", "tape delay", "room", "compressor", "bitcrush"
    };
    return kind < KTECH_EFFECT_KIND_COUNT ? names[kind] : "unknown";
}

static unsigned project_effect_count(const ktech_project *project)
{
    unsigned count = 0u;
    for (uint8_t bus = 0u; bus < project->bus_count; ++bus)
        count += project->buses[bus].effect_count;
    return count;
}

static size_t render_mixer(char *output, size_t capacity, int columns,
                           const ktech_editor *editor,
                           const ktech_audio_status *audio)
{
    cell_writer writer = {output, capacity, 0u};
    const ktech_project *project = ktech_editor_project_const(editor);
    (void)columns;
    append(&writer,
           "KILIX TECHNO | MIXER | %u buses / %u effects | pending:%s | 1 grid 2 piano 3 arrange 4 mixer\n",
           (unsigned)project->bus_count, project_effect_count(project),
           audio && audio->pending ? "yes" : "no");
    for (uint8_t bus = 0u; bus < project->bus_count; ++bus) {
        const ktech_bus *value = &project->buses[bus];
        append(&writer, "BUS %u %-15s level %3u%% pan %3u %c%c |",
               (unsigned)bus + 1u, value->name, value->level, value->pan,
               value->mute ? 'M' : '-', value->solo ? 'S' : '-');
        if (audio)
            append(&writer, " meter L%02u R%02u |",
                   (unsigned)audio->bus_peak_left[bus] * 99u / 32768u,
                   (unsigned)audio->bus_peak_right[bus] * 99u / 32768u);
        for (uint8_t effect = 0u; effect < value->effect_count; ++effect)
            append(&writer, " %s(%u%%)%s", effect_name(value->effects[effect].kind),
                   value->effects[effect].wet,
                   value->effects[effect].enabled ? "" : "[off]");
        append(&writer, "\n  tracks:");
        for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track)
            if (project->track_bus[track] == bus)
                append(&writer, " %u", (unsigned)track + 1u);
        append(&writer, "\n");
    }
    append(&writer, "MACROS %u/%u  FILL %u%%  VARIATION %u%%\n",
           (unsigned)project->macro_count, KTECH_MACRO_CAPACITY,
           (unsigned)project->performance_fill,
           (unsigned)project->performance_variation);
    for (uint8_t macro = 0u; macro < project->macro_count; ++macro)
        append(&writer, "  %u %-20s %3u%% -> %u semantic targets\n",
               (unsigned)macro + 1u, project->macros[macro].name,
               project->macros[macro].value,
               project->macros[macro].target_count);
    return finish(&writer);
}

static void format_field_value(char *output, size_t capacity,
                               ktech_edit_field field,
                               const ktech_project *project,
                               const ktech_track *track,
                               const ktech_step *step)
{
    switch (field) {
    case KTECH_FIELD_NOTE:
        (void)snprintf(output, capacity, "%u", step->note);
        break;
    case KTECH_FIELD_VELOCITY:
        (void)snprintf(output, capacity, "%u/127", step->velocity);
        break;
    case KTECH_FIELD_GATE:
        (void)snprintf(output, capacity, "%u%%", step->gate);
        break;
    case KTECH_FIELD_PROBABILITY:
        (void)snprintf(output, capacity, "%u%%", step->probability);
        break;
    case KTECH_FIELD_RATCHET:
        (void)snprintf(output, capacity, "x%u", step->ratchet);
        break;
    case KTECH_FIELD_INSTRUMENT:
        (void)snprintf(output, capacity, "%s",
                       ktech_role_name((ktech_role)track->role));
        break;
    case KTECH_FIELD_TRACK_LENGTH:
        (void)snprintf(output, capacity, "%u steps", track->length);
        break;
    case KTECH_FIELD_TRACK_LEVEL:
        (void)snprintf(output, capacity, "%u%%", track->level);
        break;
    case KTECH_FIELD_TRACK_PAN: {
        int offset = (int)track->pan - 128;
        if (offset > -3 && offset < 3)
            (void)snprintf(output, capacity, "center");
        else if (offset < 0)
            (void)snprintf(output, capacity, "L%d%%", (-offset * 100) / 128);
        else
            (void)snprintf(output, capacity, "R%d%%", (offset * 100) / 127);
        break;
    }
    case KTECH_FIELD_TEMPO:
        (void)snprintf(output, capacity, "%u BPM", project->tempo);
        break;
    case KTECH_FIELD_SWING:
        (void)snprintf(output, capacity, "%u%%", project->swing);
        break;
    case KTECH_FIELD_LAUNCH_QUANTIZATION:
        (void)snprintf(output, capacity, "%s",
                       launch_name(project->launch_quantization));
        break;
    case KTECH_FIELD_REDUCED_MOTION:
        (void)snprintf(output, capacity, "%s",
                       project->reduced_motion ? "on" : "off");
        break;
    case KTECH_FIELD_MASTER_LEVEL:
        (void)snprintf(output, capacity, "%u%%", project->master_level);
        break;
    case KTECH_FIELD_MASTER_TONE:
        (void)snprintf(output, capacity, "%u%%", project->master_tone);
        break;
    case KTECH_FIELD_MASTER_DRIVE:
        (void)snprintf(output, capacity, "%u%%", project->master_drive);
        break;
    case KTECH_FIELD_MASTER_DELAY:
        (void)snprintf(output, capacity, "%u%%", project->master_delay);
        break;
    default:
        (void)snprintf(output, capacity, "-");
        break;
    }
}

size_t ktech_cell_render(char *output, size_t capacity, int columns,
                         const ktech_editor *editor,
                         const ktech_audio_status *audio,
                         const char *message)
{
    cell_writer writer = {output, capacity, 0u};
    if (!editor) return 0u;
    if (columns < 40) columns = 40;
    ktech_editor_view view = ktech_editor_view_current(editor);
    if (view == KTECH_VIEW_PIANO_ROLL)
        return render_piano(output, capacity, columns, editor);
    if (view == KTECH_VIEW_ARRANGEMENT)
        return render_arrangement(output, capacity, columns, editor);
    if (view == KTECH_VIEW_MIXER)
        return render_mixer(output, capacity, columns, editor, audio);
    int visible = columns >= 100 ? 16 : columns >= 72 ? 12 : 8;
    uint8_t selected_track = ktech_editor_track(editor);
    uint8_t selected_step = ktech_editor_step(editor);
    uint8_t scene_index = ktech_editor_scene(editor);
    int page = selected_step / visible * visible;
    const ktech_project *project = ktech_editor_project_const(editor);
    ktech_scene resolved_scene;
    resolve_scene(project, scene_index, &resolved_scene);
    const ktech_scene *scene = &resolved_scene;
    const char *state = !audio ? "EDIT" : audio->stopped ? "STOP" :
                        audio->paused ? "PAUSE" : "PLAY";
    append(&writer, "KILIX TECHNO  [%s]  SCENE %02u/16  %u BPM  SWING %u%%%s\n",
           state, (unsigned)scene_index + 1u, project->tempo, project->swing,
           ktech_editor_dirty(editor) ? "  * UNSAVED" : "");
    if (project->arrangement_count > 0u)
        append(&writer,
               "ARRANGE %u entries / %u bars  SELECT %s%u  QUEUED %s%u  AUDIBLE %s%u\n",
               project->arrangement_count,
               ktech_project_arrangement_bars(project),
               project->arrangement_selected == UINT16_MAX ? "-" : "#",
               project->arrangement_selected == UINT16_MAX ? 0u :
                   (unsigned)project->arrangement_selected + 1u,
               project->arrangement_queued == UINT16_MAX ? "-" : "#",
               project->arrangement_queued == UINT16_MAX ? 0u :
                   (unsigned)project->arrangement_queued + 1u,
               project->arrangement_audible == UINT16_MAX ? "-" : "#",
               project->arrangement_audible == UINT16_MAX ? 0u :
                   (unsigned)project->arrangement_audible + 1u);
    int label_width = columns >= 100 ? 19 : columns >= 72 ? 14 : 11;
    append(&writer, "%-*s", label_width, "STEP");
    for (int column = 0; column < visible; ++column) {
        int step_number = page + column + 1;
        append(&writer, column % 4 == 0 ? "|%02d" : " %02d",
               step_number);
    }
    append(&writer, "\n");
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
        const ktech_track *source = &scene->tracks[track];
        int name_width = label_width - 6;
        append(&writer, "%c%u %-*.*s%c%c ",
               track == selected_track ? '>' : ' ', (unsigned)track + 1u,
               name_width, name_width,
               ktech_role_name((ktech_role)source->role),
               source->mute ? 'M' : '-', source->solo ? 'S' : '-');
        for (int column = 0; column < visible; ++column) {
            int step_index = page + column;
            char mark = ' ';
            if (step_index < source->length) {
                const ktech_step *current = &source->steps[step_index];
                mark = '.';
                if (current->enabled) {
                    mark = current->ratchet > 1u ? '*' :
                           current->probability < 100u ? '?' :
                           current->velocity < 64u ? 'o' : 'X';
                }
            }
            if (audio && audio->playhead.active &&
                step_index < source->length &&
                audio->playhead.rows_elapsed % source->length ==
                    (uint64_t)step_index)
                mark = '|';
            if (track == selected_track && step_index == selected_step)
                append(&writer, "[%c]", mark);
            else if (step_index >= 0 &&
                     ktech_editor_step_selected(editor, track,
                                                (uint8_t)step_index))
                append(&writer, "{%c}", mark);
            else
                append(&writer, " %c ", mark);
        }
        append(&writer, "\n");
    }
    const ktech_track *track = &scene->tracks[selected_track];
    const ktech_step *step = &track->steps[selected_step];
    ktech_edit_field field = ktech_editor_field(editor);
    char value[64];
    format_field_value(value, sizeof value, field, project, track, step);
    append(&writer, "EDIT %-14s = %s\n", field_name(field), value);
    append(&writer,
           "TRACK %u  %s  | STEP %02u/%02u %s | NOTE %u  VEL %u  GATE %u%%  CHANCE %u%%  x%u\n",
           (unsigned)selected_track + 1u,
           ktech_role_name((ktech_role)track->role),
           (unsigned)selected_step + 1u, track->length,
           step->enabled ? "ON " : "OFF", step->note, step->velocity,
           step->gate, step->probability, step->ratchet);
    uint8_t pattern = project->scene_patterns[scene_index][selected_track];
    if (pattern != KTECH_PATTERN_NONE && pattern < project->pattern_count)
        append(&writer, "PATTERN #%u %s  [linked]\n",
               (unsigned)pattern + 1u, project->patterns[pattern].name);
    append(&writer,
           "MIX len %u  level %u%%  pan %u | MASTER level %u%%  tone %u%%  drive %u%%  delay %u%%\n",
           track->length, track->level, track->pan, project->master_level,
           project->master_tone, project->master_drive,
           project->master_delay);
    append(&writer, "SCENES ");
    for (uint8_t scene_number = 0u; scene_number < KTECH_SCENE_COUNT;
         ++scene_number) {
        bool active = audio && audio->scene == scene_number;
        append(&writer, scene_number == scene_index ? "[%02u]" :
                        active ? "{%02u}" : " %02u ",
               (unsigned)scene_number + 1u);
    }
    append(&writer, "  launch:%s\n", launch_name(project->launch_quantization));
    append(&writer, "AUDIO %s%s | BANKS %u/%u %s | %u Hz / %u ms",
           audio && audio->backend ? audio->backend : "NO AUDIO",
           audio && audio->pending ? " | QUEUED" : "",
           audio ? audio->soundbank_count : 0u,
           KTECH_SAMPLE_BANK_COUNT,
           audio && audio->soundbank_count == KTECH_SAMPLE_BANK_COUNT ?
               "READY" : "AVAILABLE",
           audio ? audio->sample_rate : 0u,
           audio ? audio->latency_ms : 0u);
    if (audio && (audio->queue_failures || audio->transport.sink_failures))
        append(&writer, " | QERR %llu SINKERR %llu",
               (unsigned long long)audio->queue_failures,
               (unsigned long long)audio->transport.sink_failures);
    append(&writer, "\nMOUSE L-drag paint | R-click options | R-drag erase | wheel edit\n");
    append(&writer, "KEYS  Space play  ? help  Q quit\n");
    if (message && message[0]) append(&writer, "STATUS %s\n", message);
    if (ktech_editor_help_visible(editor)) {
        append(&writer,
               "HELP  1 grid/2 piano/3 song/4 mixer | Shift+arrows select | Ctrl+C/X/V clipboard | Ctrl+P/D pattern link/copy | Alt+A/Enter append/launch song | Alt+F/C/I/M tools | R-click options then / search, Space preview, F favorite | Ctrl+Z/Y undo/redo | Ctrl+S/E save/export\n");
    }
    if (capacity > 0u) {
        size_t end = writer.used < capacity ? writer.used : capacity - 1u;
        output[end] = '\0';
    }
    return writer.used;
}

static void cell_context_label(char *output, size_t capacity,
                               const ktech_editor *editor,
                               const ktech_ui_context_menu *menu,
                               uint8_t item)
{
    const ktech_project *project = ktech_editor_project_const(editor);
    const ktech_track *track = &project->scenes[ktech_editor_scene(editor)]
                                     .tracks[menu->track];
    if (menu->page == KTECH_UI_CONTEXT_ACTIONS) {
        const char *label = item == 0u ? "CHANGE INSTRUMENT  >" :
                            item == 1u ? (track->mute ? "UNMUTE TRACK" :
                                                       "MUTE TRACK") :
                            item == 2u ? (track->solo ? "UNSOLO TRACK" :
                                                       "SOLO TRACK") :
                                         "CLOSE";
        (void)snprintf(output, capacity, "%s", label);
    } else if (menu->page == KTECH_UI_CONTEXT_GROUPS) {
        if (item < KTECH_UI_INSTRUMENT_GROUP_COUNT)
            (void)snprintf(output, capacity, "%s  >",
                           ktech_ui_instrument_group_name(item));
        else
            (void)snprintf(output, capacity, "<  TRACK OPTIONS");
    } else if (menu->page == KTECH_UI_CONTEXT_ROLES ||
               menu->page == KTECH_UI_CONTEXT_SEARCH) {
        ktech_role role;
        if (ktech_ui_context_role(menu, item, &role)) {
            int bank = ktech_soundbank_role_bank_index(role);
            bool installed = bank < 0 ||
                (menu->installed_banks & (UINT32_C(1) << (unsigned)bank));
            (void)snprintf(output, capacity, "%s  %s [%s]%s",
                           track->role == (uint8_t)role ? "*" : " ",
                           ktech_role_name(role),
                           installed ? "installed" : "download",
                           ktech_editor_favorite(editor, role) ? " +" : "");
        }
        else
            (void)snprintf(output, capacity, "%s",
                           menu->page == KTECH_UI_CONTEXT_SEARCH ?
                           "<  CLEAR SEARCH" : "<  INSTRUMENT BANKS");
    } else {
        output[0] = '\0';
    }
}

size_t ktech_cell_render_context_menu(char *output, size_t capacity,
                                      int columns,
                                      const ktech_editor *editor,
                                      const ktech_ui_context_menu *menu)
{
    cell_writer writer = {output, capacity, 0u};
    if (!editor || !menu || menu->page == KTECH_UI_CONTEXT_CLOSED ||
        menu->track >= KTECH_TRACK_COUNT)
        return 0u;
    if (columns < 20) columns = 20;
    const ktech_project *project = ktech_editor_project_const(editor);
    const ktech_track *track = &project->scenes[ktech_editor_scene(editor)]
                                     .tracks[menu->track];
    const char *title = menu->page == KTECH_UI_CONTEXT_ACTIONS ?
        "TRACK OPTIONS" : menu->page == KTECH_UI_CONTEXT_GROUPS ?
        "INSTRUMENT BANKS" : menu->page == KTECH_UI_CONTEXT_SEARCH ?
        "INSTRUMENT SEARCH" : ktech_ui_instrument_group_name(menu->group);
    append(&writer, "KILIX TECHNO  |  %s  |  TRACK %u %s\n",
           title, (unsigned)menu->track + 1u,
           ktech_role_name((ktech_role)track->role));
    append(&writer, "Click or Up/Down + Enter. Space previews, F favorites. Esc closes.\n");
    if (menu->page == KTECH_UI_CONTEXT_SEARCH)
        append(&writer, "SEARCH /%s%s\n", menu->query,
               menu->query_length == 0u ? " (type a name)" : "");
    size_t count = ktech_ui_context_item_count(menu);
    int label_width = columns - 4;
    for (size_t index = 0u; index < count; ++index) {
        char label[96];
        cell_context_label(label, sizeof label, editor, menu,
                           (uint8_t)index);
        append(&writer, "%c %-*.*s\n",
               (uint8_t)index == menu->selected ? '>' : ' ',
               label_width, label_width, label);
    }
    if (capacity > 0u) {
        size_t end = writer.used < capacity ? writer.used : capacity - 1u;
        output[end] = '\0';
    }
    return writer.used;
}

bool ktech_cell_hit_context_menu(const ktech_ui_context_menu *menu,
                                 int x, int y, int columns,
                                 uint8_t *item)
{
    size_t count = ktech_ui_context_item_count(menu);
    int first_row = menu && menu->page == KTECH_UI_CONTEXT_SEARCH ? 3 : 2;
    if (!menu || menu->page == KTECH_UI_CONTEXT_CLOSED || x < 0 ||
        x >= columns || y < first_row ||
        (size_t)(y - first_row) >= count)
        return false;
    if (item) *item = (uint8_t)(y - first_row);
    return true;
}
