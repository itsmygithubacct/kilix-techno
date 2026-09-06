#include "app_internal.h"

#include "kilix_techno/actions.h"
#include "kilix_techno/audio.h"
#include "kilix_techno/export.h"
#include "kilix_techno/lazy_install.h"
#include "kilix_techno/midi.h"
#include "kilix_techno/project.h"
#include "kilix_techno/settings.h"
#include "kilix_techno/soundbank.h"
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
#include <sys/file.h>
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
    /* Whether this session may write the shared autosave.state. A session
     * that loaded it, or found it absent, owns it. Any other session autosaves
     * to session_slot and may replace the shared slot only by a deliberate
     * second Ctrl+S; before this rule every session's first edit overwrote
     * whatever the previous session had saved there, after two seconds and
     * with no prompt. */
    bool autosave_owner;
    bool autosave_claim_pending;
    bool session_slot_written;
    char session_slot[64];
    /* Ownership is a claim, not a belief: the owner holds an exclusive
     * flock on autosave.owner for the life of the session. The lock dies with
     * the process, so a crashed owner leaves nothing to reclaim, and two
     * sessions that both find the shared slot absent cannot both own it: the
     * earlier O_EXCL-then-write claim let simultaneous starts both succeed. */
    int autosave_lock_fd;
    char autosave_lock_path[4096];
    bool exit_save_failed;
    bool scene_launch_deferred;
    uint8_t deferred_scene;
    uint8_t deferred_boundary;
    ktech_ui_context_menu context_menu;
    ktech_lazy_install lazy_install;
    bool pointer_drag_active;
    bool pointer_drag_right;
    bool pointer_drag_enabled;
    bool pointer_select_active;
    bool pointer_piano_active;
    bool pointer_paint_started;
    bool pointer_right_pending;
    uint8_t pointer_drag_track;
    uint8_t pointer_drag_step;
    uint8_t pointer_press_track;
    uint8_t pointer_press_step;
    int pointer_press_x;
    int pointer_press_y;
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

static bool claim_autosave_lock(app_state *app)
{
    if (app->autosave_lock_fd >= 0) return true;
    if (app->autosave_lock_path[0] == '\0') return false;
    /* The mark file is never removed; holding its lock is the claim. A
     * symlink or a directory in its place cannot be opened this way and makes
     * this session a guest. */
    int fd = open(app->autosave_lock_path,
                  O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return false;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        (void)close(fd);
        return false;
    }
    char text[32];
    int length = snprintf(text, sizeof text, "%ld\n", (long)getpid());
    if (length > 0 && ftruncate(fd, 0) == 0)
        (void)!write(fd, text, (size_t)length);
    app->autosave_lock_fd = fd;
    return true;
}

static void release_autosave_lock(app_state *app)
{
    if (app->autosave_lock_fd < 0) return;
    /* The pid inside is informational; an empty mark reads as unheld. */
    (void)ftruncate(app->autosave_lock_fd, 0);
    (void)close(app->autosave_lock_fd);
    app->autosave_lock_fd = -1;
}

static void save_project(app_state *app, bool explicit_save)
{
    const ktech_project *project = ktech_editor_project_const(app->editor);
    ktech_project_result result;
    bool claimed = false;
    if (app->project_path[0] != '\0') {
        result = ktech_project_save(project, app->project_path, true);
    } else if (app->autosave_owner) {
        result = ktech_project_save_autosave(project, NULL);
    } else if (app->autosave_claim_pending) {
        app->autosave_claim_pending = false;
        if (!claim_autosave_lock(app)) {
            set_message(app, "Another running session owns the autosave; "
                             "nothing replaced");
            return;
        }
        result = ktech_project_save_autosave(project, NULL);
        claimed = result == KTECH_PROJECT_OK;
        if (!claimed) release_autosave_lock(app);
    } else {
        app->autosave_claim_pending = true;
        set_message(app, "Autosave holds another project: "
                         "press Ctrl+S again to replace it");
        return;
    }
    if (result == KTECH_PROJECT_OK) {
        if (claimed) {
            app->autosave_owner = true;
            app->autosave_claim_pending = false;
            if (app->session_slot_written)
                (void)ktech_project_remove_autosave_slot(NULL,
                                                         app->session_slot);
            app->session_slot_written = false;
        }
        if (explicit_save) ktech_editor_mark_saved(app->editor);
        if (explicit_save) app->autosave_write_allowed = true;
        app->autosaved_revision = ktech_editor_revision(app->editor);
        set_message(app, app->project_path[0] ? "Project saved" :
                    claimed ? "Project saved to autosave, replacing the previous one" :
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
    const ktech_project *project = ktech_editor_project_const(app->editor);
    ktech_project_result result = app->autosave_owner ?
        ktech_project_save_autosave(project, NULL) :
        ktech_project_save_autosave_slot(project, NULL, app->session_slot);
    if (result == KTECH_PROJECT_OK) {
        app->autosaved_revision = ktech_editor_revision(app->editor);
        if (!app->autosave_owner) app->session_slot_written = true;
        app->exit_save_failed = false;
    } else {
        char message[160];
        (void)snprintf(message, sizeof message, "Autosave failed: %s",
                       ktech_project_result_name(result));
        set_message(app, message);
        app->exit_save_failed = true;
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
        app->autosave_claim_pending = false;
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
            if (app->autosave_owner) {
                set_message(app, "Unsaved changes: press Q again to save autosave and quit");
            } else {
                char message[160];
                (void)snprintf(message, sizeof message,
                               "Unsaved changes: press Q again to quit; "
                               "they stay in %s", app->session_slot);
                set_message(app, message);
            }
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
    if (!app || !key || key->action == KITTYKB_ACTION_RELEASE) return;
    uint32_t modifiers = key->modifiers &
        (KITTYKB_MOD_SHIFT | KITTYKB_MOD_ALT | KITTYKB_MOD_CTRL);
    uint32_t letter = key->key;
    if (letter >= (uint32_t)'A' && letter <= (uint32_t)'Z')
        letter += (uint32_t)('a' - 'A');
    if (modifiers == 0u && letter >= (uint32_t)'1' &&
        letter <= (uint32_t)'4') {
        (void)ktech_editor_set_view(
            app->editor, (ktech_editor_view)(letter - (uint32_t)'1'));
        static const char *const names[] = {
            "Step grid", "Piano roll", "Arrangement", "Mixer"
        };
        set_message(app, names[letter - (uint32_t)'1']);
        return;
    }
    if ((modifiers & KITTYKB_MOD_CTRL) != 0u && letter == (uint32_t)'c') {
        set_message(app, ktech_editor_copy_range(app->editor) ?
                         "Selection copied" : "Select tiles before copying");
        return;
    }
    if ((modifiers & KITTYKB_MOD_CTRL) != 0u && letter == (uint32_t)'x') {
        if (ktech_editor_cut_range(app->editor)) {
            request_publication(app, false);
            set_message(app, "Selection cut");
        } else set_message(app, "Select tiles before cutting");
        return;
    }
    if ((modifiers & KITTYKB_MOD_CTRL) != 0u && letter == (uint32_t)'v') {
        if (ktech_editor_paste_range(app->editor,
                                     ktech_editor_track(app->editor),
                                     ktech_editor_step(app->editor))) {
            request_publication(app, false);
            set_message(app, "Selection pasted");
        } else set_message(app, "Clipboard does not fit here");
        return;
    }
    if ((modifiers & KITTYKB_MOD_CTRL) != 0u && letter == (uint32_t)'p') {
        char name[KTECH_PATTERN_NAME_CAPACITY];
        const ktech_project *project =
            ktech_editor_project_const(app->editor);
        (void)snprintf(name, sizeof name, "Pattern %u",
                       (unsigned)project->pattern_count + 1u);
        if (ktech_editor_create_pattern(app->editor, name, NULL)) {
            request_publication(app, false);
            set_message(app, "Reusable linked pattern created");
        } else set_message(app, "Pattern capacity reached");
        return;
    }
    if ((modifiers & KITTYKB_MOD_CTRL) != 0u && letter == (uint32_t)'d') {
        bool linked = (modifiers & KITTYKB_MOD_SHIFT) != 0u;
        if (ktech_editor_duplicate_pattern(app->editor, linked, NULL)) {
            if (!linked) request_publication(app, false);
            set_message(app, linked ? "Linked pattern reference retained" :
                                      "Independent pattern copy created");
        } else {
            set_message(app, "Create a reusable pattern with Ctrl+P first");
        }
        return;
    }
    if ((modifiers & KITTYKB_MOD_ALT) != 0u && letter == (uint32_t)'a') {
        if (ktech_editor_append_arrangement(app->editor, 1u))
            set_message(app, "Scene appended to arrangement");
        else
            set_message(app, "Arrangement is full or at 256 bars");
        return;
    }
    if ((modifiers & KITTYKB_MOD_ALT) != 0u &&
        key->key == KITTYKB_KEY_ENTER) {
        const ktech_project *project =
            ktech_editor_project_const(app->editor);
        char error[128];
        if (ktech_audio_publish_arrangement(
                app->audio, project,
                (ktech_launch_quantization)project->launch_quantization,
                true, error, sizeof error))
            set_message(app, "Arrangement queued for playback");
        else
            set_message(app, error);
        return;
    }
    if ((modifiers & KITTYKB_MOD_SHIFT) != 0u &&
        (key->key == KITTYKB_KEY_LEFT || key->key == KITTYKB_KEY_RIGHT ||
         key->key == KITTYKB_KEY_UP || key->key == KITTYKB_KEY_DOWN)) {
        ktech_step_range range;
        if (!ktech_editor_range(app->editor, &range)) {
            uint8_t track = ktech_editor_track(app->editor);
            uint8_t step = ktech_editor_step(app->editor);
            range = (ktech_step_range){track, track, step, step};
        }
        if (key->key == KITTYKB_KEY_LEFT && range.first_step > 0u)
            --range.first_step;
        else if (key->key == KITTYKB_KEY_RIGHT &&
                 range.last_step + 1u < KTECH_STEP_COUNT)
            ++range.last_step;
        else if (key->key == KITTYKB_KEY_UP && range.first_track > 0u)
            --range.first_track;
        else if (key->key == KITTYKB_KEY_DOWN &&
                 range.last_track + 1u < KTECH_TRACK_COUNT)
            ++range.last_track;
        (void)ktech_editor_select_range(app->editor,
                                        range.first_track,
                                        range.first_step,
                                        range.last_track,
                                        range.last_step);
        return;
    }
    if ((modifiers & KITTYKB_MOD_ALT) != 0u) {
        bool changed = false;
        const char *message = "Selection tool applied";
        if (letter == (uint32_t)'f')
            changed = ktech_editor_apply_tool(app->editor, KTECH_TOOL_FILL);
        else if (letter == (uint32_t)'c')
            changed = ktech_editor_apply_tool(app->editor, KTECH_TOOL_CLEAR);
        else if (letter == (uint32_t)'i')
            changed = ktech_editor_apply_tool(app->editor, KTECH_TOOL_INVERT);
        else if (letter == (uint32_t)'m')
            changed = ktech_editor_apply_tool(app->editor, KTECH_TOOL_MIRROR);
        else if (key->key == KITTYKB_KEY_LEFT)
            changed = ktech_editor_apply_tool(app->editor,
                                              KTECH_TOOL_ROTATE_LEFT);
        else if (key->key == KITTYKB_KEY_RIGHT)
            changed = ktech_editor_apply_tool(app->editor,
                                              KTECH_TOOL_ROTATE_RIGHT);
        else
            message = NULL;
        if (message) {
            if (changed) request_publication(app, false);
            set_message(app, changed ? message : "Select tiles first");
            return;
        }
    }
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

static void pointer_gesture_finish(app_state *app)
{
    if (!app) return;
    if (app->pointer_paint_started)
        ktech_editor_end_step_paint(app->editor);
    app->pointer_drag_active = false;
    app->pointer_select_active = false;
    app->pointer_piano_active = false;
    app->pointer_drag_right = false;
    app->pointer_paint_started = false;
    app->pointer_right_pending = false;
}

static void context_menu_close(app_state *app)
{
    if (!app) return;
    app->context_menu.page = KTECH_UI_CONTEXT_CLOSED;
}

static void context_menu_open(app_state *app, uint8_t track, int x, int y)
{
    if (!app || track >= KTECH_TRACK_COUNT) return;
    (void)ktech_editor_select_track(app->editor, track);
    app->context_menu.page = KTECH_UI_CONTEXT_ACTIONS;
    app->context_menu.track = track;
    app->context_menu.group = 0u;
    app->context_menu.selected = 0u;
    app->context_menu.query[0] = '\0';
    app->context_menu.query_length = 0u;
    ktech_audio_status status;
    ktech_audio_get_status(app->audio, &status);
    app->context_menu.installed_banks = status.soundbank_mask;
    app->context_menu.anchor_x = x;
    app->context_menu.anchor_y = y;
    set_message(app, "Track options open; press / to search instruments");
}

static bool pointer_step_set(app_state *app, uint8_t track, uint8_t step,
                             bool enabled)
{
    if (!app) return false;
    uint64_t revision = ktech_editor_revision(app->editor);
    bool valid = ktech_editor_set_step_enabled(app->editor, track, step,
                                               enabled);
    if (valid && ktech_editor_revision(app->editor) != revision)
        request_publication(app, false);
    return valid;
}

static bool pointer_piano_set(app_state *app, uint8_t step, uint8_t note,
                              bool enabled)
{
    if (!app) return false;
    uint8_t track = ktech_editor_track(app->editor);
    uint64_t revision = ktech_editor_revision(app->editor);
    bool valid = enabled ?
        ktech_editor_set_step_note(app->editor, track, step, note) : true;
    if (valid)
        valid = ktech_editor_set_step_enabled(app->editor, track, step,
                                              enabled);
    if (valid && ktech_editor_revision(app->editor) != revision)
        request_publication(app, false);
    return valid;
}

static bool pointer_piano_begin(app_state *app, uint8_t step, uint8_t note,
                                uint8_t button, uint32_t modifiers)
{
    if (!app || (button != KTECH_MOUSE_LEFT &&
                 button != KTECH_MOUSE_RIGHT)) return false;
    pointer_gesture_finish(app);
    context_menu_close(app);
    app->pointer_drag_active = true;
    app->pointer_piano_active = true;
    app->pointer_drag_right = button == KTECH_MOUSE_RIGHT;
    app->pointer_drag_track = ktech_editor_track(app->editor);
    app->pointer_drag_step = step;
    app->pointer_press_track = app->pointer_drag_track;
    app->pointer_press_step = step;
    if (button == KTECH_MOUSE_LEFT &&
        (modifiers & KITTYKB_MOD_SHIFT) != 0u) {
        app->pointer_select_active = true;
        return ktech_editor_select_range(app->editor,
                                         app->pointer_drag_track, step,
                                         app->pointer_drag_track, step);
    }
    ktech_editor_begin_step_paint(app->editor);
    app->pointer_paint_started = true;
    return pointer_piano_set(app, step, note,
                             button == KTECH_MOUSE_LEFT);
}

static void pointer_piano_drag(app_state *app, uint8_t step, uint8_t note)
{
    if (!app || !app->pointer_drag_active || !app->pointer_piano_active ||
        step == app->pointer_drag_step) return;
    if (app->pointer_select_active) {
        (void)ktech_editor_select_range(app->editor,
                                        app->pointer_press_track,
                                        app->pointer_press_step,
                                        app->pointer_press_track, step);
    } else {
        (void)pointer_piano_set(app, step, note,
                               !app->pointer_drag_right);
    }
    app->pointer_drag_step = step;
}

static bool pixel_hit_piano(const app_state *app, int width, int height,
                            int x, int y, uint8_t *step, uint8_t *note)
{
    if (!app || ktech_editor_view_current(app->editor) !=
                    KTECH_VIEW_PIANO_ROLL) return false;
    const ktech_project *project = ktech_editor_project_const(app->editor);
    uint8_t track_index = ktech_editor_track(app->editor);
    ktech_track scratch;
    const ktech_track *track = ktech_project_resolve_track(
        project, ktech_editor_scene(app->editor), track_index, &scratch);
    if (!track) return false;
    uint8_t selected = ktech_editor_step(app->editor);
    uint8_t center = track->steps[selected].note;
    uint8_t low = center > 5u ? (uint8_t)(center - 5u) : 0u;
    if (low > 116u) low = 116u;
    int columns = width >= 900 ? 16 : 8;
    int page = selected / columns * columns;
    int left = 72;
    int top = 74;
    int cell_width = (width - left - 18) / columns;
    int cell_height = (height - top - 86) / 12;
    if (cell_height < 18) cell_height = 18;
    if (cell_width <= 0 || x < left ||
        x >= left + columns * cell_width || y < top ||
        y >= top + 12 * cell_height) return false;
    int found_step = page + (x - left) / cell_width;
    int found_note = (int)low + 11 - (y - top) / cell_height;
    if (found_step < 0 || found_step >= track->length ||
        found_note < 0 || found_note > 127) return false;
    if (step) *step = (uint8_t)found_step;
    if (note) *note = (uint8_t)found_note;
    return true;
}

static bool pointer_step_begin(app_state *app, uint8_t track, uint8_t step,
                               uint8_t button, uint32_t modifiers,
                               int x, int y)
{
    if (!app || (button != KTECH_MOUSE_LEFT &&
                 button != KTECH_MOUSE_RIGHT) || track >= KTECH_TRACK_COUNT)
        return false;
    const ktech_project *project = ktech_editor_project_const(app->editor);
    const ktech_track *selected =
        &project->scenes[ktech_editor_scene(app->editor)].tracks[track];
    if (step >= selected->length) return false;
    pointer_gesture_finish(app);
    context_menu_close(app);
    app->pointer_drag_active = true;
    app->pointer_drag_right = button == KTECH_MOUSE_RIGHT;
    app->pointer_drag_track = track;
    app->pointer_drag_step = step;
    app->pointer_press_track = track;
    app->pointer_press_step = step;
    app->pointer_press_x = x;
    app->pointer_press_y = y;
    if (button == KTECH_MOUSE_LEFT &&
        (modifiers & KITTYKB_MOD_SHIFT) != 0u) {
        app->pointer_select_active = true;
        app->pointer_drag_enabled = false;
        return ktech_editor_select_range(app->editor, track, step,
                                         track, step);
    }
    if (app->pointer_drag_right) {
        app->pointer_drag_enabled = false;
        app->pointer_right_pending = true;
        return ktech_editor_select_step(app->editor, track, step, false);
    }
    app->pointer_drag_enabled = !selected->steps[step].enabled;
    ktech_editor_begin_step_paint(app->editor);
    app->pointer_paint_started = true;
    return pointer_step_set(app, track, step, app->pointer_drag_enabled);
}

static void pointer_step_drag(app_state *app, uint8_t track, uint8_t step)
{
    if (!app || !app->pointer_drag_active ||
        (track == app->pointer_drag_track &&
         step == app->pointer_drag_step))
        return;
    if (app->pointer_select_active) {
        (void)ktech_editor_select_range(app->editor,
                                        app->pointer_press_track,
                                        app->pointer_press_step,
                                        track, step);
        app->pointer_drag_track = track;
        app->pointer_drag_step = step;
        return;
    }
    if (app->pointer_drag_right && !app->pointer_paint_started) {
        ktech_editor_begin_step_paint(app->editor);
        app->pointer_paint_started = true;
        (void)pointer_step_set(app, app->pointer_press_track,
                               app->pointer_press_step, false);
    }
    app->pointer_right_pending = false;
    int start_track = app->pointer_drag_track;
    int start_step = app->pointer_drag_step;
    int track_delta = (int)track - start_track;
    int step_delta = (int)step - start_step;
    int track_distance = abs(track_delta);
    int step_distance = abs(step_delta);
    int distance = track_distance > step_distance ? track_distance :
                                                   step_distance;
    for (int offset = 1; offset <= distance; ++offset) {
        int painted_track = start_track + track_delta * offset / distance;
        int painted_step = start_step + step_delta * offset / distance;
        (void)pointer_step_set(app, (uint8_t)painted_track,
                              (uint8_t)painted_step,
                              app->pointer_drag_enabled);
    }
    app->pointer_drag_track = track;
    app->pointer_drag_step = step;
}

static void pointer_motion_outside_tile(app_state *app, int x, int y)
{
    if (!app || !app->pointer_right_pending) return;
    if (abs(x - app->pointer_press_x) > 3 ||
        abs(y - app->pointer_press_y) > 3)
        app->pointer_right_pending = false;
}

static void pointer_release(app_state *app, uint8_t button)
{
    if (!app) return;
    bool open = button == KTECH_MOUSE_RIGHT && app->pointer_right_pending;
    uint8_t track = app->pointer_press_track;
    int x = app->pointer_press_x;
    int y = app->pointer_press_y;
    pointer_gesture_finish(app);
    if (open) context_menu_open(app, track, x, y);
}

static void pointer_track_begin(app_state *app, uint8_t track,
                                uint8_t button, int x, int y)
{
    if (!app || track >= KTECH_TRACK_COUNT) return;
    pointer_gesture_finish(app);
    context_menu_close(app);
    if (button == KTECH_MOUSE_LEFT) {
        (void)ktech_editor_select_track(app->editor, track);
    } else if (button == KTECH_MOUSE_RIGHT) {
        (void)ktech_editor_select_track(app->editor, track);
        app->pointer_right_pending = true;
        app->pointer_press_track = track;
        app->pointer_press_x = x;
        app->pointer_press_y = y;
    }
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

static void context_select_role(app_state *app, uint8_t track,
                                ktech_role role)
{
    uint64_t revision = ktech_editor_revision(app->editor);
    if (ktech_editor_set_track_role(app->editor, track, role) &&
        ktech_editor_revision(app->editor) != revision)
        request_publication(app, false);
    char message[160];
    (void)snprintf(message, sizeof message, "Track %u instrument: %s",
                   (unsigned)track + 1u, ktech_role_name(role));
    set_message(app, message);
    int bank = ktech_soundbank_role_bank_index(role);
    bool installed = bank < 0 ||
        (app->context_menu.installed_banks &
         (UINT32_C(1) << (unsigned)bank)) != 0u;
    if (!installed && !ktech_lazy_install_running(&app->lazy_install)) {
        char install_error[128];
        if (ktech_lazy_install_start(&app->lazy_install, role,
                                      install_error,
                                      sizeof install_error)) {
            (void)snprintf(message, sizeof message,
                           "Installing %s; native fallback remains audible",
                           app->lazy_install.package);
            set_message(app, message);
        } else {
            set_message(app, install_error);
        }
    }
    context_menu_close(app);
}

static void context_menu_activate(app_state *app,
                                  const ktech_cli_options *cli,
                                  uint8_t item)
{
    if (!app || app->context_menu.page == KTECH_UI_CONTEXT_CLOSED)
        return;
    size_t count = ktech_ui_context_item_count(&app->context_menu);
    if ((size_t)item >= count) return;
    uint8_t track = app->context_menu.track;
    if (app->context_menu.page == KTECH_UI_CONTEXT_ACTIONS) {
        if (item == 0u) {
            app->context_menu.page = KTECH_UI_CONTEXT_GROUPS;
            app->context_menu.selected = 0u;
            return;
        }
        if (item == 1u) {
            pointer_track(app, cli, track, KTECH_MOUSE_RIGHT);
            const ktech_project *project =
                ktech_editor_project_const(app->editor);
            bool muted = project->scenes[ktech_editor_scene(app->editor)]
                             .tracks[track].mute != 0u;
            set_message(app, muted ? "Track muted" : "Track unmuted");
        } else if (item == 2u) {
            (void)ktech_editor_select_track(app->editor, track);
            kittyin_action_event action = {
                KTECH_ACTION_SOLO, KITTYIN_ACTION_PRESS,
                KITTYIN_DEVICE_KEYBOARD, 0u
            };
            dispatch_action(app, cli, &action);
            const ktech_project *project =
                ktech_editor_project_const(app->editor);
            bool solo = project->scenes[ktech_editor_scene(app->editor)]
                            .tracks[track].solo != 0u;
            set_message(app, solo ? "Track soloed" : "Track unsoloed");
        }
        context_menu_close(app);
    } else if (app->context_menu.page == KTECH_UI_CONTEXT_GROUPS) {
        if (item < KTECH_UI_INSTRUMENT_GROUP_COUNT) {
            app->context_menu.group = item;
            app->context_menu.page = KTECH_UI_CONTEXT_ROLES;
            app->context_menu.selected = 0u;
        } else {
            app->context_menu.page = KTECH_UI_CONTEXT_ACTIONS;
            app->context_menu.selected = 0u;
        }
    } else if (app->context_menu.page == KTECH_UI_CONTEXT_ROLES ||
               app->context_menu.page == KTECH_UI_CONTEXT_SEARCH) {
        ktech_role role;
        if (ktech_ui_context_role(&app->context_menu, item, &role)) {
            context_select_role(app, track, role);
        } else {
            app->context_menu.page = KTECH_UI_CONTEXT_GROUPS;
            app->context_menu.query[0] = '\0';
            app->context_menu.query_length = 0u;
            app->context_menu.selected = 0u;
        }
    }
}

static bool context_menu_key(app_state *app, const ktech_cli_options *cli,
                             const kittykb_event *key)
{
    if (!app || !key || app->context_menu.page == KTECH_UI_CONTEXT_CLOSED ||
        key->action == KITTYKB_ACTION_RELEASE)
        return false;
    size_t count = ktech_ui_context_item_count(&app->context_menu);
    if (count == 0u) return false;
    bool searching = app->context_menu.page == KTECH_UI_CONTEXT_SEARCH;
    if (key->key == KITTYKB_KEY_ESCAPE ||
        (!searching && key->key == (uint32_t)'q')) {
        context_menu_close(app);
        set_message(app, "Track options closed");
    } else if (!searching && key->key == (uint32_t)'/') {
        app->context_menu.page = KTECH_UI_CONTEXT_SEARCH;
        app->context_menu.query[0] = '\0';
        app->context_menu.query_length = 0u;
        app->context_menu.selected = 0u;
        set_message(app, "Type part of an instrument name; Enter selects");
    } else if (searching && key->key == KITTYKB_KEY_BACKSPACE) {
        if (app->context_menu.query_length > 0u) {
            --app->context_menu.query_length;
            app->context_menu.query[app->context_menu.query_length] = '\0';
        }
        app->context_menu.selected = 0u;
    } else if (key->key == KITTYKB_KEY_UP) {
        app->context_menu.selected = app->context_menu.selected == 0u ?
            (uint8_t)(count - 1u) :
            (uint8_t)(app->context_menu.selected - 1u);
    } else if (key->key == KITTYKB_KEY_DOWN) {
        app->context_menu.selected = (uint8_t)(
            (app->context_menu.selected + 1u) % count);
    } else if (key->key == (uint32_t)' ' &&
               (app->context_menu.page == KTECH_UI_CONTEXT_ROLES ||
                app->context_menu.page == KTECH_UI_CONTEXT_SEARCH)) {
        ktech_role role;
        if (ktech_ui_context_role(&app->context_menu,
                                  app->context_menu.selected, &role)) {
            const ktech_project *project =
                ktech_editor_project_const(app->editor);
            const ktech_track *track =
                &project->scenes[ktech_editor_scene(app->editor)]
                    .tracks[app->context_menu.track];
            uint8_t step = ktech_editor_step(app->editor);
            uint8_t note = step < track->length ? track->steps[step].note :
                                                 60u;
            char error[128];
            if (ktech_audio_audition(app->audio, role, note, error,
                                     sizeof error)) {
                char message[128];
                (void)snprintf(message, sizeof message, "Preview: %s",
                               ktech_role_name(role));
                set_message(app, message);
            } else {
                set_message(app, error);
            }
        }
    } else if ((key->key == (uint32_t)'f' ||
                key->key == (uint32_t)'F') &&
               (app->context_menu.page == KTECH_UI_CONTEXT_ROLES ||
                app->context_menu.page == KTECH_UI_CONTEXT_SEARCH)) {
        ktech_role role;
        if (ktech_ui_context_role(&app->context_menu,
                                  app->context_menu.selected, &role)) {
            bool favorite = !ktech_editor_favorite(app->editor, role);
            (void)ktech_editor_set_favorite(app->editor, role, favorite);
            set_message(app, favorite ? "Instrument added to favorites" :
                                        "Instrument removed from favorites");
        }
    } else if (key->key == KITTYKB_KEY_ENTER) {
        context_menu_activate(app, cli, app->context_menu.selected);
    } else if (key->key == KITTYKB_KEY_LEFT) {
        if (app->context_menu.page == KTECH_UI_CONTEXT_ROLES ||
            app->context_menu.page == KTECH_UI_CONTEXT_SEARCH)
            app->context_menu.page = KTECH_UI_CONTEXT_GROUPS;
        else if (app->context_menu.page == KTECH_UI_CONTEXT_GROUPS)
            app->context_menu.page = KTECH_UI_CONTEXT_ACTIONS;
        app->context_menu.selected = 0u;
    } else if (key->key == KITTYKB_KEY_RIGHT) {
        context_menu_activate(app, cli, app->context_menu.selected);
    } else if (searching && key->modifiers == 0u &&
               key->key >= 0x20u && key->key <= 0x7eu &&
               app->context_menu.query_length + 1u <
                   sizeof app->context_menu.query) {
        app->context_menu.query[app->context_menu.query_length++] =
            (char)key->key;
        app->context_menu.query[app->context_menu.query_length] = '\0';
        app->context_menu.selected = 0u;
    } else {
        return false;
    }
    return true;
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
    int install_status = 0;
    ktech_lazy_install_result install_result =
        ktech_lazy_install_poll(&app->lazy_install, &install_status);
    if (install_result == KTECH_LAZY_SUCCEEDED)
        set_message(app, "Soundbank installed; restart audio to load it");
    else if (install_result == KTECH_LAZY_FAILED) {
        char message[96];
        (void)snprintf(message, sizeof message,
                       "Soundbank install failed (status %d)", install_status);
        set_message(app, message);
    }
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

static void pixel_pointer_coordinates(const kittyts_session *session,
                                      const kittyin_mouse_event *mouse,
                                      int *x, int *y)
{
    if (mouse->pixel_coordinates) {
        *x = mouse->x - kittyts_origin_x(session);
        *y = mouse->y - kittyts_origin_y(session);
    } else {
        *x = mouse->x * kittyts_cell_width(session) -
             kittyts_origin_x(session);
        *y = mouse->y * kittyts_cell_height(session) -
             kittyts_origin_y(session);
    }
}

static void handle_pixel_event(app_state *app, const ktech_cli_options *cli,
                               kittyts_session *session,
                               const kittyin_event *event)
{
    if (event->kind == KITTYIN_EVENT_KEY) {
        pointer_gesture_finish(app);
        if (context_menu_key(app, cli, &event->data.key))
            return;
        dispatch_key(app, cli, &event->data.key);
    } else if (event->kind == KITTYIN_EVENT_MOUSE) {
        const kittyin_mouse_event *mouse = &event->data.mouse;
        int x = 0;
        int y = 0;
        if (mouse->action != KITTYIN_MOUSE_WHEEL)
            pixel_pointer_coordinates(session, mouse, &x, &y);
        if (app->context_menu.page != KTECH_UI_CONTEXT_CLOSED) {
            if (mouse->action == KITTYIN_MOUSE_PRESS) {
                if (mouse->button == KTECH_MOUSE_LEFT) {
                    sr_canvas dimensions = {0};
                    dimensions.w = kittyts_width(session);
                    dimensions.h = kittyts_height(session);
                    uint8_t item;
                    if (ktech_ui_hit_context_menu(
                            &dimensions, &app->context_menu, x, y, &item))
                        context_menu_activate(app, cli, item);
                    else
                        context_menu_close(app);
                } else if (mouse->button == KTECH_MOUSE_RIGHT) {
                    context_menu_close(app);
                }
            }
            return;
        }
        if (mouse->action == KITTYIN_MOUSE_WHEEL) {
            pointer_gesture_finish(app);
            kittyin_action_event action = {
                mouse->wheel_y > 0 ? KTECH_ACTION_DECREASE :
                                     KTECH_ACTION_INCREASE,
                KITTYIN_ACTION_PRESS, KITTYIN_DEVICE_KEYBOARD, 0u
            };
            dispatch_action(app, cli, &action);
        } else if (mouse->action == KITTYIN_MOUSE_RELEASE) {
            pointer_release(app, mouse->button);
        } else if (mouse->action == KITTYIN_MOUSE_MOVE) {
            uint8_t piano_step, piano_note;
            if (pixel_hit_piano(app, kittyts_width(session),
                                kittyts_height(session), x, y,
                                &piano_step, &piano_note) &&
                app->pointer_drag_active) {
                pointer_piano_drag(app, piano_step, piano_note);
                return;
            }
            if (ktech_editor_view_current(app->editor) != KTECH_VIEW_STEPS) {
                pointer_motion_outside_tile(app, x, y);
                return;
            }
            ktech_ui_layout layout;
            ktech_ui_layout_make(&layout, kittyts_width(session),
                                  kittyts_height(session), app->editor);
            uint8_t track, step;
            if (ktech_ui_hit_step(&layout, x, y, &track, &step) &&
                app->pointer_drag_active)
                pointer_step_drag(app, track, step);
            else
                pointer_motion_outside_tile(app, x, y);
        } else if (mouse->action == KITTYIN_MOUSE_PRESS &&
                   (mouse->button == KTECH_MOUSE_LEFT ||
                    mouse->button == KTECH_MOUSE_RIGHT)) {
            pointer_gesture_finish(app);
            uint8_t piano_step, piano_note;
            if (pixel_hit_piano(app, kittyts_width(session),
                                kittyts_height(session), x, y,
                                &piano_step, &piano_note)) {
                (void)pointer_piano_begin(app, piano_step, piano_note,
                                          mouse->button,
                                          mouse->modifiers);
                return;
            }
            if (ktech_editor_view_current(app->editor) != KTECH_VIEW_STEPS)
                return;
            ktech_ui_layout layout;
            ktech_ui_layout_make(&layout, kittyts_width(session),
                                  kittyts_height(session), app->editor);
            uint8_t track, step;
            if (ktech_ui_hit_step(&layout, x, y, &track, &step)) {
                (void)pointer_step_begin(app, track, step, mouse->button,
                                         mouse->modifiers,
                                         x, y);
            } else if (ktech_ui_hit_track(&layout, x, y, &track)) {
                pointer_track_begin(app, track, mouse->button, x, y);
            } else {
                uint8_t scene;
                if (ktech_ui_hit_scene(&layout, x, y, &scene))
                    pointer_scene(app, cli, scene, mouse->button);
            }
        }
    } else if (event->kind == KITTYIN_EVENT_FOCUS &&
               !event->data.focus.focused) {
        pointer_gesture_finish(app);
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
    options.mouse_tracking = KITTYIN_MOUSE_TRACKING_DRAG;
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
        if (signal_suspend) {
            pointer_gesture_finish(app);
            if (!resume_pixel(&session, &options)) {
                failed = true;
                break;
            }
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
        ktech_ui_render_context_menu(&canvas, app->editor,
                                     &app->context_menu);
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

static bool cell_hit_step(const app_state *app,
                          const kittyin_mouse_event *mouse, int columns,
                          uint8_t *track, uint8_t *step)
{
    int visible = columns >= 100 ? 16 : columns >= 72 ? 12 : 8;
    int label_width = columns >= 100 ? 19 : columns >= 72 ? 14 : 11;
    int row = mouse->y - 2;
    int column = (mouse->x - label_width) / 3;
    if (row < 0 || row >= (int)KTECH_TRACK_COUNT ||
        mouse->x < label_width || column < 0 || column >= visible)
        return false;
    int page = ktech_editor_step(app->editor) / visible * visible;
    int found_step = page + column;
    if (found_step < 0 || found_step >= (int)KTECH_STEP_COUNT) return false;
    if (track) *track = (uint8_t)row;
    if (step) *step = (uint8_t)found_step;
    return true;
}

static bool cell_hit_piano(const app_state *app,
                           const kittyin_mouse_event *mouse, int columns,
                           uint8_t *step, uint8_t *note)
{
    if (!app || !mouse || ktech_editor_view_current(app->editor) !=
                            KTECH_VIEW_PIANO_ROLL) return false;
    int visible = columns >= 100 ? 16 : columns >= 72 ? 12 : 8;
    int column = (mouse->x - 6) / 3;
    int row = mouse->y - 2;
    if (mouse->x < 6 || column < 0 || column >= visible ||
        row < 0 || row >= 12) return false;
    const ktech_project *project = ktech_editor_project_const(app->editor);
    uint8_t track_index = ktech_editor_track(app->editor);
    ktech_track scratch;
    const ktech_track *track = ktech_project_resolve_track(
        project, ktech_editor_scene(app->editor), track_index, &scratch);
    if (!track) return false;
    uint8_t selected = ktech_editor_step(app->editor);
    int page = selected / visible * visible;
    int found_step = page + column;
    uint8_t center = track->steps[selected].note;
    uint8_t low = center > 5u ? (uint8_t)(center - 5u) : 0u;
    if (low > 116u) low = 116u;
    if (found_step < 0 || found_step >= track->length) return false;
    if (step) *step = (uint8_t)found_step;
    if (note) *note = (uint8_t)((int)low + 11 - row);
    return true;
}

static void handle_cell_mouse(app_state *app, const ktech_cli_options *cli,
                              const kittyin_mouse_event *mouse, int columns)
{
    if (app->context_menu.page != KTECH_UI_CONTEXT_CLOSED) {
        if (mouse->action == KITTYIN_MOUSE_PRESS) {
            if (mouse->button == KTECH_MOUSE_LEFT) {
                uint8_t item;
                if (ktech_cell_hit_context_menu(&app->context_menu,
                                                mouse->x, mouse->y,
                                                columns, &item))
                    context_menu_activate(app, cli, item);
                else
                    context_menu_close(app);
            } else if (mouse->button == KTECH_MOUSE_RIGHT) {
                context_menu_close(app);
            }
        }
        return;
    }
    if (mouse->action == KITTYIN_MOUSE_WHEEL) {
        pointer_gesture_finish(app);
        kittyin_action_event action = {
            mouse->wheel_y > 0 ? KTECH_ACTION_DECREASE : KTECH_ACTION_INCREASE,
            KITTYIN_ACTION_PRESS, KITTYIN_DEVICE_KEYBOARD, 0u
        };
        dispatch_action(app, cli, &action);
        return;
    }
    if (mouse->pixel_coordinates) return;
    if (mouse->action == KITTYIN_MOUSE_RELEASE) {
        pointer_release(app, mouse->button);
        return;
    }
    uint8_t track;
    uint8_t step;
    uint8_t piano_note;
    if (mouse->action == KITTYIN_MOUSE_MOVE) {
        if (cell_hit_piano(app, mouse, columns, &step, &piano_note) &&
            app->pointer_drag_active) {
            pointer_piano_drag(app, step, piano_note);
            return;
        }
        if (ktech_editor_view_current(app->editor) != KTECH_VIEW_STEPS) {
            pointer_motion_outside_tile(app, mouse->x, mouse->y);
            return;
        }
        if (cell_hit_step(app, mouse, columns, &track, &step) &&
            app->pointer_drag_active)
            pointer_step_drag(app, track, step);
        else
            pointer_motion_outside_tile(app, mouse->x, mouse->y);
        return;
    }
    if (mouse->action != KITTYIN_MOUSE_PRESS)
        return;
    if (mouse->button != KTECH_MOUSE_LEFT &&
        mouse->button != KTECH_MOUSE_RIGHT)
        return;
    pointer_gesture_finish(app);
    if (cell_hit_piano(app, mouse, columns, &step, &piano_note)) {
        (void)pointer_piano_begin(app, step, piano_note, mouse->button,
                                  mouse->modifiers);
        return;
    }
    if (ktech_editor_view_current(app->editor) != KTECH_VIEW_STEPS) return;
    int label_width = columns >= 100 ? 19 : columns >= 72 ? 14 : 11;
    int row = mouse->y - 2;
    if (row >= 0 && row < (int)KTECH_TRACK_COUNT &&
        mouse->x >= 0 && mouse->x < label_width) {
        pointer_track_begin(app, (uint8_t)row, mouse->button,
                            mouse->x, mouse->y);
        return;
    }
    if (cell_hit_step(app, mouse, columns, &track, &step)) {
        (void)pointer_step_begin(app, track, step, mouse->button,
                                 mouse->modifiers,
                                 mouse->x, mouse->y);
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
    options.mouse_tracking = KITTYIN_MOUSE_TRACKING_DRAG;
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
        if (signal_suspend) {
            pointer_gesture_finish(app);
            if (!resume_cell(&terminal, &options)) {
                failed = true;
                break;
            }
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
            if (event.kind == KITTYIN_EVENT_KEY) {
                pointer_gesture_finish(app);
                if (!context_menu_key(app, cli, &event.data.key))
                    dispatch_key(app, cli, &event.data.key);
            } else if (event.kind == KITTYIN_EVENT_MOUSE) {
                handle_cell_mouse(app, cli, &event.data.mouse, columns);
            } else if (event.kind == KITTYIN_EVENT_FOCUS &&
                       !event.data.focus.focused) {
                pointer_gesture_finish(app);
            }
        }
        poll_gamepads(app, cli, elapsed);
        periodic_work(app);
        ktech_audio_status status;
        ktech_audio_get_status(app->audio, &status);
        char screen[16384];
        if (app->context_menu.page != KTECH_UI_CONTEXT_CLOSED)
            (void)ktech_cell_render_context_menu(
                screen, sizeof screen, columns, app->editor,
                &app->context_menu);
        else
            (void)ktech_cell_render(screen, sizeof screen, columns,
                                    app->editor, &status, app->message);
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
    (void)printf("Kilix Techno 0.2.1\nproject=%s\nname=%s\ntempo=%u\nscene=%u\n",
                 path ? path : "new", project->name, project->tempo,
                 (unsigned)project->active_scene + 1u);
    return 0;
}

static const ktech_export_preset *find_export_preset(
    const ktech_project *project, const char *name)
{
    if (!project || !name) return NULL;
    for (uint8_t index = 0u; index < project->export_preset_count; ++index)
        if (strcmp(project->export_presets[index].name, name) == 0)
            return &project->export_presets[index];
    return NULL;
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
    char error[256];
    if (options->operation == KTECH_CLI_IMPORT_MIDI) {
        if (!ktech_midi_load(&project, input, error, sizeof error)) {
            (void)fprintf(stderr, "MIDI import: %s\n", error);
            return 1;
        }
        ktech_project_result saved = ktech_project_save(&project, output,
                                                        false);
        if (saved != KTECH_PROJECT_OK) {
            (void)fprintf(stderr, "save: %s\n",
                          ktech_project_result_name(saved));
            return 1;
        }
        (void)printf("imported MIDI to %s\n", output);
        return 0;
    }
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
    ktech_export_format format = KTECH_EXPORT_WAV;
    if (options->operation == KTECH_CLI_EXPORT_ARRANGEMENT)
        export_options.arrangement = true;
    else if (options->operation == KTECH_CLI_EXPORT_STEMS)
        format = KTECH_EXPORT_STEMS;
    else if (options->operation == KTECH_CLI_EXPORT_MIDI)
        format = KTECH_EXPORT_MIDI;
    else if (options->operation == KTECH_CLI_EXPORT_PRESET) {
        const ktech_export_preset *preset = find_export_preset(
            &project, options->preset_name);
        if (!preset) {
            (void)fprintf(stderr, "export preset not found: %s\n",
                          options->preset_name);
            return 1;
        }
        if (!ktech_export_options_apply_preset(&export_options, preset,
                                                error, sizeof error)) {
            (void)fprintf(stderr, "export preset: %s\n", error);
            return 1;
        }
        format = (ktech_export_format)preset->format;
    }
    if (format == KTECH_EXPORT_MIDI) {
        if (!ktech_midi_save(&project, output, false, error, sizeof error)) {
            (void)fprintf(stderr, "MIDI export: %s\n", error);
            return 1;
        }
        (void)printf("exported MIDI to %s\n", output);
        return 0;
    }
    if (format == KTECH_EXPORT_STEMS) {
        ktech_export_info stems[KTECH_TRACK_COUNT];
        if (!ktech_export_stems(&project, output, &export_options, stems,
                                error, sizeof error)) {
            (void)fprintf(stderr, "stem export: %s\n", error);
            return 1;
        }
        uint64_t total = 0u;
        for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track)
            total += stems[track].bytes;
        (void)printf("exported 8 stems (%llu bytes)\n",
                     (unsigned long long)total);
        return 0;
    }
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
    if (options->operation != KTECH_CLI_RUN) return run_export(options);

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
    app.autosave_owner = true;
    app.autosave_claim_pending = false;
    app.session_slot_written = false;
    app.autosave_lock_fd = -1;
    app.autosave_lock_path[0] = '\0';
    app.exit_save_failed = false;
    bool shared_present = false;
    (void)snprintf(app.session_slot, sizeof app.session_slot,
                   "autosave.session-%lld-%ld.state",
                   (long long)time(NULL), (long)getpid());
    {
        /* Own the shared slot only by claiming it: a session that loaded it,
         * or that found it absent, takes the owner lock; an --open session
         * has its own file and never owns the shared slot. When the lock is
         * held by another running session, this one is a guest. A session
         * whose corrupt slot could not be quarantined keeps the withheld-write
         * rule but claims the lock like any other owner. */
        char shared[4096];
        struct stat shared_status;
        bool candidate = false;
        if (ktech_project_autosave_slot_path(NULL, "autosave.owner",
                                             app.autosave_lock_path,
                                             sizeof app.autosave_lock_path) !=
            KTECH_PROJECT_OK)
            app.autosave_lock_path[0] = '\0';
        shared_present = ktech_project_autosave_path(NULL, shared,
                                                     sizeof shared) ==
                             KTECH_PROJECT_OK &&
                         lstat(shared, &shared_status) == 0;
        if (!options->open_path &&
            (strcmp(description, "autosave") == 0 || !shared_present ||
             !autosave_write_allowed))
            candidate = true;
        app.autosave_owner = candidate && claim_autosave_lock(&app);
    }
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
    if (recovery_warning[0]) {
        set_message(&app, recovery_warning);
    } else if (!app.autosave_owner) {
        char notice[160];
        (void)snprintf(notice, sizeof notice,
                       "%s; this session autosaves to %s",
                       shared_present && !options->open_path ?
                           "Autosave holds another project" :
                       options->open_path ?
                           "Opened project" :
                           "Another running session owns the autosave",
                       app.session_slot);
        set_message(&app, notice);
    }
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
    if (!app.autosave_owner && app.session_slot_written &&
        !ktech_editor_dirty(app.editor))
        (void)ktech_project_remove_autosave_slot(NULL, app.session_slot);
    release_autosave_lock(&app);
    if (app.exit_save_failed && ktech_editor_dirty(app.editor))
        (void)fprintf(stderr, "%s; unsaved changes were not written\n",
                      app.message);
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
