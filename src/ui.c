#include "kilix_techno/ui.h"

#include "kilix_techno/soundbank.h"

#include <stdio.h>
#include <string.h>

enum {
    COLOR_BG = 0x071018,
    COLOR_PANEL = 0x0e1b27,
    COLOR_PANEL_RAISED = 0x142536,
    COLOR_GRID = 0x203245,
    COLOR_GRID_BEAT = 0x2a4056,
    COLOR_TEXT = 0xe5f1f7,
    COLOR_DIM = 0x7893a8,
    COLOR_CYAN = 0x35e0c1,
    COLOR_MAGENTA = 0xf472b6,
    COLOR_YELLOW = 0xfacc15,
    COLOR_RED = 0xfb7185,
    COLOR_GREEN = 0x4ade80,
    COLOR_ORANGE = 0xfb923c,
    COLOR_PURPLE = 0xa78bfa,
    COLOR_FOCUS = 0xffffff
};

#define UI_FONT SR_FONT_COMPACT_7X14

typedef struct instrument_group {
    const char *name;
    uint8_t first_role;
    uint8_t role_count;
} instrument_group;

static const instrument_group instrument_groups[] = {
    {"KILIX NATIVE", KTECH_ROLE_KICK, KTECH_NATIVE_ROLE_COUNT},
    {"TIDALCYCLES TR-808", KTECH_ROLE_TIDAL_KICK, 6u},
    {"STARGATE", KTECH_ROLE_STARGATE_KICK, 6u},
    {"SCOREDATA-FORGE", KTECH_ROLE_FORGE_GROWL, 2u},
    {"MECHSOUNDS", KTECH_ROLE_MECH_PLUCK_BASS, 6u},
    {"KARORYFER", KTECH_ROLE_KARORYFER_A2, 4u},
    {"VCSL ELECTRONIC", KTECH_ROLE_VCSL_CLAVISYNTH, 4u},
    {"ONGENET KIT", KTECH_ROLE_ONGENET_KICK, 6u},
    {"VCSL ACOUSTIC SFZ", KTECH_ROLE_VCSL_ACOUSTIC_KICK, 4u},
    {"VSCO 2 CE SFZ", KTECH_ROLE_VSCO_VIOLIN_PIZZ, 4u},
    {"CHAOSBANK SF2", KTECH_ROLE_SF2_CHAOS_PIANO, 1u},
    {"JNS-GM 2 SF2", KTECH_ROLE_SF2_JNS_SYNTH_BASS, 1u},
    {"MASTERPIECE SF2", KTECH_ROLE_SF2_MASTERPIECE_PAD, 1u},
    {"UNISON SF2", KTECH_ROLE_SF2_UNISON_LEAD, 1u}
};

typedef struct context_geometry {
    int x;
    int y;
    int width;
    int height;
    int header_height;
    int row_height;
    int item_count;
} context_geometry;

static void draw_text(sr_canvas *canvas, int x, int y, const char *text,
                      uint32_t color)
{
    sr_text_in(UI_FONT, canvas, (float)x, (float)y, text, color, 1.0f, 1);
}

static void draw_text_center(sr_canvas *canvas, int x, int y,
                             const char *text, uint32_t color)
{
    sr_text_center_in(UI_FONT, canvas, (float)x, (float)y, text,
                      color, 1.0f, 1);
}

static int text_width(const char *text)
{
    return sr_text_width_in(UI_FONT, text, 1);
}

static int draw_chip(sr_canvas *canvas, int x, int y, const char *text,
                     uint32_t color, bool solid)
{
    int width = text_width(text) + 16;
    sr_fill_rect(canvas, (float)x, (float)y, (float)width, 26.0f,
                 solid ? color : COLOR_PANEL_RAISED, 1.0f);
    if (!solid)
        sr_stroke_rect(canvas, (float)x, (float)y, (float)width, 26.0f,
                       1.0f, color, 0.75f);
    draw_text(canvas, x + 8, y + 6, text, solid ? COLOR_BG : color);
    return width;
}

static uint32_t role_color(ktech_role role)
{
    switch (ktech_role_fallback(role)) {
    case KTECH_ROLE_KICK: return COLOR_ORANGE;
    case KTECH_ROLE_SNARE:
    case KTECH_ROLE_CLAP: return COLOR_MAGENTA;
    case KTECH_ROLE_CLOSED_HAT:
    case KTECH_ROLE_OPEN_HAT:
    case KTECH_ROLE_PERCUSSION: return COLOR_YELLOW;
    case KTECH_ROLE_BASS: return COLOR_CYAN;
    case KTECH_ROLE_CHORD: return COLOR_PURPLE;
    case KTECH_ROLE_TEXTURE: return COLOR_GREEN;
    default: return COLOR_TEXT;
    }
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

static const char *launch_name(uint8_t launch)
{
    return launch == KTECH_LAUNCH_STEP ? "STEP" :
           launch == KTECH_LAUNCH_BEAT ? "BEAT" : "BAR";
}

static void format_pan(char *output, size_t capacity, uint8_t pan)
{
    int offset = (int)pan - 128;
    if (offset > -3 && offset < 3)
        (void)snprintf(output, capacity, "CENTER");
    else if (offset < 0)
        (void)snprintf(output, capacity, "L %d%%", (-offset * 100) / 128);
    else
        (void)snprintf(output, capacity, "R %d%%", (offset * 100) / 127);
}

static void field_value(char *output, size_t capacity,
                        ktech_edit_field field, const ktech_project *project,
                        const ktech_track *track, const ktech_step *step)
{
    switch (field) {
    case KTECH_FIELD_NOTE:
        (void)snprintf(output, capacity, "%u", step->note);
        break;
    case KTECH_FIELD_VELOCITY:
        (void)snprintf(output, capacity, "%u / 127", step->velocity);
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
        (void)snprintf(output, capacity, "%u STEPS", track->length);
        break;
    case KTECH_FIELD_TRACK_LEVEL:
        (void)snprintf(output, capacity, "%u%%", track->level);
        break;
    case KTECH_FIELD_TRACK_PAN:
        format_pan(output, capacity, track->pan);
        break;
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
                       project->reduced_motion ? "ON" : "OFF");
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

static uint8_t field_percent(ktech_edit_field field,
                             const ktech_project *project,
                             const ktech_track *track,
                             const ktech_step *step)
{
    switch (field) {
    case KTECH_FIELD_NOTE: return (uint8_t)((unsigned)step->note * 100u / 127u);
    case KTECH_FIELD_VELOCITY:
        return (uint8_t)((unsigned)step->velocity * 100u / 127u);
    case KTECH_FIELD_GATE: return step->gate;
    case KTECH_FIELD_PROBABILITY: return step->probability;
    case KTECH_FIELD_RATCHET:
        return (uint8_t)(((unsigned)step->ratchet - 1u) * 100u / 3u);
    case KTECH_FIELD_INSTRUMENT:
        return (uint8_t)((unsigned)track->role * 100u /
                         (KTECH_ROLE_COUNT - 1u));
    case KTECH_FIELD_TRACK_LENGTH:
        return (uint8_t)((unsigned)track->length * 100u / KTECH_STEP_COUNT);
    case KTECH_FIELD_TRACK_LEVEL: return track->level;
    case KTECH_FIELD_TRACK_PAN:
        return (uint8_t)((unsigned)track->pan * 100u / 255u);
    case KTECH_FIELD_TEMPO:
        return (uint8_t)(((unsigned)project->tempo - 20u) * 100u / 235u);
    case KTECH_FIELD_SWING:
        return (uint8_t)(((unsigned)project->swing - 50u) * 4u);
    case KTECH_FIELD_LAUNCH_QUANTIZATION:
        return (uint8_t)((unsigned)project->launch_quantization * 50u);
    case KTECH_FIELD_REDUCED_MOTION:
        return project->reduced_motion ? 100u : 0u;
    case KTECH_FIELD_MASTER_LEVEL: return project->master_level;
    case KTECH_FIELD_MASTER_TONE: return project->master_tone;
    case KTECH_FIELD_MASTER_DRIVE: return project->master_drive;
    case KTECH_FIELD_MASTER_DELAY: return project->master_delay;
    default: return 0u;
    }
}

void ktech_ui_layout_make(ktech_ui_layout *layout, int width, int height,
                          const ktech_editor *editor)
{
    if (!layout) return;
    memset(layout, 0, sizeof *layout);
    layout->grid_x = width >= 720 ? 154 : width >= 480 ? 118 : 76;
    layout->grid_y = height >= 480 ? 86 : height >= 300 ? 74 : 58;
    layout->grid_width = width - layout->grid_x - 14;
    if (layout->grid_width < 160) layout->grid_width = 160;
    layout->columns = width >= 960 ? 16 : width >= 680 ? 12 : 8;
    layout->cell_width = layout->grid_width / layout->columns;
    layout->track_rows = height >= 480 ? 8 : height >= 360 ? 6 : 4;
    if (layout->track_rows > (int)KTECH_TRACK_COUNT)
        layout->track_rows = KTECH_TRACK_COUNT;
    int selected_track = editor ? ktech_editor_track(editor) : 0;
    layout->track_start = selected_track / layout->track_rows *
                          layout->track_rows;
    if (layout->track_start + layout->track_rows > (int)KTECH_TRACK_COUNT)
        layout->track_start = (int)KTECH_TRACK_COUNT - layout->track_rows;
    int reserved = height >= 700 ? 300 :
                   height >= 480 ? 260 : height >= 360 ? 220 : 160;
    layout->row_height = (height - reserved) /
                         layout->track_rows;
    if (layout->row_height < 20) layout->row_height = 20;
    if (layout->row_height > 90) layout->row_height = 90;
    int selected_step = editor ? ktech_editor_step(editor) : 0;
    layout->page_start = selected_step / layout->columns * layout->columns;
    layout->scene_x = width >= 480 ? layout->grid_x : 8;
    layout->scene_width = width >= 480 ? layout->grid_width : width - 16;
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

static bool render_alternate_view(sr_canvas *canvas,
                                  const ktech_editor *editor,
                                  const ktech_audio_status *audio)
{
    ktech_editor_view view = ktech_editor_view_current(editor);
    if (view == KTECH_VIEW_STEPS) return false;
    const ktech_project *project = ktech_editor_project_const(editor);
    sr_fill_rect(canvas, 0.0f, 0.0f, (float)canvas->w, 52.0f,
                 COLOR_PANEL, 1.0f);
    const char *title = view == KTECH_VIEW_PIANO_ROLL ? "PIANO ROLL" :
                        view == KTECH_VIEW_ARRANGEMENT ? "ARRANGEMENT" :
                                                        "MIXER";
    sr_text_shadow(canvas, 14.0f, 10.0f, "KILIX TECHNO",
                   COLOR_CYAN, 1.0f, 2);
    draw_text(canvas, canvas->w >= 500 ? 230 : 140, 19, title,
              COLOR_MAGENTA);
    draw_text(canvas, canvas->w - 230, 19,
              "1 GRID  2 PIANO  3 ARRANGE  4 MIXER", COLOR_DIM);
    if (view == KTECH_VIEW_PIANO_ROLL) {
        uint8_t scene = ktech_editor_scene(editor);
        uint8_t track_index = ktech_editor_track(editor);
        ktech_track scratch;
        const ktech_track *track = ktech_project_resolve_track(
            project, scene, track_index, &scratch);
        if (!track) return true;
        uint8_t selected = ktech_editor_step(editor);
        uint8_t center = track->steps[selected].note;
        uint8_t low = center > 5u ? (uint8_t)(center - 5u) : 0u;
        if (low > 116u) low = 116u;
        int columns = canvas->w >= 900 ? 16 : 8;
        int page = selected / columns * columns;
        int left = 72;
        int top = 74;
        int cell_width = (canvas->w - left - 18) / columns;
        int cell_height = (canvas->h - top - 86) / 12;
        if (cell_height < 18) cell_height = 18;
        char info[160];
        (void)snprintf(info, sizeof info, "SCENE %u  TRACK %u  %s",
                       (unsigned)scene + 1u, (unsigned)track_index + 1u,
                       ktech_role_name((ktech_role)track->role));
        draw_text(canvas, 14, 56, info, COLOR_TEXT);
        for (int row = 0; row < 12; ++row) {
            uint8_t note = (uint8_t)((int)low + 11 - row);
            char label[8];
            static const char *const names[] = {
                "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
            };
            (void)snprintf(label, sizeof label, "%s%d", names[note % 12u],
                           (int)note / 12 - 1);
            draw_text(canvas, 14, top + row * cell_height + 3, label,
                      note % 12u == 0u ? COLOR_TEXT : COLOR_DIM);
            for (int column = 0; column < columns; ++column) {
                int step = page + column;
                int x = left + column * cell_width;
                int y = top + row * cell_height;
                bool black = note % 12u == 1u || note % 12u == 3u ||
                             note % 12u == 6u || note % 12u == 8u ||
                             note % 12u == 10u;
                sr_fill_rect(canvas, (float)x + 1.0f, (float)y + 1.0f,
                             (float)cell_width - 2.0f,
                             (float)cell_height - 2.0f,
                             black ? COLOR_GRID : COLOR_PANEL_RAISED,
                             black ? 0.55f : 0.8f);
                if (step < track->length && track->steps[step].enabled &&
                    track->steps[step].note == note) {
                    float height = (float)(cell_height - 4) *
                                   track->steps[step].velocity / 127.0f;
                    sr_fill_rect(canvas, (float)x + 3.0f,
                                 (float)y + (float)cell_height - 2.0f - height,
                                 (float)cell_width - 6.0f, height,
                                 role_color((ktech_role)track->role), 0.9f);
                }
                if (step == selected && note == center)
                    sr_stroke_rect(canvas, (float)x, (float)y,
                                   (float)cell_width, (float)cell_height,
                                   2.0f, COLOR_FOCUS, 1.0f);
            }
        }
        const ktech_step *step = &track->steps[selected];
        (void)snprintf(info, sizeof info,
                       "STEP %u | NOTE %u | VEL %u | PROB %u%% | GATE %u%% | x%u",
                       (unsigned)selected + 1u, step->note, step->velocity,
                       step->probability, step->gate, step->ratchet);
        draw_text(canvas, 14, canvas->h - 30, info, COLOR_TEXT);
    } else if (view == KTECH_VIEW_ARRANGEMENT) {
        char line[256];
        (void)snprintf(line, sizeof line, "%u ENTRIES / %u BARS",
                       (unsigned)project->arrangement_count,
                       (unsigned)ktech_project_arrangement_bars(project));
        draw_text(canvas, 18, 66, line, COLOR_TEXT);
        int y = 92;
        for (uint16_t index = 0u; index < project->arrangement_count &&
             y < canvas->h - 24; ++index, y += 24) {
            const ktech_arrangement_entry *entry =
                &project->arrangement[index];
            (void)snprintf(line, sizeof line,
                           "%c%c%c  #%03u  SCENE %02u  %3u BARS",
                           index == project->arrangement_selected ? 'S' : '-',
                           index == project->arrangement_queued ? 'Q' : '-',
                           index == project->arrangement_audible ? 'A' : '-',
                           (unsigned)index + 1u,
                           (unsigned)entry->scene + 1u,
                           (unsigned)entry->bars);
            sr_fill_rect(canvas, 14.0f, (float)y - 4.0f,
                         (float)canvas->w - 28.0f, 21.0f,
                         index % 2u ? COLOR_GRID : COLOR_PANEL,
                         index % 2u ? 0.4f : 0.8f);
            draw_text(canvas, 22, y, line,
                      index == project->arrangement_audible ? COLOR_GREEN :
                      index == project->arrangement_queued ? COLOR_YELLOW :
                                                             COLOR_TEXT);
        }
    } else {
        char line[256];
        int y = 68;
        for (uint8_t bus = 0u; bus < project->bus_count; ++bus, y += 70) {
            const ktech_bus *value = &project->buses[bus];
            (void)snprintf(line, sizeof line,
                           "BUS %u  %s  LEVEL %u%%  PAN %u  %c%c  FX %u",
                           (unsigned)bus + 1u, value->name,
                           (unsigned)value->level, (unsigned)value->pan,
                           value->mute ? 'M' : '-', value->solo ? 'S' : '-',
                           (unsigned)value->effect_count);
            draw_text(canvas, 18, y, line, COLOR_TEXT);
            sr_fill_rect(canvas, 18.0f, (float)y + 22.0f, 260.0f, 8.0f,
                         COLOR_GRID, 1.0f);
            sr_fill_rect(canvas, 18.0f, (float)y + 22.0f,
                         2.6f * value->level, 8.0f,
                         bus == 0u ? COLOR_CYAN : COLOR_MAGENTA, 1.0f);
            if (audio) {
                draw_meter(canvas, 300, y + 22, 130,
                           audio->bus_peak_left[bus], COLOR_CYAN);
                draw_meter(canvas, 444, y + 22, 130,
                           audio->bus_peak_right[bus], COLOR_MAGENTA);
            }
        }
        (void)snprintf(line, sizeof line,
                       "MACROS %u/%u | FILL %u%% | VARIATION %u%% | PENDING %s",
                       (unsigned)project->macro_count, KTECH_MACRO_CAPACITY,
                       (unsigned)project->performance_fill,
                       (unsigned)project->performance_variation,
                       audio && audio->pending ? "YES" : "NO");
        draw_text(canvas, 18, canvas->h - 30, line, COLOR_DIM);
    }
    return true;
}

void ktech_ui_render(sr_canvas *canvas, const ktech_editor *editor,
                     const ktech_audio_status *audio, const char *message)
{
    if (!canvas || !editor) return;
    const ktech_project *project = ktech_editor_project_const(editor);
    const uint8_t scene_index = ktech_editor_scene(editor);
    ktech_scene resolved_scene;
    resolve_scene(project, scene_index, &resolved_scene);
    const ktech_scene *scene = &resolved_scene;
    ktech_ui_layout layout;
    ktech_ui_layout_make(&layout, canvas->w, canvas->h, editor);
    sr_clear(canvas, COLOR_BG);
    if (render_alternate_view(canvas, editor, audio)) return;

    const char *state = !audio ? "EDIT" : audio->stopped ? "STOP" :
                        audio->paused ? "PAUSE" : "PLAY";
    uint32_t state_color = strcmp(state, "PLAY") == 0 ? COLOR_GREEN :
                           strcmp(state, "PAUSE") == 0 ? COLOR_YELLOW :
                           strcmp(state, "STOP") == 0 ? COLOR_RED :
                                                         COLOR_CYAN;
    sr_fill_rect(canvas, 0.0f, 0.0f, (float)canvas->w, 50.0f,
                 COLOR_PANEL, 1.0f);
    sr_fill_rect(canvas, 0.0f, 0.0f, 4.0f, 50.0f,
                 state_color, 1.0f);
    int header_x;
    if (canvas->w >= 820) {
        sr_text_shadow(canvas, 14.0f, 9.0f, "KILIX TECHNO",
                       COLOR_CYAN, 1.0f, 2);
        header_x = 220;
    } else {
        draw_text(canvas, 12, 18, "KILIX TECHNO", COLOR_CYAN);
        header_x = 116;
    }
    header_x += draw_chip(canvas, header_x, 12, state, state_color, true) + 6;
    char chip[32];
    (void)snprintf(chip, sizeof chip, "%u BPM", project->tempo);
    if (header_x + text_width(chip) + 22 < canvas->w) {
        header_x += draw_chip(canvas, header_x, 12, chip,
                              COLOR_TEXT, false) + 6;
    }
    if (canvas->w >= 680) {
        (void)snprintf(chip, sizeof chip, "SWING %u%%", project->swing);
        header_x += draw_chip(canvas, header_x, 12, chip,
                              COLOR_PURPLE, false) + 6;
    }
    (void)snprintf(chip, sizeof chip, "SCENE %02u",
                   (unsigned)scene_index + 1u);
    if (header_x + text_width(chip) + 22 < canvas->w) {
        header_x += draw_chip(canvas, header_x, 12, chip,
                              COLOR_CYAN, false) + 6;
    }
    if (canvas->w >= 760 && audio) {
        (void)snprintf(chip, sizeof chip, "BANKS %u/%u",
                       audio->soundbank_count, KTECH_SAMPLE_BANK_COUNT);
        if (header_x + text_width(chip) + 22 < canvas->w)
            header_x += draw_chip(canvas, header_x, 12, chip,
                                  audio->soundbank_count ==
                                      KTECH_SAMPLE_BANK_COUNT ?
                                      COLOR_GREEN : COLOR_YELLOW,
                                  false) + 6;
    }
    if (project->arrangement_count > 0u && canvas->w >= 920) {
        (void)snprintf(chip, sizeof chip, "ARR %uB S%s Q%s A%s",
                       ktech_project_arrangement_bars(project),
                       project->arrangement_selected == UINT16_MAX ? "-" : "+",
                       project->arrangement_queued == UINT16_MAX ? "-" : "+",
                       project->arrangement_audible == UINT16_MAX ? "-" : "+");
        if (header_x + text_width(chip) + 22 < canvas->w)
            header_x += draw_chip(canvas, header_x, 12, chip,
                                  COLOR_MAGENTA, false) + 6;
    }
    if (ktech_editor_dirty(editor)) {
        if (header_x + text_width("UNSAVED") + 22 < canvas->w)
            (void)draw_chip(canvas, header_x, 12, "UNSAVED",
                            COLOR_YELLOW, false);
        else
            sr_fill_circle(canvas, (float)canvas->w - 12.0f, 25.0f,
                           4.0f, COLOR_YELLOW, 1.0f);
    }

    uint8_t selected_track = ktech_editor_track(editor);
    uint8_t selected_step = ktech_editor_step(editor);
    uint64_t play_row = audio ? audio->playhead.rows_elapsed : 0u;
    int step_header_y = layout.grid_y - 18;
    if (step_header_y >= 52) {
        draw_text(canvas, 10, step_header_y, "TRACK", COLOR_DIM);
        for (int column = 0; column < layout.columns; ++column) {
            int step_index = layout.page_start + column;
            int x = layout.grid_x + column * layout.cell_width;
            char number[16];
            (void)snprintf(number, sizeof number, "%02d", step_index + 1);
            draw_text_center(canvas, x + layout.cell_width / 2,
                             step_header_y, number,
                             column % 4 == 0 ? COLOR_TEXT : COLOR_DIM);
            if (column % 4 == 0)
                sr_fill_rect(canvas, (float)x + 2.0f,
                             (float)layout.grid_y - 3.0f,
                             (float)layout.cell_width - 4.0f, 2.0f,
                             COLOR_CYAN, 0.55f);
        }
    }
    for (int visible_track = 0; visible_track < layout.track_rows;
         ++visible_track) {
        uint8_t track = (uint8_t)(layout.track_start + visible_track);
        const ktech_track *source = &scene->tracks[track];
        int y = layout.grid_y + visible_track * layout.row_height;
        uint32_t accent = role_color((ktech_role)source->role);
        if (track == selected_track)
            sr_fill_rect(canvas, 6.0f, (float)y + 1.0f,
                         (float)canvas->w - 12.0f,
                         (float)layout.row_height - 2.0f,
                         COLOR_PANEL_RAISED, 0.62f);
        sr_fill_rect(canvas, 10.0f, (float)y + 3.0f, 20.0f,
                     (float)layout.row_height - 6.0f,
                     track == selected_track ? accent : COLOR_GRID, 1.0f);
        char number[4];
        (void)snprintf(number, sizeof number, "%u", (unsigned)track + 1u);
        draw_text_center(canvas, 20, y + (layout.row_height - 14) / 2,
                         number, track == selected_track ? COLOR_BG :
                                                          COLOR_TEXT);
        int label_characters = (layout.grid_x - 42) / 8;
        if (source->mute || source->solo) --label_characters;
        if (label_characters < 3) label_characters = 3;
        if (label_characters > 13) label_characters = 13;
        char label[32];
        (void)snprintf(label, sizeof label, "%.*s%s",
                       label_characters,
                       ktech_role_name((ktech_role)source->role),
                       source->solo ? " S" : source->mute ? " M" : "");
        draw_text(canvas, 36, y + 4, label,
                  source->mute ? COLOR_RED :
                  source->solo ? COLOR_YELLOW :
                  track == selected_track ? COLOR_TEXT : COLOR_DIM);
        if (layout.row_height >= 27) {
            int level_width = layout.grid_x - 46;
            sr_fill_rect(canvas, 36.0f,
                         (float)y + (float)layout.row_height - 6.0f,
                         (float)level_width, 2.0f, COLOR_GRID, 1.0f);
            sr_fill_rect(canvas, 36.0f,
                         (float)y + (float)layout.row_height - 6.0f,
                         (float)((unsigned)level_width * source->level / 100u),
                         2.0f, accent, source->mute ? 0.3f : 0.8f);
        }
        for (int column = 0; column < layout.columns; ++column) {
            int step_index = layout.page_start + column;
            int x = layout.grid_x + column * layout.cell_width;
            bool valid = step_index < source->length;
            const ktech_step *current = valid ? &source->steps[step_index] :
                                                NULL;
            uint32_t base = column % 4 == 0 ? COLOR_GRID_BEAT : COLOR_GRID;
            int cell_x = x + 3;
            int cell_y = y + 3;
            int cell_width = layout.cell_width - 5;
            int cell_height = layout.row_height - 6;
            sr_fill_rect(canvas, (float)cell_x, (float)cell_y,
                         (float)cell_width, (float)cell_height,
                         valid ? base : COLOR_BG, valid ? 0.72f : 0.35f);
            if (current && current->enabled) {
                int velocity_height = 3 +
                    (int)((unsigned)(cell_height - 3) * current->velocity /
                          127u);
                sr_fill_rect(canvas, (float)cell_x,
                             (float)(cell_y + cell_height - velocity_height),
                             (float)cell_width, (float)velocity_height,
                             accent, source->mute ? 0.28f : 0.88f);
                int chance_width =
                    (int)((unsigned)cell_width * current->probability / 100u);
                sr_fill_rect(canvas, (float)cell_x, (float)cell_y,
                             (float)chance_width, 2.0f, COLOR_FOCUS, 0.7f);
                for (uint8_t ratchet = 1u; ratchet < current->ratchet;
                     ++ratchet) {
                    float dot_x = (float)cell_x + 3.0f +
                                  (float)(ratchet - 1u) * 5.0f;
                    if (dot_x < (float)(cell_x + cell_width - 2))
                        sr_fill_circle(canvas, dot_x,
                                       (float)(cell_y + cell_height - 3),
                                       1.25f, COLOR_BG, 0.9f);
                }
            }
            bool playhead = valid && audio && audio->playhead.active &&
                (play_row % source->length) == (uint64_t)step_index;
            if (playhead)
                sr_stroke_rect(canvas, (float)cell_x - 1.0f,
                               (float)cell_y - 1.0f,
                               (float)cell_width + 2.0f,
                               (float)cell_height + 2.0f, 2.0f,
                               COLOR_YELLOW, 1.0f);
            if (valid && ktech_editor_step_selected(
                             editor, track, (uint8_t)step_index))
                sr_stroke_rect(canvas, (float)cell_x,
                               (float)cell_y,
                               (float)cell_width,
                               (float)cell_height, 1.0f,
                               COLOR_MAGENTA, 1.0f);
            if (valid && track == selected_track &&
                step_index == selected_step) {
                sr_stroke_rect(canvas, (float)cell_x - 2.0f,
                               (float)cell_y - 2.0f,
                               (float)cell_width + 4.0f,
                               (float)cell_height + 4.0f, 2.0f,
                               COLOR_FOCUS, 1.0f);
                sr_fill_triangle(canvas, (float)cell_x, (float)cell_y,
                                 (float)cell_x + 7.0f, (float)cell_y,
                                 (float)cell_x, (float)cell_y + 7.0f,
                                 COLOR_FOCUS, 1.0f);
            }
        }
    }

    if (canvas->w >= 480)
        draw_text(canvas, 10, layout.scene_y +
                  (layout.scene_height - 14) / 2, "SCENES", COLOR_DIM);
    for (uint8_t scene_number = 0u; scene_number < KTECH_SCENE_COUNT;
         ++scene_number) {
        int width = layout.scene_width / (int)KTECH_SCENE_COUNT;
        int x = layout.scene_x + (int)scene_number * width;
        bool selected = scene_number == scene_index;
        bool active = audio && scene_number == audio->scene;
        uint32_t fill = selected ? COLOR_CYAN : COLOR_PANEL_RAISED;
        sr_fill_rect(canvas, (float)x + 1.0f, (float)layout.scene_y,
                     (float)width - 2.0f, (float)layout.scene_height,
                     fill, selected ? 1.0f : 0.82f);
        if (active)
            sr_stroke_rect(canvas, (float)x, (float)layout.scene_y,
                           (float)width, (float)layout.scene_height, 2.0f,
                           COLOR_YELLOW, 1.0f);
        char number[4];
        (void)snprintf(number, sizeof number, "%u",
                       (unsigned)scene_number + 1u);
        draw_text_center(canvas, x + width / 2,
                         layout.scene_y + (layout.scene_height - 14) / 2,
                         number, selected ? COLOR_BG :
                         active ? COLOR_YELLOW : COLOR_DIM);
    }

    int panel_y = layout.scene_y + layout.scene_height + 12;
    int panel_height = canvas->h - panel_y - 36;
    if (panel_height < 0) panel_height = 0;
    sr_fill_rect(canvas, 10.0f, (float)panel_y,
                 (float)canvas->w - 20.0f, (float)panel_height,
                 COLOR_PANEL, 1.0f);
    const ktech_track *track = &scene->tracks[selected_track];
    const ktech_step *step = &track->steps[selected_step];
    ktech_edit_field field = ktech_editor_field(editor);
    char value[64];
    field_value(value, sizeof value, field, project, track, step);
    if (panel_height >= 18 && (canvas->w < 480 || panel_height < 48)) {
        char compact[128];
        (void)snprintf(compact, sizeof compact, "%s: %s | T%u %-.18s | STEP %02u",
                       field_name(field), value,
                       (unsigned)selected_track + 1u,
                       ktech_role_name((ktech_role)track->role),
                       (unsigned)selected_step + 1u);
        draw_text(canvas, 18, panel_y + 3, compact, COLOR_TEXT);
    } else if (panel_height >= 48) {
        int focus_width = canvas->w >= 820 ? 250 : canvas->w / 3;
        draw_text(canvas, 22, panel_y + 9, "EDITING", COLOR_DIM);
        draw_text(canvas, 86, panel_y + 9, field_name(field), COLOR_CYAN);
        if (panel_height >= 76) {
            sr_text_in(UI_FONT, canvas, 22.0f, (float)panel_y + 29.0f,
                       value, COLOR_TEXT, 1.0f, 2);
            int bar_width = focus_width - 34;
            int fill = (int)((unsigned)bar_width *
                             field_percent(field, project, track, step) / 100u);
            sr_fill_rect(canvas, 22.0f, (float)panel_y + 62.0f,
                         (float)bar_width, 5.0f, COLOR_GRID, 1.0f);
            sr_fill_rect(canvas, 22.0f, (float)panel_y + 62.0f,
                         (float)fill, 5.0f,
                         role_color((ktech_role)track->role), 1.0f);
        } else {
            draw_text(canvas, 22, panel_y + 30, value, COLOR_TEXT);
        }

        int info_x = focus_width + 18;
        char info[256];
        (void)snprintf(info, sizeof info, "TRACK %u  %-.22s  |  STEP %02u / %02u",
                       (unsigned)selected_track + 1u,
                       ktech_role_name((ktech_role)track->role),
                       (unsigned)selected_step + 1u, track->length);
        draw_text(canvas, info_x, panel_y + 9, info,
                  role_color((ktech_role)track->role));
        (void)snprintf(info, sizeof info,
                       "NOTE %3u   VEL %3u   GATE %3u%%   CHANCE %3u%%   RATCHET x%u",
                       step->note, step->velocity, step->gate,
                       step->probability, step->ratchet);
        draw_text(canvas, info_x, panel_y + 31, info, COLOR_TEXT);
        if (panel_height >= 72) {
            char pan[20];
            format_pan(pan, sizeof pan, track->pan);
            (void)snprintf(info, sizeof info,
                           "LEN %2u   LEVEL %3u%%   PAN %-8s   LAUNCH %s",
                           track->length, track->level, pan,
                           launch_name(project->launch_quantization));
            draw_text(canvas, info_x, panel_y + 53, info, COLOR_DIM);
        }
        if (audio && panel_height >= 94) {
            draw_text(canvas, info_x, panel_y + 76, "L", COLOR_DIM);
            draw_meter(canvas, info_x + 16, panel_y + 79, 92,
                       audio->peak_left, COLOR_CYAN);
            draw_text(canvas, info_x + 120, panel_y + 76, "R", COLOR_DIM);
            draw_meter(canvas, info_x + 136, panel_y + 79, 92,
                       audio->peak_right, COLOR_MAGENTA);
            (void)snprintf(info, sizeof info,
                           "MASTER %u%%  TONE %u%%  DRIVE %u%%  DELAY %u%%",
                           project->master_level, project->master_tone,
                           project->master_drive, project->master_delay);
            draw_text(canvas, info_x + 244, panel_y + 76, info, COLOR_DIM);
        }
    }

    char status[256];
    const char *backend = audio && audio->backend ? audio->backend : "NO AUDIO";
    if (audio && (audio->queue_failures || audio->transport.sink_failures)) {
        (void)snprintf(status, sizeof status,
                       "%s | %u HZ / %u MS | BANKS %u/%u | QERR %llu SINKERR %llu",
                       backend, audio->sample_rate, audio->latency_ms,
                       audio->soundbank_count, KTECH_SAMPLE_BANK_COUNT,
                       (unsigned long long)audio->queue_failures,
                       (unsigned long long)audio->transport.sink_failures);
    } else if (audio) {
        (void)snprintf(status, sizeof status,
                       "%s%s | %u HZ / %u MS | BANKS %u/%u %s",
                       backend, audio->pending ? " | QUEUED" : "",
                       audio->sample_rate, audio->latency_ms,
                       audio->soundbank_count, KTECH_SAMPLE_BANK_COUNT,
                       audio->soundbank_count == KTECH_SAMPLE_BANK_COUNT ?
                           "READY" : "AVAILABLE");
    } else {
        (void)snprintf(status, sizeof status,
                       "NO AUDIO | EDIT AND EXPORT READY | BANKS 0/%u",
                       KTECH_SAMPLE_BANK_COUNT);
    }
    if (canvas->w < 600) {
        if (audio && (audio->queue_failures ||
                      audio->transport.sink_failures)) {
            (void)snprintf(status, sizeof status,
                           "AUDIO ERROR Q%llu/S%llu | ? HELP",
                           (unsigned long long)audio->queue_failures,
                           (unsigned long long)audio->transport.sink_failures);
        } else {
            (void)snprintf(status, sizeof status,
                           "BANKS %u/%u %s | L PAINT | R MENU/ERASE | ?",
                           audio ? audio->soundbank_count : 0u,
                           KTECH_SAMPLE_BANK_COUNT,
                           audio && audio->soundbank_count ==
                               KTECH_SAMPLE_BANK_COUNT ? "READY" : "");
        }
    }
    int footer_y = canvas->h - 32;
    sr_fill_rect(canvas, 0.0f, (float)footer_y, (float)canvas->w, 32.0f,
                 COLOR_PANEL, 1.0f);
    sr_fill_circle(canvas, 14.0f, (float)footer_y + 16.0f, 3.0f,
                   message && message[0] ? COLOR_YELLOW :
                   audio && audio->audible ? COLOR_GREEN : COLOR_DIM, 1.0f);
    draw_text(canvas, 24, footer_y + 9,
              message && message[0] ? message : status,
              message && message[0] ? COLOR_YELLOW : COLOR_DIM);
    if (canvas->w >= 860 && (!message || !message[0])) {
        const char *hint =
            "L-DRAG PAINT  R-CLICK OPTIONS  R-DRAG ERASE  ? HELP";
        int hint_x = canvas->w - text_width(hint) - 14;
        sr_fill_rect(canvas, (float)hint_x - 8.0f, (float)footer_y,
                     (float)canvas->w - (float)hint_x + 8.0f, 32.0f,
                     COLOR_PANEL, 1.0f);
        draw_text(canvas, hint_x, footer_y + 9, hint, COLOR_TEXT);
    }

    if (ktech_editor_help_visible(editor)) {
        sr_fill_rect(canvas, 0.0f, 0.0f, (float)canvas->w,
                     (float)canvas->h, COLOR_BG, 0.82f);
        int box_width = canvas->w >= 760 ? 720 : canvas->w - 24;
        int box_height = canvas->h >= 520 ? 480 : canvas->h - 20;
        int x = (canvas->w - box_width) / 2;
        int y = (canvas->h - box_height) / 2;
        sr_fill_rect(canvas, (float)x, (float)y, (float)box_width,
                     (float)box_height, COLOR_PANEL, 1.0f);
        sr_stroke_rect(canvas, (float)x, (float)y, (float)box_width,
                       (float)box_height, 2.0f, COLOR_CYAN, 1.0f);
        draw_text(canvas, x + 18, y + 16, "CONTROLS", COLOR_CYAN);
        draw_text(canvas, x + 102, y + 16,
                  "KEYBOARD FIRST / MOUSE FRIENDLY", COLOR_DIM);
        const char *keys[] = {
            "ARROWS", "ENTER", "L-DRAG STEP", "R-CLICK STEP",
            "R-DRAG STEP", "+ / - / WHEEL", "SPACE", ". / CTRL+SPACE",
            "TAB/[ ]/L-TRK", "R-CLICK TRACK", "M / S", ", / >",
            "L-CLICK SCENE", "R-CLICK SCENE", "T", "CTRL+Z / Y",
            "CTRL+S / E", "1 / 2 / 3 / 4", "SHIFT+ARROWS",
            "CTRL+C/X/V", "CTRL+P / D", "ALT+A / ENTER",
            "ALT+F/C/I/M", "? / Q"
        };
        const char *descriptions[] = {
            "MOVE STEP / CHOOSE FIELD", "TOGGLE STEP", "TOGGLE / PAINT",
            "TRACK OPTIONS", "ERASE / PAINT OFF", "EDIT FOCUSED VALUE",
            "PLAY / PAUSE", "STOP / RESTART", "CHANGE TRACK",
            "TRACK OPTIONS",
            "MUTE / SOLO", "CHOOSE SCENE", "SELECT SCENE",
            "LAUNCH SCENE", "TAP TEMPO", "UNDO / REDO",
            "SAVE / EXPORT", "GRID / PIANO / SONG / MIXER",
            "EXTEND TILE RANGE", "COPY / CUT / PASTE RANGE",
            "LINK PATTERN / COPY PATTERN", "APPEND / LAUNCH SONG",
            "FILL / CLEAR / INVERT / MIRROR", "HELP / GUARDED QUIT"
        };
        size_t count = sizeof keys / sizeof keys[0];
        int columns = box_width >= 600 ? 2 : 1;
        int rows = ((int)count + columns - 1) / columns;
        int row_height = columns == 2 ? (box_height - 64) / rows : 14;
        if (row_height > 30) row_height = 30;
        if (row_height < 14) row_height = 14;
        int column_width = (box_width - 36) / columns;
        for (size_t index = 0u; index < count; ++index) {
            int column = (int)index / rows;
            int row = (int)index % rows;
            int item_x = x + 18 + column * column_width;
            int item_y = y + 48 + row * row_height;
            if (columns == 2) {
                int key_width = draw_chip(canvas, item_x, item_y - 5,
                                          keys[index], COLOR_CYAN, false);
                draw_text(canvas, item_x + key_width + 10, item_y + 1,
                          descriptions[index], COLOR_TEXT);
            } else {
                char line[64];
                (void)snprintf(line, sizeof line, "%-14.14s %-.20s",
                               keys[index], descriptions[index]);
                draw_text(canvas, item_x, item_y, line,
                          index % 2u == 0u ? COLOR_TEXT : COLOR_DIM);
            }
        }
        draw_text(canvas, x + 18, y + box_height - 24,
                  "LEFT-DRAG PAINTS; RIGHT-CLICK OPENS OPTIONS; RIGHT-DRAG ERASES",
                  COLOR_YELLOW);
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

bool ktech_ui_hit_track(const ktech_ui_layout *layout, int x, int y,
                        uint8_t *track)
{
    if (!layout || x < 6 || x >= layout->grid_x || y < layout->grid_y ||
        y >= layout->grid_y + layout->row_height * layout->track_rows)
        return false;
    int found = layout->track_start +
                (y - layout->grid_y) / layout->row_height;
    if (found < 0 || found >= (int)KTECH_TRACK_COUNT) return false;
    if (track) *track = (uint8_t)found;
    return true;
}

bool ktech_ui_hit_scene(const ktech_ui_layout *layout, int x, int y,
                        uint8_t *scene)
{
    if (!layout || layout->scene_width < (int)KTECH_SCENE_COUNT ||
        x < layout->scene_x || x >= layout->scene_x + layout->scene_width ||
        y < layout->scene_y || y >= layout->scene_y + layout->scene_height)
        return false;
    int button_width = layout->scene_width / (int)KTECH_SCENE_COUNT;
    int found = (x - layout->scene_x) / button_width;
    if (found < 0 || found >= (int)KTECH_SCENE_COUNT) return false;
    if (scene) *scene = (uint8_t)found;
    return true;
}

size_t ktech_ui_context_item_count(const ktech_ui_context_menu *menu)
{
    if (!menu) return 0u;
    switch (menu->page) {
    case KTECH_UI_CONTEXT_ACTIONS: return 4u;
    case KTECH_UI_CONTEXT_GROUPS:
        return KTECH_UI_INSTRUMENT_GROUP_COUNT + 1u;
    case KTECH_UI_CONTEXT_ROLES:
        if (menu->group < KTECH_UI_INSTRUMENT_GROUP_COUNT)
            return (size_t)instrument_groups[menu->group].role_count + 1u;
        return 1u;
    case KTECH_UI_CONTEXT_SEARCH: {
        if (menu->query_length == 0u) return 1u;
        size_t count = ktech_ui_search_roles(menu->query, NULL, 0u);
        if (count > 10u) count = 10u;
        return count + 1u;
    }
    default: return 0u;
    }
}

const char *ktech_ui_instrument_group_name(uint8_t group)
{
    return group < KTECH_UI_INSTRUMENT_GROUP_COUNT ?
        instrument_groups[group].name : "INSTRUMENTS";
}

bool ktech_ui_instrument_group_role(uint8_t group, uint8_t item,
                                    ktech_role *role)
{
    if (group >= KTECH_UI_INSTRUMENT_GROUP_COUNT ||
        item >= instrument_groups[group].role_count)
        return false;
    unsigned found = (unsigned)instrument_groups[group].first_role + item;
    if (found >= KTECH_ROLE_COUNT) return false;
    if (role) *role = (ktech_role)found;
    return true;
}

static unsigned ascii_lower(unsigned value)
{
    return value >= (unsigned)'A' && value <= (unsigned)'Z' ?
        value + ((unsigned)'a' - (unsigned)'A') : value;
}

static bool ascii_contains(const char *text, const char *query)
{
    if (!query || query[0] == '\0') return true;
    for (size_t start = 0u; text[start] != '\0'; ++start) {
        size_t offset = 0u;
        while (query[offset] != '\0' && text[start + offset] != '\0' &&
               ascii_lower((unsigned char)text[start + offset]) ==
               ascii_lower((unsigned char)query[offset]))
            ++offset;
        if (query[offset] == '\0') return true;
    }
    return false;
}

size_t ktech_ui_search_roles(const char *query, ktech_role *roles,
                             size_t capacity)
{
    if (!query) return 0u;
    size_t count = 0u;
    for (unsigned role = 0u; role < KTECH_ROLE_COUNT; ++role) {
        if (!ascii_contains(ktech_role_name((ktech_role)role), query))
            continue;
        if (roles && count < capacity) roles[count] = (ktech_role)role;
        ++count;
    }
    return count;
}

bool ktech_ui_context_role(const ktech_ui_context_menu *menu, uint8_t item,
                           ktech_role *role)
{
    if (!menu) return false;
    if (menu->page == KTECH_UI_CONTEXT_ROLES)
        return ktech_ui_instrument_group_role(menu->group, item, role);
    if (menu->page != KTECH_UI_CONTEXT_SEARCH || menu->query_length == 0u)
        return false;
    ktech_role matches[10];
    size_t count = ktech_ui_search_roles(menu->query, matches, 10u);
    if (count > 10u) count = 10u;
    if ((size_t)item >= count) return false;
    if (role) *role = matches[item];
    return true;
}

static bool context_geometry_make(context_geometry *geometry,
                                  const sr_canvas *canvas,
                                  const ktech_ui_context_menu *menu)
{
    if (!geometry || !canvas || !menu || canvas->w < 40 || canvas->h < 40)
        return false;
    size_t count = ktech_ui_context_item_count(menu);
    if (count == 0u || count > (size_t)INT32_MAX) return false;
    geometry->item_count = (int)count;
    geometry->header_height = 34;
    geometry->row_height = 24;
    int available = canvas->h - 24 - geometry->header_height;
    if (geometry->row_height * geometry->item_count > available)
        geometry->row_height = available / geometry->item_count;
    if (geometry->row_height < 16) geometry->row_height = 16;
    geometry->height = geometry->header_height +
                       geometry->row_height * geometry->item_count + 6;
    geometry->width = (menu->page == KTECH_UI_CONTEXT_ROLES ||
                       menu->page == KTECH_UI_CONTEXT_SEARCH) ? 340 : 292;
    if (geometry->width > canvas->w - 16)
        geometry->width = canvas->w - 16;
    geometry->x = menu->anchor_x;
    geometry->y = menu->anchor_y;
    if (geometry->x + geometry->width > canvas->w - 8)
        geometry->x = canvas->w - geometry->width - 8;
    if (geometry->y + geometry->height > canvas->h - 8)
        geometry->y = canvas->h - geometry->height - 8;
    if (geometry->x < 8) geometry->x = 8;
    if (geometry->y < 8) geometry->y = 8;
    return true;
}

static void context_item_label(char *output, size_t capacity,
                               const ktech_editor *editor,
                               const ktech_ui_context_menu *menu,
                               uint8_t item)
{
    const ktech_project *project = ktech_editor_project_const(editor);
    const ktech_track *track = &project->scenes[ktech_editor_scene(editor)]
                                     .tracks[menu->track];
    if (menu->page == KTECH_UI_CONTEXT_ACTIONS) {
        static const char *const actions[] = {
            "CHANGE INSTRUMENT  >", "MUTE TRACK", "SOLO TRACK", "CLOSE"
        };
        const char *label = item < 4u ? actions[item] : "CLOSE";
        if (item == 1u && track->mute) label = "UNMUTE TRACK";
        if (item == 2u && track->solo) label = "UNSOLO TRACK";
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
            (void)snprintf(output, capacity, "%s%s  [%s]%s",
                           track->role == (uint8_t)role ? "*  " : "   ",
                           ktech_role_name(role),
                           installed ? "installed" : "download",
                           ktech_editor_favorite(editor, role) ? "  +" : "");
        }
        else
            (void)snprintf(output, capacity, "%s",
                           menu->page == KTECH_UI_CONTEXT_SEARCH ?
                           "<  CLEAR SEARCH" : "<  INSTRUMENT BANKS");
    } else {
        output[0] = '\0';
    }
}

void ktech_ui_render_context_menu(sr_canvas *canvas,
                                  const ktech_editor *editor,
                                  const ktech_ui_context_menu *menu)
{
    if (!canvas || !editor || !menu || menu->track >= KTECH_TRACK_COUNT)
        return;
    context_geometry geometry;
    if (!context_geometry_make(&geometry, canvas, menu)) return;
    sr_fill_rect(canvas, (float)geometry.x + 5.0f,
                 (float)geometry.y + 5.0f, (float)geometry.width,
                 (float)geometry.height, COLOR_BG, 0.72f);
    sr_fill_rect(canvas, (float)geometry.x, (float)geometry.y,
                 (float)geometry.width, (float)geometry.height,
                 COLOR_PANEL, 1.0f);
    sr_stroke_rect(canvas, (float)geometry.x, (float)geometry.y,
                   (float)geometry.width, (float)geometry.height,
                   2.0f, COLOR_CYAN, 1.0f);
    char title[64];
    if (menu->page == KTECH_UI_CONTEXT_ACTIONS)
        (void)snprintf(title, sizeof title, "TRACK %u OPTIONS",
                       (unsigned)menu->track + 1u);
    else if (menu->page == KTECH_UI_CONTEXT_GROUPS)
        (void)snprintf(title, sizeof title, "CHOOSE INSTRUMENT BANK");
    else if (menu->page == KTECH_UI_CONTEXT_SEARCH)
        (void)snprintf(title, sizeof title, "SEARCH: %s%s", menu->query,
                       menu->query_length == 0u ? "TYPE TO FILTER" : "");
    else
        (void)snprintf(title, sizeof title, "%s",
                       ktech_ui_instrument_group_name(menu->group));
    draw_text(canvas, geometry.x + 12, geometry.y + 10,
              title, COLOR_CYAN);
    for (int index = 0; index < geometry.item_count; ++index) {
        int row_y = geometry.y + geometry.header_height +
                    index * geometry.row_height;
        char label[64];
        context_item_label(label, sizeof label, editor, menu,
                           (uint8_t)index);
        bool current = false;
        if (menu->page == KTECH_UI_CONTEXT_ROLES ||
            menu->page == KTECH_UI_CONTEXT_SEARCH) {
            ktech_role role;
            if (ktech_ui_context_role(menu, (uint8_t)index, &role)) {
                const ktech_project *project =
                    ktech_editor_project_const(editor);
                current = project->scenes[ktech_editor_scene(editor)]
                              .tracks[menu->track].role == (uint8_t)role;
            }
        }
        bool highlighted = (uint8_t)index == menu->selected;
        sr_fill_rect(canvas, (float)geometry.x + 6.0f, (float)row_y,
                     (float)geometry.width - 12.0f,
                     (float)geometry.row_height - 2.0f,
                     highlighted ? COLOR_CYAN :
                     current ? COLOR_PANEL_RAISED : COLOR_GRID,
                     highlighted ? 0.38f :
                     current ? 1.0f : index % 2 == 0 ? 0.42f : 0.24f);
        draw_text(canvas, geometry.x + 14,
                  row_y + (geometry.row_height - 14) / 2,
                  label, highlighted ? COLOR_TEXT :
                  current ? COLOR_CYAN : COLOR_TEXT);
    }
}

bool ktech_ui_hit_context_menu(const sr_canvas *canvas,
                               const ktech_ui_context_menu *menu,
                               int x, int y, uint8_t *item)
{
    context_geometry geometry;
    if (!context_geometry_make(&geometry, canvas, menu) ||
        x < geometry.x || x >= geometry.x + geometry.width ||
        y < geometry.y + geometry.header_height ||
        y >= geometry.y + geometry.header_height +
             geometry.item_count * geometry.row_height)
        return false;
    int found = (y - geometry.y - geometry.header_height) /
                geometry.row_height;
    if (found < 0 || found >= geometry.item_count) return false;
    if (item) *item = (uint8_t)found;
    return true;
}
