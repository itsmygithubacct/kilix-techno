#include "kilix_techno/compile.h"
#include "kilix_techno/project.h"
#include "kilix_techno/settings.h"
#include "kilix_techno/soundbank.h"
#include "kilix_techno/actions.h"
#include "kilix_techno/audio.h"
#include "kilix_techno/dsp.h"
#include "kilix_techno/export.h"
#include "kilix_techno/ui.h"
#include "kilix_techno/workstation.h"
#include "kilix_techno/midi.h"
#include "kilix_techno/resource.h"

#include "kilix_state.h"
#include "kitty_keyboard.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned checks;
static unsigned failures;

#define CHECK(condition) do {                                                \
    ++checks;                                                                \
    if (!(condition)) {                                                      \
        (void)fprintf(stderr, "%s:%d: check failed: %s\n",               \
                      __FILE__, __LINE__, #condition);                       \
        ++failures;                                                          \
    }                                                                        \
} while (0)

static void write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void repair_project_crc(uint8_t *bytes, size_t size)
{
    write_u32(bytes + 16u, kilixstate_crc32(bytes + 40u, size - 40u));
}

static void test_project_codec(void)
{
    ktech_project original;
    ktech_project decoded;
    uint8_t bytes[KTECH_PROJECT_ENCODED_MAX];
    size_t size = 0u;

    ktech_project_init(&original, UINT32_C(0x12345678));
    CHECK(ktech_project_validate(&original, NULL, 0u));
    static const uint8_t curated_defaults[KTECH_TRACK_COUNT] = {
        KTECH_ROLE_ONGENET_KICK,
        KTECH_ROLE_ONGENET_SNARE,
        KTECH_ROLE_ONGENET_CLAP,
        KTECH_ROLE_ONGENET_CLOSED_HAT,
        KTECH_ROLE_ONGENET_OPEN_HAT,
        KTECH_ROLE_ONGENET_RIDE,
        KTECH_ROLE_SF2_JNS_SYNTH_BASS,
        KTECH_ROLE_VCSL_STRUMSTICK
    };
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track)
        CHECK(original.scenes[0].tracks[track].role ==
              curated_defaults[track]);
    CHECK(ktech_project_encode(&original, bytes, sizeof bytes, &size) ==
          KTECH_PROJECT_OK);
    CHECK(size > 50000u && size < sizeof bytes);
    memset(&decoded, 0xa5, sizeof decoded);
    CHECK(ktech_project_decode(&decoded, bytes, size) == KTECH_PROJECT_OK);
    CHECK(memcmp(&original, &decoded, sizeof original) == 0);

    ktech_project sentinel = decoded;
    bytes[size - 1u] ^= 1u;
    CHECK(ktech_project_decode(&decoded, bytes, size) == KTECH_PROJECT_CORRUPT);
    CHECK(memcmp(&decoded, &sentinel, sizeof decoded) == 0);
    bytes[size - 1u] ^= 1u;
    CHECK(ktech_project_decode(&decoded, bytes, size - 1u) != KTECH_PROJECT_OK);

    uint8_t attack[KTECH_PROJECT_ENCODED_MAX + 1u];
    memcpy(attack, bytes, size);
    attack[10] = (uint8_t)(KTECH_PROJECT_SCHEMA + 1u);
    CHECK(ktech_project_decode(&decoded, attack, size) ==
          KTECH_PROJECT_UNKNOWN_VERSION);

    ktech_project legacy;
    ktech_project_init(&legacy, UINT32_C(0x11111111));
    for (uint8_t scene = 0u; scene < KTECH_SCENE_COUNT; ++scene)
        for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track)
            CHECK(ktech_project_set_track_role(
                &legacy, scene, track, (ktech_role)track));
    CHECK(ktech_project_encode(&legacy, attack, sizeof attack, &size) ==
          KTECH_PROJECT_OK);
    size_t legacy_size = 40u +
        KTECH_SCENE_COUNT * KTECH_TRACK_COUNT *
            (8u + KTECH_STEP_COUNT * 6u) +
        (size_t)legacy.binding_count * 16u + 1u + strlen(legacy.name);
    attack[10] = 1u;
    write_u32(attack + 12u, (uint32_t)legacy_size);
    repair_project_crc(attack, legacy_size);
    CHECK(ktech_project_decode(&decoded, attack, legacy_size) ==
          KTECH_PROJECT_OK);
    CHECK(memcmp(&legacy, &decoded, sizeof legacy) == 0);

    CHECK(ktech_project_encode(&original, bytes, sizeof bytes, &size) ==
          KTECH_PROJECT_OK);
    memcpy(attack, bytes, size);
    attack[36] = 1u;
    CHECK(ktech_project_decode(&decoded, attack, size) ==
          KTECH_PROJECT_NONCANONICAL);
    memcpy(attack, bytes, size);
    attack[44] = 2u;
    repair_project_crc(attack, size);
    CHECK(ktech_project_decode(&decoded, attack, size) ==
          KTECH_PROJECT_NONCANONICAL);
    memcpy(attack, bytes, size);
    attack[40] = KTECH_ROLE_COUNT;
    repair_project_crc(attack, size);
    CHECK(ktech_project_decode(&decoded, attack, size) ==
          KTECH_PROJECT_INVALID);
    memcpy(attack, bytes, size);
    attack[34] = (uint8_t)(KTECH_BINDING_CAPACITY + 1u);
    attack[35] = 0u;
    CHECK(ktech_project_decode(&decoded, attack, size) ==
          KTECH_PROJECT_NONCANONICAL);
    memcpy(attack, bytes, size);
    const size_t binding_offset = 40u +
        KTECH_SCENE_COUNT * KTECH_TRACK_COUNT * (8u + KTECH_STEP_COUNT * 6u);
    attack[binding_offset + 10u] = 9u;
    repair_project_crc(attack, size);
    CHECK(ktech_project_decode(&decoded, attack, size) ==
          KTECH_PROJECT_INVALID);
    memcpy(attack, bytes, size);
    memcpy(attack + binding_offset + 16u, attack + binding_offset, 16u);
    repair_project_crc(attack, size);
    CHECK(ktech_project_decode(&decoded, attack, size) ==
          KTECH_PROJECT_INVALID);
    memcpy(attack, bytes, size);
    attack[40u + 8u + 16u * 6u] = 1u;
    repair_project_crc(attack, size);
    CHECK(ktech_project_decode(&decoded, attack, size) ==
          KTECH_PROJECT_INVALID);
    memcpy(attack, bytes, size);
    attack[size] = 0u;
    CHECK(ktech_project_decode(&decoded, attack, size + 1u) !=
          KTECH_PROJECT_OK);
    CHECK(ktech_project_decode(&decoded, bytes,
                               KTECH_PROJECT_MAX_BYTES + 1u) ==
          KTECH_PROJECT_TOO_LARGE);

    original.tempo = 19u;
    CHECK(!ktech_project_validate(&original, NULL, 0u));
    original.tempo = 126u;
    original.scenes[0].tracks[0].length = 1u;
    CHECK(!ktech_project_validate(&original, NULL, 0u));

    ktech_project named;
    ktech_project_init(&named, 1u);
    memset(named.name, 0, sizeof named.name);
    named.name[0] = 'C';
    named.name[1] = (char)UINT8_C(0xc3);
    named.name[2] = (char)UINT8_C(0xa9);
    CHECK(ktech_project_validate(&named, NULL, 0u));
    named.name[1] = (char)UINT8_C(0xc0);
    CHECK(!ktech_project_validate(&named, NULL, 0u));
    memset(named.name, 0, sizeof named.name);
    named.name[0] = 'A';
    named.name[2] = 'B';
    CHECK(!ktech_project_validate(&named, NULL, 0u));
}

static void test_workstation_project(void)
{
    ktech_project project;
    ktech_project decoded;
    ktech_project_init(&project, 77u);
    uint8_t first;
    CHECK(ktech_project_pattern_create(&project, 0u, 0u, "Main Kick",
                                       &first));
    CHECK(first == 0u && project.pattern_count == 1u);
    uint8_t linked = KTECH_PATTERN_NONE;
    CHECK(ktech_project_pattern_duplicate(&project, first, true, &linked));
    CHECK(linked == first && project.pattern_count == 1u);
    uint8_t copied = KTECH_PATTERN_NONE;
    CHECK(ktech_project_pattern_duplicate(&project, first, false, &copied));
    CHECK(copied == 1u && project.pattern_count == 2u);
    CHECK(ktech_project_pattern_assign(&project, 1u, 0u, copied));
    ktech_track scratch;
    const ktech_track *resolved = ktech_project_resolve_track(
        &project, 1u, 0u, &scratch);
    CHECK(resolved == &scratch && resolved->role == project.patterns[copied].role);
    CHECK(ktech_project_arrangement_add(&project, 0u, 2u, NULL));
    uint8_t references[KTECH_TRACK_COUNT];
    memset(references, KTECH_PATTERN_NONE, sizeof references);
    references[0] = copied;
    CHECK(ktech_project_arrangement_add(&project, 1u, 3u, references));
    CHECK(ktech_project_arrangement_bars(&project) == 5u);
    project.arrangement_queued = 1u;
    project.arrangement_audible = 0u;
    project.automation_lane_count = 1u;
    project.automation[0].target = KTECH_AUTOMATION_TRACK_LEVEL;
    project.automation[0].target_index = 0u;
    project.automation[0].point_count = 2u;
    project.automation[0].points[0] =
        (ktech_automation_point){0u, 7000};
    project.automation[0].points[1] =
        (ktech_automation_point){16u, 9000};
    project.macro_count = 1u;
    (void)snprintf(project.macros[0].name,
                   sizeof project.macros[0].name, "%s", "Energy");
    project.macros[0].value = 50u;
    project.macros[0].target_count = 1u;
    project.macros[0].targets[0] = (ktech_macro_target){
        KTECH_AUTOMATION_MASTER_DRIVE, 0u, 60, 0u
    };
    project.bus_count = 2u;
    (void)snprintf(project.buses[1].name, sizeof project.buses[1].name,
                   "%s", "DRUMS");
    project.buses[1].level = 90u;
    project.buses[1].pan = 128u;
    project.buses[1].effect_count = 1u;
    project.buses[1].effects[0] = (ktech_effect){
        KTECH_EFFECT_SATURATOR, 1u, 25u, 2u, {40u, 60u, 0u, 0u}
    };
    project.track_bus[0] = 1u;
    project.export_preset_count = 1u;
    (void)snprintf(project.export_presets[0].name,
                   sizeof project.export_presets[0].name, "%s", "Release");
    project.export_presets[0].format = KTECH_EXPORT_STEMS;
    project.export_presets[0].bit_depth = 24u;
    project.export_presets[0].normalize = 1u;
    project.export_presets[0].include_master = 1u;
    project.export_presets[0].bars = 5u;
    project.export_presets[0].tail_ms = 500u;
    project.resource_count = 1u;
    (void)snprintf(project.resources[0].path,
                   sizeof project.resources[0].path,
                   "%s", "samples/user-kick.ogg");
    (void)snprintf(project.resources[0].license,
                   sizeof project.resources[0].license, "%s", "CC0-1.0");
    memset(project.resources[0].sha256, 0x5a,
           sizeof project.resources[0].sha256);
    project.resources[0].bytes = 4096u;
    project.resources[0].format = KTECH_RESOURCE_OGG;
    project.resources[0].role = KTECH_ROLE_KICK;
    CHECK(ktech_project_validate(&project, NULL, 0u));
    CHECK(ktech_control_evaluate(&project, KTECH_AUTOMATION_TRACK_LEVEL,
                                 0u, 8u, 50) == 80);
    ktech_export_options preset_options;
    ktech_export_options_init(&preset_options);
    char preset_error[96];
    CHECK(ktech_export_options_apply_preset(
        &preset_options, &project.export_presets[0], preset_error,
        sizeof preset_error));
    CHECK(preset_options.bit_depth == 24u && preset_options.normalize &&
          preset_options.include_master && preset_options.bars == 5u &&
          preset_options.tail_ms == 500u);
    uint8_t *bytes = malloc(KTECH_PROJECT_ENCODED_MAX);
    CHECK(bytes != NULL);
    if (bytes) {
        size_t size = 0u;
        CHECK(ktech_project_encode(&project, bytes,
                                   KTECH_PROJECT_ENCODED_MAX, &size) ==
              KTECH_PROJECT_OK);
        CHECK(size > 50000u && size < KTECH_PROJECT_MAX_BYTES);
        CHECK(ktech_project_decode(&decoded, bytes, size) == KTECH_PROJECT_OK);
        CHECK(memcmp(&project, &decoded, sizeof project) == 0);
        free(bytes);
    }
    char error[128];
    ktech_compiled *song = ktech_compile_arrangement(&project, error,
                                                      sizeof error);
    CHECK(song != NULL);
    if (song) {
        CHECK(ktech_compiled_rows(song) == 80u);
        ktech_compiled_free(song);
    }
    ktech_editor *pattern_editor = ktech_editor_create(&project);
    CHECK(pattern_editor != NULL);
    if (pattern_editor) {
        uint8_t before_enabled = ktech_editor_project(pattern_editor)
            ->patterns[first].steps[0].enabled;
        CHECK(ktech_editor_apply(pattern_editor, KTECH_ACTION_ACTIVATE,
                                 KITTYIN_ACTION_PRESS) != KTECH_EFFECT_NONE);
        CHECK(ktech_editor_project(pattern_editor)->patterns[first]
                  .steps[0].enabled == (uint8_t)(before_enabled ^ 1u));
        ktech_editor_free(pattern_editor);
    }
    uint8_t *midi = malloc(KTECH_MIDI_MAX_BYTES);
    CHECK(midi != NULL);
    if (midi) {
        size_t midi_size = 0u;
        CHECK(ktech_midi_encode(&project, midi, KTECH_MIDI_MAX_BYTES,
                                &midi_size, error, sizeof error));
        CHECK(midi_size > 100u && memcmp(midi, "MThd", 4u) == 0);
        ktech_project imported;
        CHECK(ktech_midi_decode(&imported, midi, midi_size,
                                error, sizeof error));
        CHECK(imported.tempo == project.tempo);
        midi[0] ^= 1u;
        CHECK(!ktech_midi_decode(&imported, midi, midi_size,
                                 error, sizeof error));
        free(midi);
    }
}

static void test_project_files(void)
{
    char directory[] = "/tmp/kilix-techno-test-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char path[512];
    CHECK(snprintf(path, sizeof path, "%s/project.ktech", directory) > 0);
    ktech_project original, loaded;
    ktech_project_init(&original, 77u);
    CHECK(ktech_project_save(&original, path, false) == KTECH_PROJECT_OK);
    struct stat file_status;
    CHECK(stat(path, &file_status) == 0 &&
          (file_status.st_mode & 0777) == 0600);
    CHECK(ktech_project_save(&original, path, false) == KTECH_PROJECT_EXISTS);
    CHECK(ktech_project_load(&loaded, path) == KTECH_PROJECT_OK);
    CHECK(memcmp(&original, &loaded, sizeof original) == 0);
    original.tempo = 140u;
    CHECK(ktech_project_save(&original, path, true) == KTECH_PROJECT_OK);
    CHECK(ktech_project_load(&loaded, path) == KTECH_PROJECT_OK);
    CHECK(loaded.tempo == 140u);

    char midi_path[512];
    CHECK(snprintf(midi_path, sizeof midi_path, "%s/project.mid",
                   directory) > 0);
    char midi_error[128];
    CHECK(ktech_midi_save(&original, midi_path, false, midi_error,
                          sizeof midi_error));
    ktech_project midi_loaded;
    CHECK(ktech_midi_load(&midi_loaded, midi_path, midi_error,
                          sizeof midi_error));
    CHECK(midi_loaded.tempo == original.tempo);
    CHECK(!ktech_midi_save(&original, midi_path, false, midi_error,
                           sizeof midi_error));

    char link_path[512];
    CHECK(snprintf(link_path, sizeof link_path, "%s/link.ktech", directory) >
          0);
    CHECK(symlink(path, link_path) == 0);
    CHECK(ktech_project_load(&loaded, link_path) ==
          KTECH_PROJECT_SECURITY_ERROR);
    CHECK(ktech_project_save(&original, link_path, true) ==
          KTECH_PROJECT_SECURITY_ERROR);
    CHECK(unlink(link_path) == 0);

    CHECK(ktech_project_save_autosave(&original, directory) ==
          KTECH_PROJECT_OK);
    memset(&loaded, 0, sizeof loaded);
    CHECK(ktech_project_load_autosave(&loaded, directory) ==
          KTECH_PROJECT_OK);
    CHECK(memcmp(&original, &loaded, sizeof original) == 0);
    char autosave[512], appdir[512], recovery[512];
    CHECK(snprintf(appdir, sizeof appdir, "%s/kilix-techno", directory) > 0);
    CHECK(snprintf(autosave, sizeof autosave,
                   "%s/autosave.state", appdir) > 0);
    int autosave_fd = open(autosave, O_RDWR | O_CLOEXEC);
    CHECK(autosave_fd >= 0);
    if (autosave_fd >= 0) {
        CHECK(lseek(autosave_fd, -1, SEEK_END) >= 0);
        uint8_t last;
        CHECK(read(autosave_fd, &last, 1u) == 1);
        last ^= UINT8_C(0x80);
        CHECK(lseek(autosave_fd, -1, SEEK_END) >= 0);
        CHECK(write(autosave_fd, &last, 1u) == 1);
        CHECK(close(autosave_fd) == 0);
    }
    CHECK(ktech_project_load_autosave(&loaded, directory) ==
          KTECH_PROJECT_CORRUPT);
    CHECK(ktech_project_quarantine_autosave(
              directory, recovery, sizeof recovery) == KTECH_PROJECT_OK);
    CHECK(access(autosave, F_OK) != 0 && errno == ENOENT);
    CHECK(access(recovery, F_OK) == 0);
    CHECK(unlink(path) == 0);
    CHECK(unlink(midi_path) == 0);
    CHECK(unlink(recovery) == 0);
    CHECK(rmdir(appdir) == 0);
    CHECK(rmdir(directory) == 0);
}

static bool map_has_key(const kittyin_action_map *map, uint16_t action,
                        uint32_t key, uint32_t modifiers)
{
    for (size_t index = 0u; index < map->binding_count; ++index) {
        const kittyin_action_binding *binding = &map->bindings[index];
        if (binding->kind == KITTYIN_BINDING_KEY &&
            binding->action == action && binding->key == key &&
            binding->modifiers == modifiers)
            return true;
    }
    return false;
}

static void test_settings_and_tap(void)
{
    ktech_settings original, decoded;
    ktech_settings_init(&original);
    CHECK(ktech_settings_validate(&original));
    uint8_t bytes[KTECH_SETTINGS_ENCODED_MAX + 1u];
    size_t size;
    CHECK(ktech_settings_encode(&original, bytes, sizeof bytes, &size) ==
          KTECH_SETTINGS_OK);
    memset(&decoded, 0xa5, sizeof decoded);
    CHECK(ktech_settings_decode(&decoded, bytes, size) == KTECH_SETTINGS_OK);
    CHECK(memcmp(&original, &decoded, sizeof original) == 0);
    ktech_settings sentinel = decoded;
    bytes[size] = 0u;
    CHECK(ktech_settings_decode(&decoded, bytes, size + 1u) !=
          KTECH_SETTINGS_OK);
    CHECK(memcmp(&decoded, &sentinel, sizeof decoded) == 0);
    uint8_t settings_attack[KTECH_SETTINGS_ENCODED_MAX];
    CHECK(ktech_settings_encode(&original, settings_attack,
                                sizeof settings_attack, &size) ==
          KTECH_SETTINGS_OK);
    settings_attack[16] = (uint8_t)(KTECH_BINDING_CAPACITY + 1u);
    settings_attack[17] = 0u;
    CHECK(ktech_settings_decode(&decoded, settings_attack, size) ==
          KTECH_SETTINGS_NONCANONICAL);
    CHECK(memcmp(&decoded, &sentinel, sizeof decoded) == 0);
    CHECK(ktech_settings_encode(&original, settings_attack,
                                sizeof settings_attack, &size) ==
          KTECH_SETTINGS_OK);
    settings_attack[42] = 9u;
    CHECK(ktech_settings_decode(&decoded, settings_attack, size) ==
          KTECH_SETTINGS_NONCANONICAL);
    CHECK(memcmp(&decoded, &sentinel, sizeof decoded) == 0);

    kittyin_action_map map;
    CHECK(ktech_settings_apply_action_map(&original, &map));
    CHECK(map_has_key(&map, KTECH_ACTION_UNDO, (uint32_t)'z',
                      KITTYKB_MOD_CTRL));
    CHECK(map_has_key(&map, KTECH_ACTION_REDO, (uint32_t)'y',
                      KITTYKB_MOD_CTRL));
    CHECK(map_has_key(&map, KTECH_ACTION_EXPORT, (uint32_t)'e',
                      KITTYKB_MOD_CTRL));
    kittykb_event key;
    memset(&key, 0, sizeof key);
    key.key = (uint32_t)'M';
    key.action = KITTYKB_ACTION_PRESS;
    kittyin_action_event mapped[4];
    CHECK(ktech_actions_map_key(&map, &key, mapped, 4u) == 1u);
    CHECK(mapped[0].action == KTECH_ACTION_SOLO);
    key.key = (uint32_t)'m';
    key.shifted_key = (uint32_t)'M';
    key.modifiers = KITTYKB_MOD_SHIFT;
    CHECK(ktech_actions_map_key(&map, &key, mapped, 4u) == 1u);
    CHECK(mapped[0].action == KTECH_ACTION_SOLO);

    char directory[] = "/tmp/kilix-techno-settings-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    CHECK(ktech_settings_save(&original, directory) == KTECH_SETTINGS_OK);
    memset(&decoded, 0, sizeof decoded);
    CHECK(ktech_settings_load(&decoded, directory) == KTECH_SETTINGS_OK);
    CHECK(memcmp(&original, &decoded, sizeof original) == 0);
    char path[512], appdir[512];
    CHECK(ktech_settings_path(directory, path, sizeof path) ==
          KTECH_SETTINGS_OK);
    struct stat settings_status;
    CHECK(stat(path, &settings_status) == 0 &&
          (settings_status.st_mode & 0777) == 0600);
    CHECK(unlink(path) == 0);
    CHECK(snprintf(appdir, sizeof appdir, "%s/kilix-techno", directory) > 0);
    CHECK(rmdir(appdir) == 0);
    CHECK(rmdir(directory) == 0);

    ktech_tap_tempo tap;
    uint16_t tempo = 0u;
    ktech_tap_tempo_init(&tap);
    CHECK(!ktech_tap_tempo_press(&tap, 1000, &tempo));
    CHECK(ktech_tap_tempo_press(&tap, 1500, &tempo));
    CHECK(tempo == 120u);
    CHECK(ktech_tap_tempo_press(&tap, 2000, &tempo));
    CHECK(tempo == 120u);
    CHECK(!ktech_tap_tempo_press(&tap, 6000, &tempo));
}

static void test_probability_and_compile(void)
{
    for (uint32_t loop = 0u; loop < 1000u; ++loop) {
        CHECK(!ktech_probability_hit(1u, 0u, 0u, loop, 0u, 0u));
        CHECK(ktech_probability_hit(1u, 0u, 0u, loop, 0u, 100u));
    }
    CHECK(ktech_probability_hit(123u, 2u, 4u, 9u, 3u, 47u) ==
          ktech_probability_hit(123u, 2u, 4u, 9u, 3u, 47u));

    ktech_project project;
    ktech_project_init(&project, 123u);
    project.scenes[0].tracks[0].length = 3u;
    for (uint8_t step = 3u; step < KTECH_STEP_COUNT; ++step)
        project.scenes[0].tracks[0].steps[step] =
            (ktech_step){0u, 36u, 100u, 80u, 100u, 1u};
    project.scenes[0].tracks[1].length = 8u;
    for (uint8_t step = 8u; step < KTECH_STEP_COUNT; ++step)
        project.scenes[0].tracks[1].steps[step] =
            (ktech_step){0u, 38u, 100u, 80u, 100u, 1u};
    char error[160];
    ktech_compiled *first = ktech_compile_scene(&project, 0u,
                                                 error, sizeof error);
    ktech_compiled *second = ktech_compile_scene(&project, 0u,
                                                  error, sizeof error);
    CHECK(first != NULL && second != NULL);
    if (first && second) {
        CHECK(ktech_compiled_supercycle_rows(first) == 48u);
        CHECK(ktech_compiled_rows(first) == 3072u);
        CHECK(ktech_compiled_hash(first) == ktech_compiled_hash(second));
        CHECK(chipseq_song_validate(ktech_compiled_song(first),
                                    error, sizeof error));
        uint64_t baseline_hash = ktech_compiled_hash(first);
        project.tempo = 127u;
        ktech_compiled *tempo = ktech_compile_scene(&project, 0u,
                                                     error, sizeof error);
        CHECK(tempo != NULL && ktech_compiled_hash(tempo) != baseline_hash);
        ktech_compiled_free(tempo);
        project.tempo = 126u;
        CHECK(ktech_project_set_track_role(&project, 0u, 0u,
                                            KTECH_ROLE_TEXTURE));
        ktech_compiled *timbre = ktech_compile_scene(&project, 0u,
                                                      error, sizeof error);
        CHECK(timbre != NULL && ktech_compiled_hash(timbre) != baseline_hash);
        ktech_compiled_free(timbre);
    }
    ktech_compiled_free(first);
    ktech_compiled_free(second);
}

static bool write_test_wav(const char *path)
{
    uint8_t header[44] = {0};
    memcpy(header, "RIFF", 4u);
    write_u32(header + 4u, 44u);
    memcpy(header + 8u, "WAVEfmt ", 8u);
    write_u32(header + 16u, 16u);
    write_u16(header + 20u, 1u);
    write_u16(header + 22u, 1u);
    write_u32(header + 24u, 44100u);
    write_u32(header + 28u, 88200u);
    write_u16(header + 32u, 2u);
    write_u16(header + 34u, 16u);
    memcpy(header + 36u, "data", 4u);
    write_u32(header + 40u, 8u);
    const int16_t samples[] = {0, 12000, -12000, 0};
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    bool ok = fwrite(header, 1u, sizeof header, file) == sizeof header;
    for (size_t index = 0u; ok && index < 4u; ++index) {
        uint8_t encoded[2];
        write_u16(encoded, (uint16_t)samples[index]);
        ok = fwrite(encoded, 1u, sizeof encoded, file) == sizeof encoded;
    }
    return fclose(file) == 0 && ok;
}

static void test_optional_soundbank(void)
{
    static const char *const names[] = {
        "kick.wav", "snare.wav", "clap.wav", "closed-hat.wav",
        "open-hat.wav", "cowbell.wav"
    };
    char root[] = "/tmp/kilix-techno-bank-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char directory[512];
    CHECK(snprintf(directory, sizeof directory, "%s/tr808-fischer", root) > 0);
    CHECK(mkdir(directory, 0700) == 0);
    for (size_t index = 0u; index < sizeof names / sizeof names[0]; ++index) {
        char path[640];
        CHECK(snprintf(path, sizeof path, "%s/%s", directory, names[index]) > 0);
        CHECK(write_test_wav(path));
    }
    char receipt[640];
    CHECK(snprintf(receipt, sizeof receipt, "%s/.kilix-bank", directory) > 0);
    FILE *file = fopen(receipt, "wb");
    CHECK(file != NULL);
    if (file) {
        CHECK(fwrite("receipt\n", 1u, 8u, file) == 8u);
        CHECK(fclose(file) == 0);
    }
    const char *previous_value = getenv("KILIX_TECHNO_SOUNDBANK_DIR");
    char *previous = previous_value ? strdup(previous_value) : NULL;
    CHECK(setenv("KILIX_TECHNO_SOUNDBANK_DIR", root, 1) == 0);
    char warning[160];
    ktech_soundbank *bank = ktech_soundbank_load(48000u, warning,
                                                  sizeof warning);
    CHECK(bank != NULL);
    CHECK(ktech_soundbank_installed_count(bank) == 1u);
    CHECK(ktech_soundbank_role_available(bank, KTECH_ROLE_TIDAL_KICK));
    CHECK(!ktech_soundbank_role_available(bank, KTECH_ROLE_VCSL_FM_PIANO));
    const chipseq_pcm *pcm = ktech_soundbank_pcm(
        bank, KTECH_ROLE_TIDAL_KICK);
    CHECK(pcm != NULL && pcm->frame_count > 0u && pcm->root_note == 36u);

    ktech_project project;
    ktech_project_init(&project, 14u);
    CHECK(ktech_project_set_track_role(&project, 0u, 0u,
                                       KTECH_ROLE_TIDAL_KICK));
    ktech_compiled *compiled = ktech_compile_scene_with_bank(
        &project, 0u, bank, warning, sizeof warning);
    CHECK(compiled != NULL);
    if (compiled)
        CHECK(ktech_compiled_song(compiled)->instruments[0].wave ==
              CHIPSEQ_WAVE_PCM);
    ktech_compiled_free(compiled);
    ktech_soundbank_free(bank);
    if (previous) {
        CHECK(setenv("KILIX_TECHNO_SOUNDBANK_DIR", previous, 1) == 0);
        free(previous);
    } else {
        CHECK(unsetenv("KILIX_TECHNO_SOUNDBANK_DIR") == 0);
    }
    for (size_t index = 0u; index < sizeof names / sizeof names[0]; ++index) {
        char path[640];
        (void)snprintf(path, sizeof path, "%s/%s", directory, names[index]);
        CHECK(unlink(path) == 0);
    }
    CHECK(unlink(receipt) == 0);
    CHECK(rmdir(directory) == 0);
    CHECK(rmdir(root) == 0);
}

static void test_editor_history(void)
{
    ktech_project project;
    ktech_project_init(&project, 9u);
    ktech_editor *editor = ktech_editor_create(&project);
    CHECK(editor != NULL);
    if (!editor) return;
    CHECK(!ktech_editor_dirty(editor));
    CHECK((ktech_editor_apply(editor, KTECH_ACTION_ACTIVATE,
                              KITTYIN_ACTION_PRESS) &
           KTECH_EFFECT_PROJECT_CHANGED) != 0u);
    CHECK(ktech_editor_project(editor)->scenes[0].tracks[0].steps[0].enabled ==
          0u);
    CHECK(ktech_editor_dirty(editor));
    CHECK((ktech_editor_apply(editor, KTECH_ACTION_UNDO,
                              KITTYIN_ACTION_PRESS) &
           KTECH_EFFECT_PROJECT_CHANGED) != 0u);
    CHECK(!ktech_editor_dirty(editor));
    CHECK(ktech_editor_project(editor)->scenes[0].tracks[0].steps[0].enabled ==
          1u);
    CHECK((ktech_editor_apply(editor, KTECH_ACTION_REDO,
                              KITTYIN_ACTION_PRESS) &
           KTECH_EFFECT_PROJECT_CHANGED) != 0u);
    CHECK(ktech_editor_dirty(editor));
    ktech_editor_mark_saved(editor);
    CHECK(!ktech_editor_dirty(editor));
    CHECK(ktech_editor_select_step(editor, 3u, 2u, true));
    CHECK(ktech_editor_track(editor) == 3u && ktech_editor_step(editor) == 2u);
    CHECK(ktech_editor_dirty(editor));
    CHECK(ktech_editor_set_step_enabled(editor, 3u, 3u, true));
    CHECK(ktech_editor_project(editor)->scenes[0].tracks[3].steps[3].enabled ==
          1u);
    uint64_t enabled_revision = ktech_editor_revision(editor);
    CHECK(ktech_editor_set_step_enabled(editor, 3u, 3u, false));
    CHECK(ktech_editor_project(editor)->scenes[0].tracks[3].steps[3].enabled ==
          0u);
    CHECK(ktech_editor_revision(editor) > enabled_revision);
    uint64_t cleared_revision = ktech_editor_revision(editor);
    CHECK(ktech_editor_set_step_enabled(editor, 3u, 3u, false));
    CHECK(ktech_editor_revision(editor) == cleared_revision);
    CHECK(!ktech_editor_set_step_enabled(editor, 3u, KTECH_STEP_COUNT,
                                         false));
    bool original_steps[4];
    for (uint8_t step = 4u; step < 8u; ++step)
        original_steps[step - 4u] =
            ktech_editor_project(editor)->scenes[0].tracks[3]
                .steps[step].enabled != 0u;
    bool painted = !original_steps[0];
    ktech_editor_begin_step_paint(editor);
    for (uint8_t step = 4u; step < 8u; ++step)
        CHECK(ktech_editor_set_step_enabled(editor, 3u, step, painted));
    ktech_editor_end_step_paint(editor);
    for (uint8_t step = 4u; step < 8u; ++step)
        CHECK((ktech_editor_project(editor)->scenes[0].tracks[3]
                   .steps[step].enabled != 0u) == painted);
    CHECK((ktech_editor_apply(editor, KTECH_ACTION_UNDO,
                              KITTYIN_ACTION_PRESS) &
           KTECH_EFFECT_PROJECT_CHANGED) != 0u);
    for (uint8_t step = 4u; step < 8u; ++step)
        CHECK((ktech_editor_project(editor)->scenes[0].tracks[3]
                   .steps[step].enabled != 0u) ==
              original_steps[step - 4u]);
    ktech_step piano_before = ktech_editor_project(editor)->scenes[0]
        .tracks[3].steps[8];
    ktech_editor_begin_step_paint(editor);
    CHECK(ktech_editor_set_step_note(editor, 3u, 8u, 67u));
    CHECK(ktech_editor_set_step_enabled(editor, 3u, 8u, true));
    ktech_editor_end_step_paint(editor);
    CHECK(ktech_editor_project(editor)->scenes[0].tracks[3]
              .steps[8].note == 67u);
    CHECK((ktech_editor_apply(editor, KTECH_ACTION_UNDO,
                              KITTYIN_ACTION_PRESS) &
           KTECH_EFFECT_PROJECT_CHANGED) != 0u);
    CHECK(memcmp(&ktech_editor_project(editor)->scenes[0].tracks[3].steps[8],
                 &piano_before, sizeof piano_before) == 0);
    CHECK(ktech_editor_select_track(editor, 4u));
    CHECK(!ktech_editor_select_track(editor, 4u));
    CHECK(ktech_editor_select_track(editor, 3u));
    CHECK(!ktech_editor_select_track(editor, KTECH_TRACK_COUNT));
    uint64_t role_revision = ktech_editor_revision(editor);
    CHECK(ktech_editor_set_track_role(editor, 3u,
                                      KTECH_ROLE_VCSL_FM_PIANO));
    CHECK(ktech_editor_revision(editor) > role_revision);
    role_revision = ktech_editor_revision(editor);
    CHECK(ktech_editor_set_track_role(editor, 3u,
                                      KTECH_ROLE_VCSL_FM_PIANO));
    CHECK(ktech_editor_revision(editor) == role_revision);
    CHECK(!ktech_editor_set_track_role(editor, KTECH_TRACK_COUNT,
                                       KTECH_ROLE_KICK));
    CHECK(!ktech_editor_set_track_role(editor, 3u, KTECH_ROLE_COUNT));
    CHECK(ktech_editor_apply(editor, KTECH_ACTION_INCREASE,
                             KITTYIN_ACTION_RELEASE) == KTECH_EFFECT_NONE);
    CHECK(ktech_project_validate(ktech_editor_project(editor), NULL, 0u));

    kittyin_action_map map;
    ktech_actions_bind_defaults(&map);
    CHECK(map.binding_count >= 20u);
    CHECK(map.binding_count == project.binding_count);
    CHECK(map_has_key(&map, KTECH_ACTION_TAP_TEMPO, (uint32_t)'t', 0u));
    uint8_t old_role = ktech_editor_project(editor)->scenes[0].tracks[3].role;
    while (ktech_editor_field(editor) != KTECH_FIELD_INSTRUMENT)
        (void)ktech_editor_apply(editor, KTECH_ACTION_DOWN,
                                 KITTYIN_ACTION_PRESS);
    CHECK((ktech_editor_apply(editor, KTECH_ACTION_INCREASE,
                              KITTYIN_ACTION_PRESS) &
           KTECH_EFFECT_PROJECT_CHANGED) != 0u);
    CHECK(ktech_editor_project(editor)->scenes[0].tracks[3].role != old_role);
    (void)ktech_editor_apply(editor, KTECH_ACTION_NEXT_SCENE,
                             KITTYIN_ACTION_PRESS);
    CHECK(ktech_editor_scene(editor) == 1u);
    CHECK(ktech_editor_select_scene(editor, 5u));
    CHECK(ktech_editor_scene(editor) == 5u);
    CHECK(!ktech_editor_select_scene(editor, 5u));
    CHECK(!ktech_editor_select_scene(editor, KTECH_SCENE_COUNT));
    (void)ktech_editor_apply(editor, KTECH_ACTION_LAUNCH_SCENE,
                             KITTYIN_ACTION_PRESS);
    CHECK(ktech_editor_project(editor)->active_scene == 5u);
    (void)ktech_editor_apply(editor, KTECH_ACTION_UNDO,
                             KITTYIN_ACTION_PRESS);
    CHECK(ktech_editor_project(editor)->active_scene == 5u);
    CHECK(ktech_editor_scene(editor) == 5u);
    ktech_editor_free(editor);
}

static void test_editor_range_tools(void)
{
    ktech_project project;
    ktech_project_init(&project, 123u);
    ktech_editor *editor = ktech_editor_create(&project);
    CHECK(editor != NULL);
    if (!editor) return;
    CHECK(ktech_editor_select_range(editor, 1u, 2u, 2u, 9u));
    ktech_step_range range;
    CHECK(ktech_editor_range(editor, &range));
    CHECK(range.first_track == 1u && range.last_track == 2u);
    CHECK(range.first_step == 2u && range.last_step == 9u);
    uint64_t revision = ktech_editor_revision(editor);
    CHECK(ktech_editor_apply_tool(editor, KTECH_TOOL_FILL));
    CHECK(ktech_editor_revision(editor) == revision + 1u);
    for (uint8_t track = 1u; track <= 2u; ++track)
        for (uint8_t step = 2u; step <= 9u; ++step)
            CHECK(ktech_editor_project(editor)->scenes[0].tracks[track]
                      .steps[step].enabled == 1u);
    CHECK(ktech_editor_velocity_ramp(editor, 20u, 127u));
    CHECK(ktech_editor_project(editor)->scenes[0].tracks[1]
              .steps[2].velocity == 20u);
    CHECK(ktech_editor_project(editor)->scenes[0].tracks[1]
              .steps[9].velocity == 127u);
    CHECK(ktech_editor_probability_scale(editor, 50u));
    CHECK(ktech_editor_project(editor)->scenes[0].tracks[2]
              .steps[5].probability == 50u);
    CHECK(ktech_editor_euclidean_fill(editor, 3u, 0u));
    unsigned pulses = 0u;
    for (uint8_t step = 2u; step <= 9u; ++step)
        pulses += ktech_editor_project(editor)->scenes[0].tracks[1]
                      .steps[step].enabled;
    CHECK(pulses == 3u);
    CHECK(ktech_editor_copy_range(editor));
    CHECK(ktech_editor_paste_range(editor, 3u, 2u));
    CHECK(memcmp(&ktech_editor_project(editor)->scenes[0].tracks[1]
                      .steps[2],
                 &ktech_editor_project(editor)->scenes[0].tracks[3]
                      .steps[2],
                 8u * sizeof(ktech_step)) == 0);
    CHECK(ktech_editor_paint_range_value(editor, KTECH_FIELD_NOTE, 64u));
    CHECK(ktech_editor_project(editor)->scenes[0].tracks[3]
              .steps[2].note == 64u);
    CHECK(ktech_editor_apply_tool(editor, KTECH_TOOL_MIRROR));
    CHECK(ktech_editor_apply(editor, KTECH_ACTION_UNDO,
                             KITTYIN_ACTION_PRESS) != KTECH_EFFECT_NONE);
    CHECK(ktech_editor_cut_range(editor));
    CHECK(!ktech_editor_select_range(editor, 0u, 0u, 0u,
                                     KTECH_STEP_COUNT));
    CHECK(ktech_editor_set_favorite(editor, KTECH_ROLE_KICK, true));
    CHECK(ktech_editor_favorite(editor, KTECH_ROLE_KICK));
    CHECK(ktech_editor_set_favorite(editor, KTECH_ROLE_KICK, false));
    CHECK(!ktech_editor_favorite(editor, KTECH_ROLE_KICK));
    CHECK(ktech_editor_set_view(editor, KTECH_VIEW_PIANO_ROLL));
    CHECK(ktech_editor_view_current(editor) == KTECH_VIEW_PIANO_ROLL);
    CHECK(!ktech_editor_set_view(editor, KTECH_VIEW_COUNT));
    ktech_editor_clear_range(editor);
    CHECK(!ktech_editor_range(editor, NULL));
    ktech_editor_free(editor);
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void test_dsp_and_export(void)
{
    ktech_dsp dsp;
    CHECK(ktech_dsp_init(&dsp, 8000u));
    ktech_project project;
    ktech_project_init(&project, 44u);
    project.master_level = 100u;
    project.master_tone = 100u;
    project.master_drive = 0u;
    project.master_delay = 0u;
    ktech_dsp_configure(&dsp, &project);
    int16_t impulse[] = {1000, -1000, 2000, -2000};
    ktech_dsp_process(&dsp, impulse, 2u);
    CHECK(impulse[0] >= 980 && impulse[0] <= 1000);
    CHECK(impulse[1] <= -980 && impulse[1] >= -1000);
    ktech_dsp_free(&dsp);

    ktech_fx_rack rack;
    CHECK(ktech_fx_rack_init(&rack, 8000u));
    ktech_bus effect_bus;
    memset(&effect_bus, 0, sizeof effect_bus);
    effect_bus.effect_count = 2u;
    effect_bus.effects[0] = (ktech_effect){
        KTECH_EFFECT_SATURATOR, 1u, 100u, 1u, {80u, 0u, 0u, 0u}
    };
    effect_bus.effects[1] = (ktech_effect){
        KTECH_EFFECT_BITCRUSH, 1u, 100u, 2u, {20u, 50u, 0u, 0u}
    };
    ktech_fx_rack_configure(&rack, &effect_bus);
    int16_t effected[64];
    for (size_t index = 0u; index < 64u; ++index)
        effected[index] = (int16_t)((int)index * 700 - 20000);
    int16_t before = effected[10];
    ktech_fx_rack_process(&rack, effected, 32u);
    CHECK(effected[10] != before);
    ktech_fx_rack_free(&rack);

    char directory[] = "/tmp/kilix-techno-export-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char first_path[512], second_path[512], highres_path[512];
    char automated_path[512], silent_path[512];
    char cancelled_path[512];
    char link_path[512], parent_link[512], unsafe_path[512];
    CHECK(snprintf(first_path, sizeof first_path, "%s/first.wav", directory) >
          0);
    CHECK(snprintf(second_path, sizeof second_path, "%s/second.wav",
                   directory) > 0);
    CHECK(snprintf(highres_path, sizeof highres_path, "%s/highres.wav",
                   directory) > 0);
    CHECK(snprintf(automated_path, sizeof automated_path, "%s/automated.wav",
                   directory) > 0);
    CHECK(snprintf(silent_path, sizeof silent_path, "%s/silent.wav",
                   directory) > 0);
    CHECK(snprintf(cancelled_path, sizeof cancelled_path, "%s/cancel.wav",
                   directory) > 0);
    CHECK(snprintf(link_path, sizeof link_path, "%s/link.wav", directory) >
          0);
    CHECK(snprintf(parent_link, sizeof parent_link, "%s/parent", directory) >
          0);
    CHECK(snprintf(unsafe_path, sizeof unsafe_path, "%s/parent/unsafe.wav",
                   directory) > 0);
    ktech_export_options options;
    ktech_export_options_init(&options);
    options.sample_rate = 8000u;
    options.bars = 1u;
    ktech_export_info first, second;
    char error[200];
    CHECK(ktech_export_wav(&project, first_path, &options, &first,
                           error, sizeof error));
    CHECK(ktech_export_wav(&project, second_path, &options, &second,
                           error, sizeof error));
    struct stat wave_status;
    CHECK(stat(first_path, &wave_status) == 0 &&
          (wave_status.st_mode & 0777) == 0600);
    CHECK(first.frames == second.frames && first.pcm_hash == second.pcm_hash &&
          first.bytes == second.bytes);
    ktech_project automated = project;
    uint8_t automation_lane;
    CHECK(ktech_automation_lane_add(&automated,
                                    KTECH_AUTOMATION_MASTER_LEVEL, 0u,
                                    &automation_lane));
    CHECK(ktech_automation_point_set(&automated, automation_lane, 0u, 0));
    CHECK(ktech_automation_point_set(&automated, automation_lane, 1u,
                                     10000));
    ktech_export_info automated_info, silent_info;
    CHECK(ktech_export_wav(&automated, automated_path, &options,
                           &automated_info, error, sizeof error));
    ktech_project silent = project;
    silent.master_level = 0u;
    CHECK(ktech_export_wav(&silent, silent_path, &options, &silent_info,
                           error, sizeof error));
    CHECK(automated_info.pcm_hash != silent_info.pcm_hash);
    ktech_export_options highres = options;
    highres.bit_depth = 24u;
    highres.normalize = true;
    highres.tail_ms = 100u;
    ktech_export_info highres_info;
    CHECK(ktech_export_wav(&project, highres_path, &highres, &highres_info,
                           error, sizeof error));
    CHECK(highres_info.frames == first.frames + 800u &&
          highres_info.bytes == 44u + highres_info.frames * 6u);
    int highres_fd = open(highres_path, O_RDONLY | O_CLOEXEC);
    uint8_t highres_header[44];
    CHECK(highres_fd >= 0 && read(highres_fd, highres_header,
                                  sizeof highres_header) ==
                                 (ssize_t)sizeof highres_header);
    if (highres_fd >= 0) CHECK(close(highres_fd) == 0);
    CHECK(highres_header[34] == 24u && highres_header[35] == 0u);
    char resource_root[512];
    CHECK(snprintf(resource_root, sizeof resource_root, "%s/resources",
                   directory) > 0);
    CHECK(mkdir(resource_root, 0700) == 0);
    CHECK(ktech_sample_import(&project, first_path, resource_root,
                              "user-kick.wav", "CC0-1.0",
                              KTECH_ROLE_KICK, error, sizeof error));
    int descriptor;
    char sfz_path[600];
    CHECK(snprintf(sfz_path, sizeof sfz_path, "%s/source.sfz", directory) >
          0);
    descriptor = open(sfz_path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                      0600);
    CHECK(descriptor >= 0);
    static const char sfz_text[] = "<region> sample=first.wav key=60\n";
    CHECK(descriptor >= 0 && write(descriptor, sfz_text,
                                   sizeof sfz_text - 1u) ==
                                  (ssize_t)(sizeof sfz_text - 1u));
    if (descriptor >= 0) CHECK(close(descriptor) == 0);
    CHECK(ktech_sample_import(&project, sfz_path, resource_root,
                              "converted/from-sfz.wav", "CC0-1.0",
                              KTECH_ROLE_BASS, error, sizeof error));
    CHECK(project.resources[project.resource_count - 1u].format ==
          KTECH_RESOURCE_WAV);
    ktech_resource_report resource_report;
    CHECK(ktech_resource_check(&project, resource_root, &resource_report,
                               error, sizeof error));
    CHECK(resource_report.available == 2u && resource_report.missing == 0u &&
          resource_report.changed == 0u && resource_report.unsafe == 0u);
    char converted_path[640];
    CHECK(snprintf(converted_path, sizeof converted_path,
                   "%s/converted/from-sfz.wav", resource_root) > 0);
    descriptor = open(converted_path, O_RDONLY | O_CLOEXEC);
    CHECK(descriptor >= 0);
    uint8_t converted_header[44];
    CHECK(descriptor >= 0 && read(descriptor, converted_header,
                                  sizeof converted_header) ==
                                 (ssize_t)sizeof converted_header);
    if (descriptor >= 0) CHECK(close(descriptor) == 0);
    CHECK(memcmp(converted_header, "RIFF", 4u) == 0 &&
          read_u32(converted_header + 24u) == 44100u);
    ktech_project full_manifest = project;
    uint8_t dummy_hash[32] = {1u};
    while (full_manifest.resource_count < KTECH_RESOURCE_CAPACITY) {
        char dummy_path[64];
        (void)snprintf(dummy_path, sizeof dummy_path, "missing/%u.wav",
                       full_manifest.resource_count);
        bool added = ktech_resource_add(&full_manifest, dummy_path,
                                        "CC0-1.0", dummy_hash, 1u,
                                        KTECH_RESOURCE_WAV,
                                        KTECH_ROLE_KICK);
        CHECK(added);
        if (!added) break;
    }
    CHECK(full_manifest.resource_count == KTECH_RESOURCE_CAPACITY);
    CHECK(!ktech_sample_import(&full_manifest, first_path, resource_root,
                               "failed/cleanup.wav", "CC0-1.0",
                               KTECH_ROLE_KICK, error, sizeof error));
    char failed_import[640];
    CHECK(snprintf(failed_import, sizeof failed_import,
                   "%s/failed/cleanup.wav", resource_root) > 0);
    CHECK(access(failed_import, F_OK) != 0 && errno == ENOENT);
    descriptor = open(first_path, O_RDONLY | O_CLOEXEC);
    CHECK(descriptor >= 0);
    uint8_t header[44];
    CHECK(descriptor >= 0 && read(descriptor, header, sizeof header) ==
                                  (ssize_t)sizeof header);
    if (descriptor >= 0) CHECK(close(descriptor) == 0);
    CHECK(memcmp(header, "RIFF", 4u) == 0 &&
          memcmp(header + 8u, "WAVEfmt ", 8u) == 0 &&
          read_u32(header + 24u) == 8000u &&
          read_u32(header + 40u) + 44u == first.bytes);

    ktech_audio_options audio_options;
    ktech_audio_options_init(&audio_options);
    audio_options.sample_rate = 8000u;
    audio_options.offline = true;
    ktech_audio *live_equivalent = ktech_audio_create(
        &project, &audio_options, error, sizeof error);
    CHECK(live_equivalent != NULL);
    int16_t live_samples[4096u * 2u];
    uint8_t wave_samples[4096u * 4u];
    if (live_equivalent) {
        ktech_audio_mix(live_equivalent, live_samples, 4096u);
        descriptor = open(first_path, O_RDONLY | O_CLOEXEC);
        CHECK(descriptor >= 0);
        bool wave_read = false;
        if (descriptor >= 0) {
            off_t positioned = lseek(descriptor, 44, SEEK_SET);
            ssize_t count = positioned == 44
                                ? read(descriptor, wave_samples,
                                       sizeof wave_samples) : -1;
            CHECK(positioned == 44);
            CHECK(count == (ssize_t)sizeof wave_samples);
            wave_read = positioned == 44 &&
                        count == (ssize_t)sizeof wave_samples;
            CHECK(close(descriptor) == 0);
        }
        int32_t maximum_difference = 0;
        for (size_t index = 0u;
             wave_read && index < sizeof live_samples / sizeof live_samples[0];
             ++index) {
            uint16_t sample = (uint16_t)live_samples[index];
            uint16_t encoded_sample =
                (uint16_t)wave_samples[index * 2u] |
                (uint16_t)((uint16_t)wave_samples[index * 2u + 1u] << 8);
            int32_t difference = (int32_t)(int16_t)sample -
                                 (int32_t)(int16_t)encoded_sample;
            if (difference < 0) difference = -difference;
            if (difference > maximum_difference)
                maximum_difference = difference;
        }
        if (wave_read && maximum_difference > 1)
            (void)fprintf(stderr, "live/export maximum sample delta: %d\n",
                          maximum_difference);
        CHECK(wave_read && maximum_difference <= 1);
    }
    ktech_audio_free(live_equivalent);

    char stem_root[512];
    CHECK(snprintf(stem_root, sizeof stem_root, "%s/stems", directory) > 0);
    CHECK(mkdir(stem_root, 0700) == 0);
    ktech_export_info stems[KTECH_TRACK_COUNT];
    CHECK(ktech_export_stems(&project, stem_root, &options, stems,
                             error, sizeof error));
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
        char stem_path[600];
        CHECK(snprintf(stem_path, sizeof stem_path, "%s/track-%02u.wav",
                       stem_root, (unsigned)track + 1u) > 0);
        CHECK(stems[track].bytes > 44u);
        CHECK(unlink(stem_path) == 0);
    }
    CHECK(rmdir(stem_root) == 0);

    CHECK(symlink(first_path, link_path) == 0);
    options.replace_existing = true;
    CHECK(!ktech_export_wav(&project, link_path, &options, NULL,
                            error, sizeof error));
    options.replace_existing = false;
    CHECK(symlink(directory, parent_link) == 0);
    CHECK(!ktech_export_wav(&project, unsafe_path, &options, NULL,
                            error, sizeof error));

    atomic_bool cancel;
    atomic_init(&cancel, true);
    options.cancel = &cancel;
    CHECK(!ktech_export_wav(&project, cancelled_path, &options, NULL,
                            error, sizeof error));
    CHECK(access(cancelled_path, F_OK) != 0 && errno == ENOENT);

    CHECK(unlink(first_path) == 0);
    CHECK(unlink(second_path) == 0);
    CHECK(unlink(highres_path) == 0);
    CHECK(unlink(automated_path) == 0);
    CHECK(unlink(silent_path) == 0);
    CHECK(unlink(link_path) == 0);
    CHECK(unlink(parent_link) == 0);
    CHECK(unlink(sfz_path) == 0);
    char imported_path[600];
    CHECK(snprintf(imported_path, sizeof imported_path,
                   "%s/user-kick.wav", resource_root) > 0);
    CHECK(unlink(imported_path) == 0);
    CHECK(unlink(converted_path) == 0);
    char converted_directory[600];
    CHECK(snprintf(converted_directory, sizeof converted_directory,
                   "%s/converted", resource_root) > 0);
    CHECK(rmdir(converted_directory) == 0);
    char failed_directory[600];
    CHECK(snprintf(failed_directory, sizeof failed_directory,
                   "%s/failed", resource_root) > 0);
    CHECK(rmdir(failed_directory) == 0);
    CHECK(rmdir(resource_root) == 0);
    CHECK(rmdir(directory) == 0);
}

static void test_partition_rate(uint32_t sample_rate)
{
    enum { FRAMES = 2048 };
    ktech_project project;
    ktech_project_init(&project, UINT32_C(0x72617465));
    ktech_audio_options options;
    ktech_audio_options_init(&options);
    options.sample_rate = sample_rate;
    options.offline = true;
    char error[160];
    ktech_audio *whole = ktech_audio_create(&project, &options,
                                             error, sizeof error);
    ktech_audio *split = ktech_audio_create(&project, &options,
                                             error, sizeof error);
    CHECK(whole != NULL && split != NULL);
    int16_t first[FRAMES * 2u], second[FRAMES * 2u];
    if (whole && split) {
        ktech_audio_mix(whole, first, FRAMES);
        size_t done = 0u;
        while (done < FRAMES) {
            size_t block = 1u + done % 113u;
            if (block > FRAMES - done) block = FRAMES - done;
            ktech_audio_mix(split, second + done * 2u, block);
            done += block;
        }
        CHECK(memcmp(first, second, sizeof first) == 0);
    }
    ktech_audio_free(whole);
    ktech_audio_free(split);
}

static void test_offline_audio(void)
{
    ktech_project project;
    ktech_project_init(&project, 88u);
    ktech_audio_options options;
    ktech_audio_options_init(&options);
    options.sample_rate = 8000u;
    options.offline = true;
    char error[200];
    ktech_audio *whole = ktech_audio_create(&project, &options,
                                             error, sizeof error);
    ktech_audio *split = ktech_audio_create(&project, &options,
                                             error, sizeof error);
    CHECK(whole != NULL && split != NULL);
    if (!whole || !split) {
        ktech_audio_free(whole);
        ktech_audio_free(split);
        return;
    }
    enum { FRAMES = 4096 };
    int16_t *a = malloc(FRAMES * 2u * sizeof *a);
    int16_t *b = malloc(FRAMES * 2u * sizeof *b);
    CHECK(a != NULL && b != NULL);
    if (a && b) {
        ktech_audio_mix(whole, a, FRAMES);
        size_t done = 0u;
        while (done < FRAMES) {
            size_t chunk = (done % 127u) + 1u;
            if (chunk > FRAMES - done) chunk = FRAMES - done;
            ktech_audio_mix(split, b + done * 2u, chunk);
            done += chunk;
        }
        CHECK(memcmp(a, b, FRAMES * 2u * sizeof *a) == 0);
        bool nonzero = false;
        for (size_t index = 0u; index < FRAMES * 2u; ++index)
            nonzero = nonzero || a[index] != 0;
        CHECK(nonzero);
    }
    free(a);
    free(b);

    ktech_audio_status status;
    ktech_audio_get_status(whole, &status);
    CHECK(status.offline && !status.audible &&
          status.transport.rendered_frames == FRAMES);

    project.scenes[0].tracks[2].steps[1].enabled = 1u;
    CHECK(ktech_audio_publish(whole, &project, 0u, KTECH_LAUNCH_STEP,
                              false, error, sizeof error));
    CHECK(!ktech_audio_publish(whole, &project, 0u, KTECH_LAUNCH_STEP,
                               false, error, sizeof error));
    int16_t transition[2048 * 2u];
    ktech_audio_mix(whole, transition, 2048u);
    ktech_audio_poll(whole);
    ktech_audio_get_status(whole, &status);
    CHECK(!status.pending && status.playhead.applied_generation == 1u);
    project.scenes[0].tracks[0].mute = 1u;
    CHECK(ktech_audio_update_mix(whole, &project, 0u));
    CHECK(ktech_audio_toggle_play(whole));
    CHECK(ktech_audio_toggle_play(whole));
    CHECK(ktech_audio_stop(whole));
    CHECK(ktech_audio_restart(whole));

    project.active_scene = 1u;
    CHECK(ktech_audio_publish(whole, &project, 1u, KTECH_LAUNCH_BEAT,
                              true, error, sizeof error));
    for (unsigned block = 0u; block < 80u; ++block) {
        ktech_audio_mix(whole, transition, 127u);
        ktech_audio_poll(whole);
        ktech_audio_get_status(whole, &status);
        if (!status.pending) break;
    }
    CHECK(!status.pending && status.scene == 1u);

    project.active_scene = 2u;
    CHECK(ktech_audio_publish(whole, &project, 2u, KTECH_LAUNCH_BAR,
                              true, error, sizeof error));
    for (unsigned block = 0u; block < 180u; ++block) {
        ktech_audio_mix(whole, transition, 127u);
        ktech_audio_poll(whole);
        ktech_audio_get_status(whole, &status);
        if (!status.pending) break;
    }
    CHECK(!status.pending && status.scene == 2u);

    ktech_audio_free(whole);
    ktech_audio_free(split);
    test_partition_rate(44100u);
    test_partition_rate(48000u);
}

static void test_ui_models(void)
{
    ktech_project project;
    ktech_project_init(&project, 5u);
    ktech_editor *editor = ktech_editor_create(&project);
    CHECK(editor != NULL);
    if (!editor) return;
    ktech_audio_status status;
    memset(&status, 0, sizeof status);
    status.playhead.active = true;
    status.playhead.rows_elapsed = 4u;
    status.peak_left = 12000u;
    status.peak_right = 8000u;

    sr_canvas canvas;
    CHECK(sr_canvas_init(&canvas, 1000, 640));
    if (canvas.px) {
        ktech_ui_render(&canvas, editor, &status, NULL);
        uint64_t sum = 0u;
        for (size_t index = 0u; index < (size_t)canvas.w * (size_t)canvas.h;
             index += 113u)
            sum += canvas.px[index];
        CHECK(sum != 0u);
        ktech_ui_layout layout;
        ktech_ui_layout_make(&layout, canvas.w, canvas.h, editor);
        uint8_t track, step;
        CHECK(ktech_ui_hit_step(&layout,
                                layout.grid_x + layout.cell_width / 2,
                                layout.grid_y + layout.row_height / 2,
                                &track, &step));
        CHECK(track == 0u && step == 0u);
        CHECK(!ktech_ui_hit_step(&layout, 0, 0, &track, &step));
        CHECK(ktech_ui_hit_track(&layout, layout.grid_x / 2,
                                 layout.grid_y + layout.row_height * 3 + 2,
                                 &track));
        CHECK(track == 3u);
        CHECK(!ktech_ui_hit_track(&layout, layout.grid_x + 1,
                                  layout.grid_y, &track));
        uint8_t scene_number;
        int scene_button = layout.scene_width / (int)KTECH_SCENE_COUNT;
        CHECK(ktech_ui_hit_scene(&layout,
                                 layout.scene_x + scene_button * 7 + 2,
                                 layout.scene_y + layout.scene_height / 2,
                                 &scene_number));
        CHECK(scene_number == 7u);
        CHECK(!ktech_ui_hit_scene(&layout, 0, 0, &scene_number));

        ktech_ui_context_menu menu = {
            .page = KTECH_UI_CONTEXT_ACTIONS, .anchor_x = 100,
            .anchor_y = 100
        };
        CHECK(ktech_ui_context_item_count(&menu) == 4u);
        ktech_ui_render_context_menu(&canvas, editor, &menu);
        uint8_t item;
        CHECK(ktech_ui_hit_context_menu(&canvas, &menu, 110, 146, &item));
        CHECK(item == 0u);
        CHECK(!ktech_ui_hit_context_menu(&canvas, &menu, 20, 20, &item));
    }
    sr_canvas_free(&canvas);

    for (unsigned track = 0u; track < 6u; ++track)
        (void)ktech_editor_apply(editor, KTECH_ACTION_NEXT_TRACK,
                                 KITTYIN_ACTION_PRESS);
    CHECK(sr_canvas_init(&canvas, 320, 240));
    if (canvas.px) {
        ktech_ui_render(&canvas, editor, &status, "compact");
        ktech_ui_layout compact;
        ktech_ui_layout_make(&compact, canvas.w, canvas.h, editor);
        CHECK(compact.track_rows == 4 && compact.track_start == 4);
        uint8_t track, step;
        CHECK(ktech_ui_hit_step(
            &compact, compact.grid_x + compact.cell_width / 2,
            compact.grid_y + 2 * compact.row_height + 2,
            &track, &step));
        CHECK(track == 6u && step == 0u);
    }
    sr_canvas_free(&canvas);

    char cells[8192];
    size_t needed = ktech_cell_render(cells, sizeof cells, 100, editor,
                                      &status, "ready");
    CHECK(needed < sizeof cells);
    CHECK(strstr(cells, "KILIX TECHNO") != NULL);
    CHECK(strstr(cells, "[|") != NULL || strstr(cells, "[X]") != NULL ||
          strstr(cells, "[.]") != NULL);
    CHECK(strstr(cells, "EDIT ") != NULL);
    CHECK(strstr(cells, "CHANCE") != NULL);
    CHECK(strstr(cells, "BANKS 0/13 AVAILABLE") != NULL);
    CHECK(strstr(cells, "STATUS ready") != NULL);
    CHECK(ktech_editor_set_view(editor, KTECH_VIEW_PIANO_ROLL));
    needed = ktech_cell_render(cells, sizeof cells, 100, editor,
                               &status, "ready");
    CHECK(needed < sizeof cells && strstr(cells, "PIANO ROLL") != NULL);
    CHECK(ktech_editor_set_view(editor, KTECH_VIEW_ARRANGEMENT));
    needed = ktech_cell_render(cells, sizeof cells, 100, editor,
                               &status, "ready");
    CHECK(needed < sizeof cells && strstr(cells, "ARRANGEMENT") != NULL);
    CHECK(ktech_editor_set_view(editor, KTECH_VIEW_MIXER));
    needed = ktech_cell_render(cells, sizeof cells, 100, editor,
                               &status, "ready");
    CHECK(needed < sizeof cells && strstr(cells, "MIXER") != NULL);
    CHECK(ktech_editor_set_view(editor, KTECH_VIEW_STEPS));
    char tiny[8];
    CHECK(ktech_cell_render(tiny, sizeof tiny, 40, editor, &status, NULL) >
          sizeof tiny);
    CHECK(tiny[sizeof tiny - 1u] == '\0');

    unsigned expected_role = 0u;
    for (uint8_t group = 0u; group < KTECH_UI_INSTRUMENT_GROUP_COUNT;
         ++group) {
        CHECK(ktech_ui_instrument_group_name(group) != NULL);
        uint8_t item = 0u;
        ktech_role role;
        while (ktech_ui_instrument_group_role(group, item, &role)) {
            CHECK((unsigned)role == expected_role);
            ++expected_role;
            ++item;
        }
        ktech_ui_context_menu roles = {
            .page = KTECH_UI_CONTEXT_ROLES, .group = group
        };
        CHECK(ktech_ui_context_item_count(&roles) == (size_t)item + 1u);
    }
    CHECK(expected_role == KTECH_ROLE_COUNT);
    ktech_role search_results[8];
    CHECK(ktech_ui_search_roles("kick", search_results, 8u) >= 4u);
    CHECK(search_results[0] == KTECH_ROLE_KICK);
    CHECK(ktech_ui_search_roles("definitely absent", NULL, 0u) == 0u);
    ktech_ui_context_menu search_menu = {
        .page = KTECH_UI_CONTEXT_SEARCH,
        .query = "808",
        .query_length = 3u
    };
    CHECK(ktech_ui_context_item_count(&search_menu) > 1u);
    ktech_role searched_role;
    CHECK(ktech_ui_context_role(&search_menu, 0u, &searched_role));
    CHECK(strstr(ktech_role_name(searched_role), "808") != NULL);
    CHECK(ktech_cell_render_context_menu(cells, sizeof cells, 80, editor,
                                         &search_menu) < sizeof cells);
    CHECK(strstr(cells, "SEARCH /808") != NULL);
    uint8_t searched_item;
    CHECK(ktech_cell_hit_context_menu(&search_menu, 2, 3, 80,
                                      &searched_item));
    CHECK(searched_item == 0u);
    ktech_ui_context_menu cell_menu = {
        .page = KTECH_UI_CONTEXT_GROUPS
    };
    size_t menu_needed = ktech_cell_render_context_menu(
        cells, sizeof cells, 80, editor, &cell_menu);
    CHECK(menu_needed < sizeof cells);
    CHECK(strstr(cells, "INSTRUMENT BANKS") != NULL);
    CHECK(strstr(cells, "TIDALCYCLES TR-808") != NULL);
    uint8_t menu_item;
    CHECK(ktech_cell_hit_context_menu(&cell_menu, 2, 2, 80,
                                      &menu_item));
    CHECK(menu_item == 0u);
    CHECK(!ktech_cell_hit_context_menu(&cell_menu, 2, 1, 80,
                                       &menu_item));
    ktech_editor_free(editor);
}

int main(void)
{
    /* Audio tests must not discover the developer's optional soundbanks. Apart
     * from making results host-dependent, loading an installed SF2 initializes
     * process-global FluidSynth/GLib caches that are outside this test's
     * lifecycle. The focused optional-bank test temporarily replaces this
     * empty root with its own fixture and restores it afterwards. */
    char soundbank_root[] = "/tmp/kilix-techno-empty-banks-XXXXXX";
    if (mkdtemp(soundbank_root) == NULL ||
        setenv("KILIX_TECHNO_SOUNDBANK_DIR", soundbank_root, 1) != 0) {
        (void)fprintf(stderr, "test-core: cannot isolate optional soundbanks\n");
        return 1;
    }
    test_project_codec();
    test_workstation_project();
    test_project_files();
    test_settings_and_tap();
    test_probability_and_compile();
    test_optional_soundbank();
    test_editor_history();
    test_editor_range_tools();
    test_dsp_and_export();
    test_offline_audio();
    test_ui_models();
    CHECK(unsetenv("KILIX_TECHNO_SOUNDBANK_DIR") == 0);
    CHECK(rmdir(soundbank_root) == 0);
    if (failures != 0u) {
        (void)fprintf(stderr, "test-core: %u/%u checks failed\n",
                      failures, checks);
        return 1;
    }
    (void)printf("test-core: OK (%u checks)\n", checks);
    return 0;
}
