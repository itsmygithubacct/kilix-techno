#include "kilix_techno/ui.h"

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
    int visible = columns >= 100 ? 16 : columns >= 72 ? 12 : 8;
    uint8_t selected_track = ktech_editor_track(editor);
    uint8_t selected_step = ktech_editor_step(editor);
    uint8_t scene_index = ktech_editor_scene(editor);
    int page = selected_step / visible * visible;
    const ktech_project *project = ktech_editor_project_const(editor);
    const ktech_scene *scene = &project->scenes[scene_index];
    const char *state = !audio ? "EDIT" : audio->stopped ? "STOP" :
                        audio->paused ? "PAUSE" : "PLAY";
    append(&writer, "KILIX TECHNO  [%s]  SCENE %02u/16  %u BPM  SWING %u%%%s\n",
           state, (unsigned)scene_index + 1u, project->tempo, project->swing,
           ktech_editor_dirty(editor) ? "  * UNSAVED" : "");
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
    append(&writer, "\nKEYS  arrows move/field  Enter toggle  +/- edit  Space play  ? help  Q quit\n");
    if (message && message[0]) append(&writer, "STATUS %s\n", message);
    if (ktech_editor_help_visible(editor)) {
        append(&writer,
               "HELP  Tab/[ ] track | M/S mute/solo | . stop | Ctrl+Space restart | T tap | ,/> scene | L launch | Ctrl+Z/Y undo/redo | Ctrl+S save | Ctrl+E export\n");
    }
    if (capacity > 0u) {
        size_t end = writer.used < capacity ? writer.used : capacity - 1u;
        output[end] = '\0';
    }
    return writer.used;
}
