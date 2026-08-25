#include "kilix_techno/compile.h"
#include "kilix_techno/project.h"
#include "kilix_techno/settings.h"
#include "kilix_techno/soundbank.h"
#include "kilix_techno/actions.h"
#include "kilix_techno/audio.h"
#include "kilix_techno/dsp.h"
#include "kilix_techno/export.h"
#include "kilix_techno/ui.h"

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
    attack[10] = 3u;
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
    attack[10] = 1u;
    CHECK(ktech_project_decode(&decoded, attack, size) == KTECH_PROJECT_OK);
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
    (void)ktech_editor_apply(editor, KTECH_ACTION_LAUNCH_SCENE,
                             KITTYIN_ACTION_PRESS);
    CHECK(ktech_editor_project(editor)->active_scene == 1u);
    (void)ktech_editor_apply(editor, KTECH_ACTION_UNDO,
                             KITTYIN_ACTION_PRESS);
    CHECK(ktech_editor_project(editor)->active_scene == 1u);
    CHECK(ktech_editor_scene(editor) == 1u);
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

    char directory[] = "/tmp/kilix-techno-export-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char first_path[512], second_path[512], cancelled_path[512];
    char link_path[512], parent_link[512], unsafe_path[512];
    CHECK(snprintf(first_path, sizeof first_path, "%s/first.wav", directory) >
          0);
    CHECK(snprintf(second_path, sizeof second_path, "%s/second.wav",
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
    int descriptor = open(first_path, O_RDONLY | O_CLOEXEC);
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
    CHECK(unlink(link_path) == 0);
    CHECK(unlink(parent_link) == 0);
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
    CHECK(strstr(cells, "[|") != NULL || strstr(cells, "[x]") != NULL);
    CHECK(strstr(cells, "STATUS ready") != NULL);
    char tiny[8];
    CHECK(ktech_cell_render(tiny, sizeof tiny, 40, editor, &status, NULL) >
          sizeof tiny);
    CHECK(tiny[sizeof tiny - 1u] == '\0');
    ktech_editor_free(editor);
}

int main(void)
{
    test_project_codec();
    test_project_files();
    test_settings_and_tap();
    test_probability_and_compile();
    test_optional_soundbank();
    test_editor_history();
    test_dsp_and_export();
    test_offline_audio();
    test_ui_models();
    if (failures != 0u) {
        (void)fprintf(stderr, "test-core: %u/%u checks failed\n",
                      failures, checks);
        return 1;
    }
    (void)printf("test-core: OK (%u checks)\n", checks);
    return 0;
}
