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
    append(&writer, "KILIX TECHNO | %s | %u BPM | SWING %u%% | SCENE %u%s\n",
           state, project->tempo, project->swing,
           (unsigned)scene_index + 1u,
           ktech_editor_dirty(editor) ? " | UNSAVED" : "");
    append(&writer, "             ");
    for (int column = 0; column < visible; ++column)
        append(&writer, "%02d ", page + column + 1);
    append(&writer, "\n");
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
        const ktech_track *source = &scene->tracks[track];
        append(&writer, "%c%u %-9.9s%c%c ",
               track == selected_track ? '>' : ' ', (unsigned)track + 1u,
               ktech_role_name((ktech_role)source->role),
               source->mute ? 'M' : '-', source->solo ? 'S' : '-');
        for (int column = 0; column < visible; ++column) {
            int step = page + column;
            char mark = step >= source->length ? ' ' :
                        source->steps[step].enabled ? 'x' : '.';
            if (audio && audio->playhead.active && step < source->length &&
                audio->playhead.rows_elapsed % source->length ==
                    (uint64_t)step)
                mark = '|';
            if (track == selected_track && step == selected_step)
                append(&writer, "[%c]", mark);
            else
                append(&writer, " %c ", mark);
        }
        append(&writer, "\n");
    }
    const ktech_track *track = &scene->tracks[selected_track];
    const ktech_step *step = &track->steps[selected_step];
    append(&writer,
           "FOCUS %s | step %u note %u vel %u gate %u prob %u ratchet %u | len %u level %u pan %u | launch %s motion %s\n",
           field_name(ktech_editor_field(editor)),
           (unsigned)selected_step + 1u, step->note, step->velocity,
           step->gate, step->probability, step->ratchet, track->length,
           track->level, track->pan,
           project->launch_quantization == KTECH_LAUNCH_STEP ? "step" :
           project->launch_quantization == KTECH_LAUNCH_BEAT ? "beat" :
                                                               "bar",
           project->reduced_motion ? "reduced" : "full");
    append(&writer, "SCENES ");
    for (uint8_t scene_number = 0u; scene_number < KTECH_SCENE_COUNT;
         ++scene_number) {
        append(&writer, scene_number == scene_index ? "[%u]" : " %u ",
               (unsigned)scene_number + 1u);
    }
    append(&writer, "\nAUDIO %s%s %uHz/%ums QERR%llu SINKERR%llu | arrows move, Enter toggle, +/- edit, F1 help, Q quit\n",
           audio && audio->backend ? audio->backend : "NO AUDIO",
           audio && audio->pending ? " (queued)" : "",
           audio ? audio->sample_rate : 0u,
           audio ? audio->latency_ms : 0u,
           (unsigned long long)(audio ? audio->queue_failures : 0u),
           (unsigned long long)(audio ? audio->transport.sink_failures : 0u));
    if (message && message[0]) append(&writer, "STATUS %s\n", message);
    if (ktech_editor_help_visible(editor)) {
        append(&writer,
               "HELP: Space play/pause; . stop; Ctrl+Space restart; T tap tempo; [ ] tracks; M/S mute/solo; Ctrl+Z/Y undo/redo; Ctrl+S save; Ctrl+E export/cancel; L launch scene.\n");
    }
    if (capacity > 0u) {
        size_t end = writer.used < capacity ? writer.used : capacity - 1u;
        output[end] = '\0';
    }
    return writer.used;
}
