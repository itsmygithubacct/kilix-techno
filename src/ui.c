#include "kilix_techno/ui.h"

#include <stdio.h>
#include <string.h>

enum {
    COLOR_BG = 0x081019,
    COLOR_PANEL = 0x101d2a,
    COLOR_GRID = 0x26384a,
    COLOR_TEXT = 0xdce9f2,
    COLOR_DIM = 0x7891a4,
    COLOR_CYAN = 0x2dd4bf,
    COLOR_MAGENTA = 0xf472b6,
    COLOR_YELLOW = 0xfacc15,
    COLOR_RED = 0xfb7185,
    COLOR_FOCUS = 0xffffff
};

static const char *field_name(ktech_edit_field field)
{
    static const char *const names[KTECH_FIELD_COUNT] = {
        "NOTE", "VELOCITY", "GATE", "PROBABILITY", "RATCHET",
        "INSTRUMENT", "LENGTH", "TRACK LEVEL", "TRACK PAN", "TEMPO",
        "SWING", "SCENE QUANTIZE", "REDUCED MOTION", "MASTER LEVEL",
        "MASTER TONE", "MASTER DRIVE", "MASTER DELAY"
    };
    return field < KTECH_FIELD_COUNT ? names[field] : "FIELD";
}

void ktech_ui_layout_make(ktech_ui_layout *layout, int width, int height,
                          const ktech_editor *editor)
{
    if (!layout) return;
    memset(layout, 0, sizeof *layout);
    layout->grid_x = width >= 720 ? 132 : 92;
    layout->grid_y = height >= 480 ? 70 : 56;
    layout->grid_width = width - layout->grid_x - 16;
    if (layout->grid_width < 160) layout->grid_width = 160;
    layout->columns = width >= 1000 ? 16 : width >= 720 ? 12 : 8;
    layout->cell_width = layout->grid_width / layout->columns;
    layout->track_rows = height >= 480 ? 8 : height >= 360 ? 6 : 4;
    if (layout->track_rows > (int)KTECH_TRACK_COUNT)
        layout->track_rows = KTECH_TRACK_COUNT;
    int selected_track = editor ? ktech_editor_track(editor) : 0;
    layout->track_start = selected_track / layout->track_rows *
                          layout->track_rows;
    if (layout->track_start + layout->track_rows > (int)KTECH_TRACK_COUNT)
        layout->track_start = (int)KTECH_TRACK_COUNT - layout->track_rows;
    layout->row_height = (height - (height >= 480 ? 220 : 150)) /
                         layout->track_rows;
    if (layout->row_height < 20) layout->row_height = 20;
    if (layout->row_height > 44) layout->row_height = 44;
    int selected_step = editor ? ktech_editor_step(editor) : 0;
    layout->page_start = selected_step / layout->columns * layout->columns;
    layout->scene_y = layout->grid_y +
                      layout->row_height * layout->track_rows + 8;
    layout->scene_height = height >= 480 ? 28 : 22;
}

static void draw_meter(sr_canvas *canvas, int x, int y, int width,
                       uint16_t value, uint32_t color)
{
    sr_fill_rect(canvas, (float)x, (float)y, (float)width, 7.0f,
                 COLOR_GRID, 1.0f);
    int fill = (int)((uint32_t)value * (uint32_t)width / 32768u);
    if (fill > width) fill = width;
    sr_fill_rect(canvas, (float)x, (float)y, (float)fill, 7.0f,
                 color, 1.0f);
}

void ktech_ui_render(sr_canvas *canvas, const ktech_editor *editor,
                     const ktech_audio_status *audio, const char *message)
{
    if (!canvas || !editor) return;
    const ktech_project *project = ktech_editor_project_const(editor);
    const uint8_t scene_index = ktech_editor_scene(editor);
    const ktech_scene *scene = &project->scenes[scene_index];
    ktech_ui_layout layout;
    ktech_ui_layout_make(&layout, canvas->w, canvas->h, editor);
    sr_clear(canvas, COLOR_BG);

    sr_fill_rect(canvas, 0.0f, 0.0f, (float)canvas->w, 54.0f,
                 COLOR_PANEL, 1.0f);
    sr_text(canvas, 16.0f, 10.0f, "KILIX TECHNO", COLOR_CYAN, 1.0f, 2);
    char transport[160];
    const char *state = !audio ? "EDIT" : audio->stopped ? "STOP" :
                        audio->paused ? "PAUSE" : "PLAY";
    (void)snprintf(transport, sizeof transport,
                   "%s  %u BPM  %u%% SWING  SCENE %02u",
                   state, project->tempo, project->swing,
                   (unsigned)scene_index + 1u);
    sr_text(canvas, 310.0f, 16.0f, transport, COLOR_TEXT, 1.0f, 1);
    if (ktech_editor_dirty(editor))
        sr_text(canvas, (float)canvas->w - 92.0f, 16.0f, "UNSAVED",
                COLOR_YELLOW, 1.0f, 1);

    uint8_t selected_track = ktech_editor_track(editor);
    uint8_t selected_step = ktech_editor_step(editor);
    uint64_t play_row = audio ? audio->playhead.rows_elapsed : 0u;
    for (int visible_track = 0; visible_track < layout.track_rows;
         ++visible_track) {
        uint8_t track = (uint8_t)(layout.track_start + visible_track);
        const ktech_track *source = &scene->tracks[track];
        int y = layout.grid_y + visible_track * layout.row_height;
        uint32_t label_color = track == selected_track ? COLOR_CYAN :
                                                        COLOR_TEXT;
        char label[32];
        (void)snprintf(label, sizeof label, "%u %-.10s%s%s",
                       (unsigned)track + 1u,
                       ktech_role_name((ktech_role)source->role),
                       source->mute ? " M" : "",
                       source->solo ? " S" : "");
        sr_text(canvas, 10.0f, (float)y + 5.0f, label,
                label_color, 1.0f, 1);
        for (int column = 0; column < layout.columns; ++column) {
            int step_index = layout.page_start + column;
            int x = layout.grid_x + column * layout.cell_width;
            uint32_t fill = COLOR_PANEL;
            bool valid = step_index < source->length;
            if (valid && source->steps[step_index].enabled)
                fill = track < 6u ? COLOR_MAGENTA : COLOR_CYAN;
            bool playhead = valid && audio && audio->playhead.active &&
                (play_row % source->length) == (uint64_t)step_index;
            if (playhead)
                fill = COLOR_YELLOW;
            sr_fill_rect(canvas, (float)x + 2.0f, (float)y + 2.0f,
                         (float)layout.cell_width - 4.0f,
                         (float)layout.row_height - 5.0f,
                         valid ? fill : COLOR_BG, valid ? 1.0f : 0.45f);
            if (playhead)
                sr_line(canvas, (float)x + 5.0f, (float)y + 5.0f,
                        (float)x + (float)layout.cell_width - 6.0f,
                        (float)y + 5.0f, 2.0f, COLOR_FOCUS, 1.0f, 0, 0);
            if (valid && track == selected_track &&
                step_index == selected_step) {
                sr_stroke_rect(canvas, (float)x + 1.0f, (float)y + 1.0f,
                               (float)layout.cell_width - 2.0f,
                               (float)layout.row_height - 3.0f, 2.0f,
                               COLOR_FOCUS, 1.0f);
                sr_line(canvas, (float)x + 4.0f, (float)y + 5.0f,
                        (float)x + 11.0f, (float)y + 12.0f, 2.0f,
                        COLOR_FOCUS, 1.0f, 0, 0);
            }
        }
    }

    for (uint8_t scene_number = 0u; scene_number < KTECH_SCENE_COUNT;
         ++scene_number) {
        int available = canvas->w - 32;
        int width = available / (int)KTECH_SCENE_COUNT;
        int x = 16 + (int)scene_number * width;
        uint32_t color = scene_number == scene_index ? COLOR_CYAN : COLOR_GRID;
        if (audio && scene_number == audio->scene) color = COLOR_YELLOW;
        sr_fill_rect(canvas, (float)x, (float)layout.scene_y,
                     (float)width - 3.0f, (float)layout.scene_height,
                     color, 1.0f);
        if (scene_number == scene_index)
            sr_stroke_rect(canvas, (float)x, (float)layout.scene_y,
                           (float)width - 3.0f,
                           (float)layout.scene_height, 2.0f,
                           COLOR_FOCUS, 1.0f);
        char number[4];
        (void)snprintf(number, sizeof number, "%u",
                       (unsigned)scene_number + 1u);
        sr_text_center(canvas, (float)x + ((float)width - 3.0f) * 0.5f,
                       (float)layout.scene_y + 6.0f, number,
                       scene_number == scene_index ? COLOR_BG : COLOR_TEXT,
                       1.0f, 1);
    }

    int panel_y = layout.scene_y + layout.scene_height + 12;
    int panel_height = canvas->h - panel_y - 38;
    if (panel_height < 0) panel_height = 0;
    sr_fill_rect(canvas, 10.0f, (float)panel_y,
                 (float)canvas->w - 20.0f, (float)panel_height,
                 COLOR_PANEL, 1.0f);
    const ktech_track *track = &scene->tracks[selected_track];
    const ktech_step *step = &track->steps[selected_step];
    char editor_line[256];
    (void)snprintf(editor_line, sizeof editor_line,
                   "%s  STEP %02u  NOTE %u  VEL %u  GATE %u  PROB %u  x%u  LEN %u  LVL %u  PAN %u  QNT %s  MOTION %s",
                   field_name(ktech_editor_field(editor)),
                   (unsigned)selected_step + 1u, step->note, step->velocity,
                   step->gate, step->probability, step->ratchet, track->length,
                   track->level, track->pan,
                   project->launch_quantization == KTECH_LAUNCH_STEP ?
                       "STEP" :
                   project->launch_quantization == KTECH_LAUNCH_BEAT ?
                       "BEAT" : "BAR",
                   project->reduced_motion ? "REDUCED" : "FULL");
    if (panel_height >= 20)
        sr_text(canvas, 22.0f, (float)panel_y + 8.0f, editor_line,
                COLOR_TEXT, 1.0f, 1);
    if (audio && panel_height >= 48) {
        draw_meter(canvas, 24, panel_y + 40, 120, audio->peak_left,
                   COLOR_CYAN);
        draw_meter(canvas, 150, panel_y + 40, 120, audio->peak_right,
                   COLOR_MAGENTA);
    }

    char status[256];
    const char *backend = audio && audio->backend ? audio->backend : "NO AUDIO";
    (void)snprintf(status, sizeof status,
                   "%s%s | %u Hz %u ms | QERR %llu SINKERR %llu | arrows move | Enter toggle | +/- edit | Space play | F1 help | Q quit",
                   backend, audio && audio->pending ? " | QUEUED" : "",
                   audio ? audio->sample_rate : 0u,
                   audio ? audio->latency_ms : 0u,
                   (unsigned long long)(audio ? audio->queue_failures : 0u),
                   (unsigned long long)(audio ?
                       audio->transport.sink_failures : 0u));
    sr_text(canvas, 12.0f, (float)canvas->h - 25.0f,
            message && message[0] ? message : status,
            message && message[0] ? COLOR_YELLOW : COLOR_DIM, 1.0f, 1);

    if (ktech_editor_help_visible(editor)) {
        int box_width = canvas->w > 760 ? 680 : canvas->w - 40;
        int box_height = 250;
        int x = (canvas->w - box_width) / 2;
        int y = (canvas->h - box_height) / 2;
        sr_fill_rect(canvas, (float)x, (float)y, (float)box_width,
                     (float)box_height, COLOR_BG, 0.96f);
        sr_stroke_rect(canvas, (float)x, (float)y, (float)box_width,
                       (float)box_height, 3.0f, COLOR_CYAN, 1.0f);
        sr_text(canvas, (float)x + 20.0f, (float)y + 18.0f,
                "HELP - EVERY CONTROL IS KEYBOARD REACHABLE",
                COLOR_CYAN, 1.0f, 1);
        const char *lines[] = {
            "ARROWS: step / parameter     TAB [ ]: tracks",
            "ENTER: toggle                - +: change value",
            "SPACE: play/pause            . / CTRL+SPACE: stop / restart",
            "T: tap tempo                 CTRL+Z/Y: undo / redo",
            "M / S: mute / solo           U / R: undo / redo",
            ", / >: choose scene          L: launch scene",
            "CTRL+S: save                 CTRL+E: export / cancel",
            "F1 / ?: close help           Q: guarded quit"
        };
        for (size_t index = 0u; index < sizeof lines / sizeof lines[0]; ++index)
            sr_text(canvas, (float)x + 20.0f,
                    (float)y + 52.0f + (float)index * 25.0f,
                    lines[index], COLOR_TEXT, 1.0f, 1);
    }
}

bool ktech_ui_hit_step(const ktech_ui_layout *layout, int x, int y,
                       uint8_t *track, uint8_t *step)
{
    if (!layout || x < layout->grid_x ||
        x >= layout->grid_x + layout->cell_width * layout->columns ||
        y < layout->grid_y ||
        y >= layout->grid_y + layout->row_height * layout->track_rows)
        return false;
    int found_track = layout->track_start +
                      (y - layout->grid_y) / layout->row_height;
    int found_step = layout->page_start +
                     (x - layout->grid_x) / layout->cell_width;
    if (found_track < 0 || found_track >= (int)KTECH_TRACK_COUNT ||
        found_step < 0 || found_step >= (int)KTECH_STEP_COUNT)
        return false;
    if (track) *track = (uint8_t)found_track;
    if (step) *step = (uint8_t)found_step;
    return true;
}
