#include "app_internal.h"

#include "kilix_techno/actions.h"
#include "kilix_techno/audio.h"
#include "kilix_techno/export.h"
#include "kilix_techno/project.h"
#include "kilix_techno/settings.h"
#include "kilix_techno/ui.h"

#include "kitty_input_posix.h"
#include "kitty_terminal_session.h"
#include "soft_raster.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct export_job {
    pthread_t thread;
    ktech_project project;
    ktech_export_options options;
    ktech_export_info info;
    char output[4096];
    char error[256];
    atomic_bool cancel;
    atomic_bool done;
    bool success;
    bool joinable;
} export_job;

typedef struct app_state {
    ktech_editor *editor;
    ktech_audio *audio;
    kittyin_action_map action_map;
    kittyin_gamepads gamepads;
    ktech_settings settings;
    ktech_tap_tempo tap;
    export_job export;
    char project_path[4096];
    char message[256];
    uint64_t published_revision;
    uint64_t autosaved_revision;
    int64_t last_autosave_ms;
    bool quit_guard;
    bool exit_requested;
    bool settings_write_allowed;
    bool autosave_write_allowed;
    bool scene_launch_deferred;
    uint8_t deferred_scene;
    uint8_t deferred_boundary;
} app_state;

static volatile sig_atomic_t signal_exit;
static volatile sig_atomic_t signal_suspend;
static kittyts_session *signal_pixel_session;
static kittyin_terminal *signal_cell_terminal;

static int64_t monotonic_milliseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (int64_t)now.tv_sec * 1000 + (int64_t)now.tv_nsec / 1000000;
}

static void handle_signal(int number)
{
    if (number == SIGTSTP) signal_suspend = 1;
    else signal_exit = number;
}

static void handle_fatal_signal(int number)
{
    static const char restore[] = "\033[?25h\033[?1049l\033[0m\r\n";
    if (signal_pixel_session)
        kittyts_emergency_restore(signal_pixel_session);
    if (signal_cell_terminal)
        kittyin_terminal_emergency_restore(signal_cell_terminal);
    (void)write(STDOUT_FILENO, restore, sizeof restore - 1u);
    _exit(128 + number);
}

static void install_signal_handlers(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = handle_signal;
    (void)sigemptyset(&action.sa_mask);
    (void)sigaction(SIGINT, &action, NULL);
    (void)sigaction(SIGTERM, &action, NULL);
    (void)sigaction(SIGHUP, &action, NULL);
    (void)sigaction(SIGTSTP, &action, NULL);

    action.sa_handler = handle_fatal_signal;
    (void)sigaction(SIGABRT, &action, NULL);
    (void)sigaction(SIGSEGV, &action, NULL);
    (void)sigaction(SIGBUS, &action, NULL);
    (void)sigaction(SIGILL, &action, NULL);
    (void)sigaction(SIGFPE, &action, NULL);
}

static bool make_absolute(const char *input, char *output, size_t capacity)
{
    if (!input || !output || capacity == 0u) return false;
    if (input[0] == '/') {
        int count = snprintf(output, capacity, "%s", input);
        return count >= 0 && (size_t)count < capacity;
    }
    char working[4096];
    if (!getcwd(working, sizeof working)) return false;
    int count = snprintf(output, capacity, "%s/%s", working, input);
    return count >= 0 && (size_t)count < capacity;
}

static void set_message(app_state *app, const char *message)
{
    if (!app) return;
    (void)snprintf(app->message, sizeof app->message, "%s",
                   message ? message : "");
}

static void save_project(app_state *app, bool explicit_save)
{
    const ktech_project *project = ktech_editor_project_const(app->editor);
    ktech_project_result result;
    if (app->project_path[0] != '\0') {
        result = ktech_project_save(project, app->project_path, true);
    } else {
        result = ktech_project_save_autosave(project, NULL);
    }
    if (result == KTECH_PROJECT_OK) {
        if (explicit_save) ktech_editor_mark_saved(app->editor);
        if (explicit_save) app->autosave_write_allowed = true;
        app->autosaved_revision = ktech_editor_revision(app->editor);
        set_message(app, app->project_path[0] ? "Project saved" :
                                                 "Project saved to autosave");
    } else {
        char message[160];
        (void)snprintf(message, sizeof message, "Save failed: %s",
                       ktech_project_result_name(result));
        set_message(app, message);
    }
}

static void save_autosave(app_state *app)
{
    if (!app->autosave_write_allowed) return;
    ktech_project_result result = ktech_project_save_autosave(
        ktech_editor_project_const(app->editor), NULL);
    if (result == KTECH_PROJECT_OK) {
        app->autosaved_revision = ktech_editor_revision(app->editor);
    } else {
        char message[160];
        (void)snprintf(message, sizeof message, "Autosave failed: %s",
                       ktech_project_result_name(result));
        set_message(app, message);
    }
}

static void *export_thread_main(void *user)
{
    export_job *job = user;
    job->success = ktech_export_wav(&job->project, job->output,
                                    &job->options, &job->info,
                                    job->error, sizeof job->error);
    atomic_store_explicit(&job->done, true, memory_order_release);
    return NULL;
}

static void finish_export(app_state *app)
{
    export_job *job = &app->export;
    if (!job->joinable ||
        !atomic_load_explicit(&job->done, memory_order_acquire))
        return;
    (void)pthread_join(job->thread, NULL);
    job->joinable = false;
    if (job->success) {
        char message[256];
        (void)snprintf(message, sizeof message,
                       "Exported %llu frames to %.160s",
                       (unsigned long long)job->info.frames, job->output);
        set_message(app, message);
    } else if (atomic_load_explicit(&job->cancel, memory_order_acquire)) {
        set_message(app, "Export cancelled; no partial WAV was retained");
    } else {
        char message[256];
        (void)snprintf(message, sizeof message, "Export failed: %.210s",
                       job->error);
        set_message(app, message);
    }
}

static void cancel_export(app_state *app)
{
    export_job *job = &app->export;
    if (!job->joinable) return;
    atomic_store_explicit(&job->cancel, true, memory_order_release);
    (void)pthread_join(job->thread, NULL);
    job->joinable = false;
}

static void export_project(app_state *app, const ktech_cli_options *cli)
{
    export_job *job = &app->export;
    finish_export(app);
    if (job->joinable) {
        atomic_store_explicit(&job->cancel, true, memory_order_release);
        set_message(app, "Cancelling active export");
        return;
    }
    if (app->project_path[0] != '\0') {
        (void)snprintf(job->output, sizeof job->output, "%s",
                       app->project_path);
        char *extension = strrchr(job->output, '.');
        if (extension && strcmp(extension, ".ktech") == 0)
            (void)snprintf(extension,
                           (size_t)(job->output + sizeof job->output -
                                    extension),
                           "%s", ".wav");
        else if (strlen(job->output) + 4u < sizeof job->output)
            (void)strcat(job->output, ".wav");
    } else if (!make_absolute("kilix-techno-export.wav", job->output,
                              sizeof job->output)) {
        set_message(app, "Cannot resolve export path");
        return;
    }
    job->project = *ktech_editor_project_const(app->editor);
    ktech_export_options_init(&job->options);
    job->options.sample_rate = cli->sample_rate;
    job->options.bars = cli->bars;
    job->options.scene = ktech_editor_scene(app->editor);
    atomic_init(&job->cancel, false);
    atomic_init(&job->done, false);
    job->options.cancel = &job->cancel;
    job->success = false;
    job->error[0] = '\0';
    if (pthread_create(&job->thread, NULL, export_thread_main, job) != 0) {
        set_message(app, "Cannot start export worker");
        return;
    }
    job->joinable = true;
    set_message(app, "Export started; press Ctrl+E again to cancel");
}

static void request_publication(app_state *app, bool scene_launch)
{
    if (!app->audio) return;
    ktech_audio_status status;
    ktech_audio_get_status(app->audio, &status);
    const ktech_project *project = ktech_editor_project_const(app->editor);
    uint8_t scene = scene_launch ? app->deferred_scene : status.scene;
    ktech_launch_quantization boundary = scene_launch
        ? (ktech_launch_quantization)app->deferred_boundary
        : KTECH_LAUNCH_STEP;
    char error[160];
    if (ktech_audio_publish(app->audio, project, scene, boundary,
                            scene_launch, error, sizeof error)) {
        app->published_revision = ktech_editor_revision(app->editor);
        if (scene_launch) app->scene_launch_deferred = false;
    } else if (!status.pending) {
        set_message(app, error);
        if (scene_launch) app->scene_launch_deferred = false;
    }
}

static void apply_effect(app_state *app, unsigned effect,
                         const ktech_cli_options *cli)
{
    if ((effect & KTECH_EFFECT_MIX_CHANGED) != 0u && app->audio) {
        ktech_audio_status status;
        ktech_audio_get_status(app->audio, &status);
        (void)ktech_audio_update_mix(
            app->audio, ktech_editor_project_const(app->editor), status.scene);
    }
    if ((effect & KTECH_EFFECT_PROJECT_CHANGED) != 0u) {
        app->quit_guard = false;
        request_publication(app, false);
    }
    if ((effect & KTECH_EFFECT_SCENE_LAUNCH) != 0u) {
        const ktech_project *project =
            ktech_editor_project_const(app->editor);
        if (app->audio) {
            app->scene_launch_deferred = true;
            app->deferred_scene = ktech_editor_scene(app->editor);
            app->deferred_boundary = project->launch_quantization;
            request_publication(app, true);
            if (app->scene_launch_deferred)
                set_message(app,
                            "Scene launch queued behind the active edit");
        } else {
            set_message(app, "Scene selected; audio is unavailable");
        }
    }
    if ((effect & KTECH_EFFECT_PLAY_PAUSE) != 0u && app->audio)
        (void)ktech_audio_toggle_play(app->audio);
    if ((effect & KTECH_EFFECT_STOP) != 0u && app->audio)
        (void)ktech_audio_stop(app->audio);
    if ((effect & KTECH_EFFECT_RESTART) != 0u && app->audio)
        (void)ktech_audio_restart(app->audio);
    if ((effect & KTECH_EFFECT_TAP_TEMPO) != 0u) {
        uint16_t tempo;
        if (ktech_tap_tempo_press(&app->tap, monotonic_milliseconds(),
                                  &tempo)) {
            if (ktech_editor_set_tempo(app->editor, tempo))
                request_publication(app, false);
            char message[80];
            (void)snprintf(message, sizeof message, "Tap tempo: %u BPM",
                           tempo);
            set_message(app, message);
        } else {
            set_message(app, "Tap tempo: tap T again");
        }
    }
    if ((effect & KTECH_EFFECT_SAVE) != 0u) save_project(app, true);
    if ((effect & KTECH_EFFECT_EXPORT) != 0u) export_project(app, cli);
    if ((effect & KTECH_EFFECT_QUIT) != 0u) {
        if (ktech_editor_dirty(app->editor) &&
            !app->autosave_write_allowed) {
            app->quit_guard = false;
            set_message(app,
                "Corrupt autosave is preserved; Ctrl+S replaces it, then Q quits");
            return;
        }
        if (ktech_editor_dirty(app->editor) && !app->quit_guard) {
            app->quit_guard = true;
            set_message(app, "Unsaved changes: press Q again to save autosave and quit");
        } else {
            app->exit_requested = true;
        }
    }
}

static void dispatch_action(app_state *app, const ktech_cli_options *cli,
                            const kittyin_action_event *event)
{
    if (!event || event->action >= KTECH_ACTION_COUNT) return;
    unsigned effect = ktech_editor_apply(app->editor,
                                         (ktech_action)event->action,
                                         event->phase);
    apply_effect(app, effect, cli);
}

static void dispatch_key(app_state *app, const ktech_cli_options *cli,
                         const kittykb_event *key)
{
    kittyin_action_event actions[4];
    size_t count = ktech_actions_map_key(&app->action_map, key,
                                         actions, 4u);
    for (size_t index = 0u; index < count; ++index)
        dispatch_action(app, cli, &actions[index]);
}

enum {
    KTECH_MOUSE_LEFT = 1u,
    KTECH_MOUSE_RIGHT = 3u
};

static bool pointer_step(app_state *app, uint8_t track, uint8_t step,
                         uint8_t button)
{
    if (!app || (button != KTECH_MOUSE_LEFT &&
                 button != KTECH_MOUSE_RIGHT))
        return false;
    uint64_t revision = ktech_editor_revision(app->editor);
    bool valid = button == KTECH_MOUSE_LEFT ?
        ktech_editor_select_step(app->editor, track, step, true) :
        ktech_editor_set_step_enabled(app->editor, track, step, false);
    if (valid && ktech_editor_revision(app->editor) != revision)
        request_publication(app, false);
    return valid;
}

static void pointer_track(app_state *app, const ktech_cli_options *cli,
                          uint8_t track, uint8_t button)
{
    if (!app || (button != KTECH_MOUSE_LEFT &&
                 button != KTECH_MOUSE_RIGHT))
        return;
    (void)ktech_editor_select_track(app->editor, track);
    if (button == KTECH_MOUSE_RIGHT) {
        kittyin_action_event action = {
            KTECH_ACTION_MUTE, KITTYIN_ACTION_PRESS,
            KITTYIN_DEVICE_KEYBOARD, 0u
        };
        dispatch_action(app, cli, &action);
    }
}

static void pointer_scene(app_state *app, const ktech_cli_options *cli,
                          uint8_t scene, uint8_t button)
{
    if (!app || (button != KTECH_MOUSE_LEFT &&
                 button != KTECH_MOUSE_RIGHT))
        return;
    (void)ktech_editor_select_scene(app->editor, scene);
    if (button == KTECH_MOUSE_RIGHT) {
        set_message(app, "Scene launch requested");
        kittyin_action_event action = {
            KTECH_ACTION_LAUNCH_SCENE, KITTYIN_ACTION_PRESS,
            KITTYIN_DEVICE_KEYBOARD, 0u
        };
        dispatch_action(app, cli, &action);
    } else {
        set_message(app, "Scene selected; right-click or press L to launch");
    }
}

static void poll_gamepads(app_state *app, const ktech_cli_options *cli,
                          uint32_t elapsed_ms)
{
    kittyin_gamepads_poll(&app->gamepads, monotonic_milliseconds());
    kittyin_gamepad_event input;
    while (kittyin_gamepads_next(&app->gamepads, &input)) {
        kittyin_action_event actions[4];
        size_t count = kittyin_action_map_gamepad(&app->action_map, &input,
                                                  actions, 4u);
        for (size_t index = 0u; index < count; ++index)
            dispatch_action(app, cli, &actions[index]);
    }
    kittyin_action_event repeats[4];
    size_t count = kittyin_action_map_advance(&app->action_map, elapsed_ms,
                                              repeats, 4u);
    for (size_t index = 0u; index < count; ++index)
        dispatch_action(app, cli, &repeats[index]);
}

static void periodic_work(app_state *app)
{
    finish_export(app);
    if (app->audio) {
        ktech_audio_poll(app->audio);
        ktech_audio_status status;
        ktech_audio_get_status(app->audio, &status);
        uint64_t revision = ktech_editor_revision(app->editor);
        if (!status.pending && app->scene_launch_deferred)
            request_publication(app, true);
        else if (!status.pending && revision != app->published_revision)
            request_publication(app, false);
    }
    int64_t now = monotonic_milliseconds();
    uint64_t revision = ktech_editor_revision(app->editor);
    if (revision != app->autosaved_revision &&
        now - app->last_autosave_ms >= 2000) {
        save_autosave(app);
        app->last_autosave_ms = now;
    }
}

static bool allocate_frame(sr_canvas *canvas, uint8_t **rgba,
                           int width, int height)
{
    sr_canvas_free(canvas);
    free(*rgba);
    *rgba = NULL;
    if (!sr_canvas_init(canvas, width, height)) return false;
    size_t pixels = (size_t)width * (size_t)height;
    if (pixels > SIZE_MAX / 4u) {
        sr_canvas_free(canvas);
        return false;
    }
    *rgba = malloc(pixels * 4u);
    if (!*rgba) {
        sr_canvas_free(canvas);
        return false;
    }
    return true;
}

static void handle_pixel_event(app_state *app, const ktech_cli_options *cli,
                               kittyts_session *session,
                               const kittyin_event *event)
{
    if (event->kind == KITTYIN_EVENT_KEY) {
        dispatch_key(app, cli, &event->data.key);
    } else if (event->kind == KITTYIN_EVENT_MOUSE) {
        const kittyin_mouse_event *mouse = &event->data.mouse;
        if (mouse->action == KITTYIN_MOUSE_WHEEL) {
            kittyin_action_event action = {
                mouse->wheel_y > 0 ? KTECH_ACTION_DECREASE :
                                     KTECH_ACTION_INCREASE,
                KITTYIN_ACTION_PRESS, KITTYIN_DEVICE_KEYBOARD, 0u
            };
            dispatch_action(app, cli, &action);
        } else if (mouse->action == KITTYIN_MOUSE_PRESS) {
            int x, y;
            if (mouse->pixel_coordinates) {
                x = mouse->x - kittyts_origin_x(session);
                y = mouse->y - kittyts_origin_y(session);
            } else {
                x = (mouse->x - 1) * kittyts_cell_width(session) -
                    kittyts_origin_x(session);
                y = (mouse->y - 1) * kittyts_cell_height(session) -
                    kittyts_origin_y(session);
            }
            ktech_ui_layout layout;
            ktech_ui_layout_make(&layout, kittyts_width(session),
                                  kittyts_height(session), app->editor);
            uint8_t track, step;
            if (ktech_ui_hit_step(&layout, x, y, &track, &step)) {
                (void)pointer_step(app, track, step, mouse->button);
            } else if (ktech_ui_hit_track(&layout, x, y, &track)) {
                pointer_track(app, cli, track, mouse->button);
            } else {
                uint8_t scene;
                if (ktech_ui_hit_scene(&layout, x, y, &scene))
                    pointer_scene(app, cli, scene, mouse->button);
            }
        }
    } else if (event->kind == KITTYIN_EVENT_FOCUS &&
               !event->data.focus.focused) {
        set_message(app, "Terminal focus lost; project remains active");
    }
}

static bool resume_pixel(kittyts_session *session,
                         const kittyts_options *options)
{
    struct sigaction action;
    kittyts_suspend(session);
    signal_pixel_session = NULL;
    memset(&action, 0, sizeof action);
    action.sa_handler = SIG_DFL;
    (void)sigemptyset(&action.sa_mask);
    (void)sigaction(SIGTSTP, &action, NULL);
    (void)raise(SIGTSTP);
    action.sa_handler = handle_signal;
    (void)sigaction(SIGTSTP, &action, NULL);
    signal_suspend = 0;
    if (kittyts_start(session, STDIN_FILENO, STDOUT_FILENO, options) != 0)
        return false;
    signal_pixel_session = session;
    return true;
}

static int run_pixel(app_state *app, const ktech_cli_options *cli)
{
    kittyts_options options;
    kittyts_options_init(&options);
    options.mouse_tracking = KITTYIN_MOUSE_TRACKING_BUTTON;
    options.pixel_mouse = true;
    options.focus_events = true;
    options.framebuffer.min_width = 320;
    options.framebuffer.min_height = 240;
    kittyts_session session;
    kittyts_session_init(&session);
    if (kittyts_start(&session, STDIN_FILENO, STDOUT_FILENO, &options) != 0)
        return -1;
    signal_pixel_session = &session;

    sr_canvas canvas = {0};
    uint8_t *rgba = NULL;
    if (!allocate_frame(&canvas, &rgba, kittyts_width(&session),
                        kittyts_height(&session))) {
        kittyts_stop(&session);
        signal_pixel_session = NULL;
        return 1;
    }
    int64_t previous = monotonic_milliseconds();
    bool failed = false;
    while (!app->exit_requested && !signal_exit) {
        int64_t now = monotonic_milliseconds();
        uint32_t elapsed = now > previous ?
            (uint32_t)(now - previous > UINT32_MAX ? UINT32_MAX :
                       now - previous) : 0u;
        previous = now;
        if (signal_suspend && !resume_pixel(&session, &options)) {
            failed = true;
            break;
        }
        (void)kittyts_read_input(&session);
        kittyin_event event;
        while (kittyts_next_event(&session, &event))
            handle_pixel_event(app, cli, &session, &event);
        poll_gamepads(app, cli, elapsed);
        periodic_work(app);

        int width, height;
        if (kittyts_check_resize(&session, &width, &height) &&
            (width != canvas.w || height != canvas.h) &&
            !allocate_frame(&canvas, &rgba, width, height)) {
            set_message(app, "Resize allocation failed");
            failed = true;
            break;
        }
        ktech_audio_status status;
        ktech_audio_get_status(app->audio, &status);
        ktech_ui_render(&canvas, app->editor, &status, app->message);
        if (!sr_pack_rgba(&canvas, rgba,
                          (size_t)canvas.w * (size_t)canvas.h * 4u) ||
            !kittyts_present(&session, rgba, canvas.w, canvas.h)) {
            failed = true;
            break;
        }
        struct pollfd input = {STDIN_FILENO, POLLIN, 0};
        (void)poll(&input, 1u, 33);
    }
    free(rgba);
    sr_canvas_free(&canvas);
    signal_pixel_session = NULL;
    kittyts_stop(&session);
    return failed && !signal_exit && !app->exit_requested ? -1 : 0;
}

static bool write_output(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    while (size > 0u) {
        ssize_t count = write(STDOUT_FILENO, bytes, size);
        if (count > 0) {
            bytes += (size_t)count;
            size -= (size_t)count;
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

static bool resume_cell(kittyin_terminal *terminal,
                        const kittyin_terminal_options *options)
{
    static const char leave[] = "\033[?25h\033[?1049l";
    static const char enter[] = "\033[?1049h\033[?25l\033[2J\033[H";
    struct sigaction action;
    (void)kittyin_terminal_stop(terminal);
    (void)write_output(leave, sizeof leave - 1u);
    signal_cell_terminal = NULL;
    memset(&action, 0, sizeof action);
    action.sa_handler = SIG_DFL;
    (void)sigemptyset(&action.sa_mask);
    (void)sigaction(SIGTSTP, &action, NULL);
    (void)raise(SIGTSTP);
    action.sa_handler = handle_signal;
    (void)sigaction(SIGTSTP, &action, NULL);
    signal_suspend = 0;
    if (!write_output(enter, sizeof enter - 1u) ||
        kittyin_terminal_start(terminal, STDIN_FILENO, STDOUT_FILENO,
                               options) != 0)
        return false;
    signal_cell_terminal = terminal;
    return true;
}

static void handle_cell_mouse(app_state *app, const ktech_cli_options *cli,
                              const kittyin_mouse_event *mouse, int columns)
{
    if (mouse->action == KITTYIN_MOUSE_WHEEL) {
        kittyin_action_event action = {
            mouse->wheel_y > 0 ? KTECH_ACTION_DECREASE : KTECH_ACTION_INCREASE,
            KITTYIN_ACTION_PRESS, KITTYIN_DEVICE_KEYBOARD, 0u
        };
        dispatch_action(app, cli, &action);
        return;
    }
    if (mouse->action != KITTYIN_MOUSE_PRESS || mouse->pixel_coordinates)
        return;
    if (mouse->button != KTECH_MOUSE_LEFT &&
        mouse->button != KTECH_MOUSE_RIGHT)
        return;
    int visible = columns >= 100 ? 16 : columns >= 72 ? 12 : 8;
    int label_width = columns >= 100 ? 19 : columns >= 72 ? 14 : 11;
    int row = mouse->y - 2;
    if (row >= 0 && row < (int)KTECH_TRACK_COUNT &&
        mouse->x >= 0 && mouse->x < label_width) {
        pointer_track(app, cli, (uint8_t)row, mouse->button);
        return;
    }
    int column = (mouse->x - label_width) / 3;
    if (row >= 0 && row < (int)KTECH_TRACK_COUNT &&
        mouse->x >= label_width && column >= 0 && column < visible) {
        int page = ktech_editor_step(app->editor) / visible * visible;
        (void)pointer_step(app, (uint8_t)row,
                           (uint8_t)(page + column), mouse->button);
        return;
    }
    if (columns >= 100 && mouse->y == 13 && mouse->x >= 7 &&
        mouse->x < 7 + (int)KTECH_SCENE_COUNT * 4) {
        uint8_t scene = (uint8_t)((mouse->x - 7) / 4);
        pointer_scene(app, cli, scene, mouse->button);
    }
}

static int run_cell(app_state *app, const ktech_cli_options *cli)
{
    static const char enter[] = "\033[?1049h\033[?25l\033[2J\033[H";
    static const char leave[] = "\033[0m\033[?25h\033[?1049l";
    kittyin_terminal_options options;
    kittyin_terminal_options_init(&options);
    options.mouse_tracking = KITTYIN_MOUSE_TRACKING_BUTTON;
    options.pixel_mouse = false;
    options.focus_events = true;
    kittyin_terminal terminal;
    kittyin_terminal_init(&terminal);
    if (!write_output(enter, sizeof enter - 1u) ||
        kittyin_terminal_start(&terminal, STDIN_FILENO, STDOUT_FILENO,
                               &options) != 0)
        return 1;
    signal_cell_terminal = &terminal;
    int64_t previous = monotonic_milliseconds();
    bool failed = false;
    while (!app->exit_requested && !signal_exit) {
        if (signal_suspend && !resume_cell(&terminal, &options)) {
            failed = true;
            break;
        }
        int64_t now = monotonic_milliseconds();
        uint32_t elapsed = now > previous ?
            (uint32_t)(now - previous > UINT32_MAX ? UINT32_MAX :
                       now - previous) : 0u;
        previous = now;
        (void)kittyin_terminal_read(&terminal);
        kittyin_event event;
        struct winsize window = {0};
        int columns = ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == 0
                          ? window.ws_col : 80;
        while (kittyin_input_next(&terminal.input, &event)) {
            if (event.kind == KITTYIN_EVENT_KEY)
                dispatch_key(app, cli, &event.data.key);
            else if (event.kind == KITTYIN_EVENT_MOUSE)
                handle_cell_mouse(app, cli, &event.data.mouse, columns);
        }
        poll_gamepads(app, cli, elapsed);
        periodic_work(app);
        ktech_audio_status status;
        ktech_audio_get_status(app->audio, &status);
        char screen[16384];
        (void)ktech_cell_render(screen, sizeof screen, columns, app->editor,
                                &status, app->message);
        static const char home[] = "\033[H\033[2J";
        if (!write_output(home, sizeof home - 1u) ||
            !write_output(screen, strlen(screen))) {
            failed = true;
            break;
        }
        struct pollfd input = {STDIN_FILENO, POLLIN, 0};
        (void)poll(&input, 1u, 50);
    }
    signal_cell_terminal = NULL;
    (void)kittyin_terminal_stop(&terminal);
    (void)write_output(leave, sizeof leave - 1u);
    return failed && !signal_exit && !app->exit_requested ? 1 : 0;
}

static int run_headless(const ktech_cli_options *options,
                        ktech_project *project, const char *path)
{
    if (options->save_path) {
        char destination[4096];
        if (!make_absolute(options->save_path, destination,
                           sizeof destination)) {
            (void)fprintf(stderr, "cannot resolve save path\n");
            return 1;
        }
        ktech_project_result result = ktech_project_save(
            project, destination, false);
        if (result != KTECH_PROJECT_OK) {
            (void)fprintf(stderr, "save: %s\n",
                          ktech_project_result_name(result));
            return 1;
        }
    }
    (void)printf("Kilix Techno 0.1.0\nproject=%s\nname=%s\ntempo=%u\nscene=%u\n",
                 path ? path : "new", project->name, project->tempo,
                 (unsigned)project->active_scene + 1u);
    return 0;
}

static int run_export(const ktech_cli_options *options)
{
    char input[4096], output[4096];
    if (!make_absolute(options->export_input, input, sizeof input) ||
        !make_absolute(options->export_output, output, sizeof output)) {
        (void)fprintf(stderr, "cannot resolve export path\n");
        return 1;
    }
    ktech_project project;
    ktech_project_result result = ktech_project_load(&project, input);
    if (result != KTECH_PROJECT_OK) {
        (void)fprintf(stderr, "load: %s\n", ktech_project_result_name(result));
        return 1;
    }
    ktech_export_options export_options;
    ktech_export_options_init(&export_options);
    export_options.sample_rate = options->sample_rate;
    export_options.bars = options->bars;
    export_options.scene = project.active_scene;
    char error[256];
    ktech_export_info info;
    if (!ktech_export_wav(&project, output, &export_options, &info,
                          error, sizeof error)) {
        (void)fprintf(stderr, "export: %s\n", error);
        return 1;
    }
    (void)printf("exported %llu frames (%u bytes), hash=%016llx\n",
                 (unsigned long long)info.frames, info.bytes,
                 (unsigned long long)info.pcm_hash);
    return 0;
}

int ktech_app_run(const ktech_cli_options *options)
{
    if (!options) return 1;
    if (options->export_input) return run_export(options);

    ktech_project project;
    char opened[4096] = {0};
    char recovery_warning[512] = {0};
    bool autosave_write_allowed = true;
    const char *description = NULL;
    if (options->open_path) {
        if (!make_absolute(options->open_path, opened, sizeof opened)) {
            (void)fprintf(stderr, "cannot resolve project path\n");
            return 1;
        }
        ktech_project_result result = ktech_project_load(&project, opened);
        if (result != KTECH_PROJECT_OK) {
            (void)fprintf(stderr, "load: %s\n",
                          ktech_project_result_name(result));
            return 1;
        }
        description = opened;
    } else {
        ktech_project_result autosave_result = KTECH_PROJECT_NOT_FOUND;
        if (!options->new_project)
            autosave_result = ktech_project_load_autosave(&project, NULL);
        if (autosave_result == KTECH_PROJECT_OK) {
            description = "autosave";
        } else {
            ktech_project_init(&project, options->seed);
            description = "new";
            if (!options->new_project &&
                autosave_result != KTECH_PROJECT_NOT_FOUND) {
                char recovery_path[4096];
                ktech_project_result quarantined =
                    ktech_project_quarantine_autosave(
                        NULL, recovery_path, sizeof recovery_path);
                autosave_write_allowed = quarantined == KTECH_PROJECT_OK;
                if (quarantined == KTECH_PROJECT_OK) {
                    (void)snprintf(recovery_warning,
                                   sizeof recovery_warning,
                                   "Corrupt autosave preserved at %.420s",
                                   recovery_path);
                } else {
                    (void)ktech_project_autosave_path(
                        NULL, recovery_path, sizeof recovery_path);
                    (void)snprintf(recovery_warning,
                                   sizeof recovery_warning,
                                   "Autosave %s at %.380s; bytes preserved",
                                   ktech_project_result_name(autosave_result),
                                   recovery_path);
                }
            }
        }
    }
    if (options->headless || !isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        if (recovery_warning[0])
            (void)fprintf(stderr, "%s\n", recovery_warning);
        return run_headless(options, &project, description);
    }

    app_state app;
    memset(&app, 0, sizeof app);
    ktech_settings_init(&app.settings);
    ktech_settings_result settings_result = ktech_settings_load(
        &app.settings, NULL);
    app.settings_write_allowed = settings_result == KTECH_SETTINGS_OK ||
                                 settings_result == KTECH_SETTINGS_NOT_FOUND;
    app.autosave_write_allowed = autosave_write_allowed;
    if (settings_result == KTECH_SETTINGS_NOT_FOUND)
        ktech_settings_init(&app.settings);
    if (strcmp(description, "new") == 0 &&
        ktech_settings_validate(&app.settings)) {
        project.reduced_motion = app.settings.reduced_motion;
        memset(project.bindings, 0, sizeof project.bindings);
        project.binding_count = app.settings.binding_count;
        memcpy(project.bindings, app.settings.bindings,
               (size_t)project.binding_count * sizeof project.bindings[0]);
    }
    app.editor = ktech_editor_create(&project);
    if (!app.editor) return 1;
    if (opened[0]) (void)snprintf(app.project_path, sizeof app.project_path,
                                  "%s", opened);
    if (!ktech_actions_bind_project(
            &app.action_map, ktech_editor_project_const(app.editor)) &&
        !ktech_settings_apply_action_map(&app.settings, &app.action_map))
        ktech_actions_bind_defaults(&app.action_map);
    ktech_tap_tempo_init(&app.tap);
    kittyin_gamepads_init(&app.gamepads);
    app.published_revision = ktech_editor_revision(app.editor);
    app.autosaved_revision = app.published_revision;
    app.last_autosave_ms = monotonic_milliseconds();
    if (recovery_warning[0]) set_message(&app, recovery_warning);
    if (!app.settings_write_allowed) {
        char path[4096];
        if (ktech_settings_path(NULL, path, sizeof path) == KTECH_SETTINGS_OK) {
            char warning[256];
            (void)snprintf(warning, sizeof warning,
                           "Settings %s at %.170s; bytes preserved",
                           ktech_settings_result_name(settings_result), path);
            set_message(&app, warning);
        } else {
            set_message(&app, "Settings are invalid; bytes preserved");
        }
    }

    if (!options->no_audio) {
        ktech_audio_options audio_options;
        ktech_audio_options_init(&audio_options);
        audio_options.sample_rate = options->sample_rate;
        char error[200];
        app.audio = ktech_audio_create(&project, &audio_options,
                                       error, sizeof error);
        if (!app.audio) set_message(&app, error);
    }
    install_signal_handlers();
    int result = options->force_cell ? -1 : run_pixel(&app, options);
    if (result < 0 && !signal_exit) result = run_cell(&app, options);

    if (ktech_editor_dirty(app.editor)) save_autosave(&app);
    cancel_export(&app);
    if (app.settings_write_allowed &&
        ktech_settings_from_action_map(
            &app.settings, &app.action_map,
            ktech_editor_project_const(app.editor)->reduced_motion)) {
        ktech_settings_result saved = ktech_settings_save(&app.settings, NULL);
        if (saved != KTECH_SETTINGS_OK)
            (void)fprintf(stderr, "settings: %s\n",
                          ktech_settings_result_name(saved));
    }
    kittyin_gamepads_close(&app.gamepads);
    ktech_audio_free(app.audio);
    ktech_editor_free(app.editor);
    signal_pixel_session = NULL;
    signal_cell_terminal = NULL;
    return result < 0 ? 1 : result;
}
