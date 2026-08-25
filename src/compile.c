#include "kilix_techno/compile.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    GATE_VARIANTS = 16,
    ARRANGEMENT_GATE_VARIANTS = 4,
    TICKS_PER_ROW = 12,
    COMPILED_INSTRUMENTS = KTECH_TRACK_COUNT * GATE_VARIANTS,
    COMPILED_INSTRUMENTS_MAX = KTECH_ROLE_COUNT * ARRANGEMENT_GATE_VARIANTS
};

_Static_assert(COMPILED_INSTRUMENTS_MAX <= 256u,
               "arrangement instruments must fit the cell index");

struct ktech_compiled {
    chipseq_song song;
    chipseq_pattern pattern;
    chipseq_instrument instruments[COMPILED_INSTRUMENTS_MAX];
    chipseq_seq volume_sequences[COMPILED_INSTRUMENTS_MAX];
    int8_t volume_values[COMPILED_INSTRUMENTS_MAX][TICKS_PER_ROW];
    chipseq_cell *cells;
    uint8_t order[1];
    uint16_t rows;
    uint16_t supercycle_rows;
    uint8_t scene;
    uint64_t hash;
    char name[KTECH_NAME_CAPACITY];
};

static const int8_t kick_pitch_values[TICKS_PER_ROW] = {
    48, 32, 20, 12, 6, 2, 0, -2, -4, -4, -4, -4
};
static const chipseq_seq kick_pitch = {
    kick_pitch_values, TICKS_PER_ROW, CHIPSEQ_SEQ_NO_LOOP,
    CHIPSEQ_SEQ_NO_RELEASE
};

static const int8_t bass_pitch_values[TICKS_PER_ROW] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1
};
static const chipseq_seq bass_pitch = {
    bass_pitch_values, TICKS_PER_ROW, CHIPSEQ_SEQ_NO_LOOP,
    CHIPSEQ_SEQ_NO_RELEASE
};

static const uint8_t texture_wave[CHIPSEQ_WAVETABLE_LEN] = {
    8u, 10u, 12u, 14u, 15u, 14u, 12u, 10u,
    8u, 6u, 4u, 2u, 1u, 2u, 4u, 6u,
    8u, 11u, 13u, 15u, 13u, 11u, 9u, 7u,
    5u, 3u, 1u, 0u, 2u, 4u, 6u, 7u
};

static void set_error(char *error, size_t size, const char *format, ...)
{
    va_list arguments;
    if (!error || size == 0u) return;
    va_start(arguments, format);
    (void)vsnprintf(error, size, format, arguments);
    va_end(arguments);
}

static uint16_t gcd_u16(uint16_t left, uint16_t right)
{
    while (right != 0u) {
        uint16_t remainder = (uint16_t)(left % right);
        left = right;
        right = remainder;
    }
    return left;
}

static uint16_t lcm_u16(uint16_t left, uint16_t right)
{
    return (uint16_t)((left / gcd_u16(left, right)) * right);
}

static uint32_t avalanche(uint32_t value)
{
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    value *= UINT32_C(0x846ca68b);
    value ^= value >> 16;
    return value;
}

bool ktech_probability_hit(uint32_t seed, uint8_t scene, uint8_t track,
                           uint32_t loop, uint8_t step,
                           uint8_t probability)
{
    if (probability == 0u) return false;
    if (probability >= 100u) return true;
    uint32_t value = seed ^ UINT32_C(0x9e3779b9);
    value = avalanche(value ^ ((uint32_t)scene + 1u) * UINT32_C(0x85ebca6b));
    value = avalanche(value ^ ((uint32_t)track + 1u) * UINT32_C(0xc2b2ae35));
    value = avalanche(value ^ loop * UINT32_C(0x27d4eb2d));
    value = avalanche(value ^ ((uint32_t)step + 1u) * UINT32_C(0x165667b1));
    return value % 100u < probability;
}

static bool performance_hit(const ktech_project *project, uint8_t scene,
                            uint8_t track, uint32_t loop, uint8_t local_step,
                            const ktech_track *source,
                            const ktech_step *step)
{
    int probability = step->probability;
    if (project->performance_variation > 0u) {
        uint32_t random = avalanche(project->seed ^
            ((uint32_t)scene << 24) ^ ((uint32_t)track << 16) ^
            (loop * UINT32_C(0x9e3779b9)) ^ local_step);
        int spread = project->performance_variation / 4u;
        probability += (int)(random % (unsigned)(spread * 2 + 1)) - spread;
        if (probability < 0) probability = 0;
        if (probability > 100) probability = 100;
    }
    if (step->enabled != 0u)
        return ktech_probability_hit(project->seed, scene, track, loop,
                                     local_step, (uint8_t)probability);
    ktech_role family = ktech_role_fallback((ktech_role)source->role);
    bool rhythmic = family <= KTECH_ROLE_PERCUSSION;
    return rhythmic && project->performance_fill > 0u &&
           local_step % 4u == 3u &&
           ktech_probability_hit(project->seed ^ UINT32_C(0xf111a11),
                                 scene, track, loop, local_step,
                                 project->performance_fill);
}

static int base_volume(uint8_t role, unsigned tick)
{
    static const uint8_t envelopes[KTECH_NATIVE_ROLE_COUNT][TICKS_PER_ROW] = {
        {64, 61, 55, 47, 38, 29, 21, 14, 8, 4, 2, 0},
        {64, 58, 51, 44, 37, 30, 24, 18, 13, 9, 5, 2},
        {64, 42, 58, 34, 48, 28, 18, 12, 8, 5, 3, 1},
        {62, 48, 34, 22, 13, 7, 3, 1, 0, 0, 0, 0},
        {58, 55, 51, 46, 41, 36, 31, 26, 21, 16, 11, 6},
        {62, 54, 45, 36, 28, 21, 15, 10, 7, 4, 2, 1},
        {58, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48},
        {62, 60, 56, 50, 43, 35, 28, 21, 15, 10, 6, 3},
        {48, 52, 56, 60, 58, 54, 50, 47, 44, 41, 38, 35}
    };
    return envelopes[ktech_role_fallback((ktech_role)role)][tick];
}

static void configure_instrument(chipseq_instrument *instrument,
                                 uint8_t role, const ktech_soundbank *bank)
{
    memset(instrument, 0, sizeof *instrument);
    instrument->name = ktech_role_name((ktech_role)role);
    const chipseq_pcm *pcm = ktech_soundbank_pcm(
        bank, (ktech_role)role);
    if (pcm) {
        instrument->wave = CHIPSEQ_WAVE_PCM;
        instrument->pcm = pcm;
        return;
    }
    switch (ktech_role_fallback((ktech_role)role)) {
    case KTECH_ROLE_KICK:
        instrument->wave = CHIPSEQ_WAVE_TRIANGLE;
        instrument->tri_steps = 32u;
        instrument->pitch_seq = &kick_pitch;
        instrument->transpose = -12;
        break;
    case KTECH_ROLE_SNARE:
        instrument->wave = CHIPSEQ_WAVE_NOISE;
        instrument->noise_mode = CHIPSEQ_NOISE_LONG;
        break;
    case KTECH_ROLE_CLAP:
        instrument->wave = CHIPSEQ_WAVE_NOISE;
        instrument->noise_mode = CHIPSEQ_NOISE_SHORT;
        break;
    case KTECH_ROLE_CLOSED_HAT:
        instrument->wave = CHIPSEQ_WAVE_NOISE;
        instrument->noise_mode = CHIPSEQ_NOISE_SHORT;
        instrument->transpose = 24;
        break;
    case KTECH_ROLE_OPEN_HAT:
        instrument->wave = CHIPSEQ_WAVE_NOISE;
        instrument->noise_mode = CHIPSEQ_NOISE_LONG;
        instrument->transpose = 24;
        break;
    case KTECH_ROLE_PERCUSSION:
        instrument->wave = CHIPSEQ_WAVE_PULSE;
        instrument->duty = CHIPSEQ_DUTY_12;
        break;
    case KTECH_ROLE_BASS:
        instrument->wave = CHIPSEQ_WAVE_SAW;
        instrument->duty = CHIPSEQ_DUTY_50;
        instrument->pitch_seq = &bass_pitch;
        instrument->transpose = -12;
        break;
    case KTECH_ROLE_CHORD:
        instrument->wave = CHIPSEQ_WAVE_PULSE;
        instrument->duty = CHIPSEQ_DUTY_25;
        instrument->vib_speed = 4u;
        instrument->vib_depth = 1u;
        break;
    case KTECH_ROLE_TEXTURE:
        instrument->wave = CHIPSEQ_WAVE_WAVETABLE;
        instrument->wavetable = texture_wave;
        instrument->vib_speed = 2u;
        instrument->vib_depth = 1u;
        instrument->vib_delay = 2u;
        break;
    default:
        break;
    }
}

static void build_instruments(ktech_compiled *compiled,
                              const ktech_scene *scene,
                              const ktech_soundbank *bank)
{
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
        uint8_t role = scene->tracks[track].role;
        for (uint8_t gate = 0u; gate < GATE_VARIANTS; ++gate) {
            size_t index = (size_t)track * GATE_VARIANTS + gate;
            chipseq_instrument *instrument = &compiled->instruments[index];
            configure_instrument(instrument, role, bank);
            unsigned cut = 1u + (unsigned)gate * (TICKS_PER_ROW - 1u) /
                                     (GATE_VARIANTS - 1u);
            if (gate == GATE_VARIANTS - 1u) cut = TICKS_PER_ROW;
            for (unsigned tick = 0u; tick < TICKS_PER_ROW; ++tick) {
                compiled->volume_values[index][tick] =
                    (int8_t)(tick < cut ? base_volume(role, tick) : 0);
            }
            compiled->volume_sequences[index] = (chipseq_seq){
                compiled->volume_values[index], TICKS_PER_ROW,
                CHIPSEQ_SEQ_NO_LOOP, CHIPSEQ_SEQ_NO_RELEASE
            };
            instrument->vol_seq = &compiled->volume_sequences[index];
        }
    }
}

static uint8_t gate_variant(uint8_t gate)
{
    return (uint8_t)(((uint16_t)(gate - 1u) * (GATE_VARIANTS - 1u) + 49u) /
                     99u);
}

static uint64_t fnv_byte(uint64_t hash, uint8_t byte)
{
    return (hash ^ byte) * UINT64_C(1099511628211);
}

static uint64_t fnv_u32(uint64_t hash, uint32_t value)
{
    for (unsigned shift = 0u; shift < 32u; shift += 8u)
        hash = fnv_byte(hash, (uint8_t)(value >> shift));
    return hash;
}

ktech_compiled *ktech_compile_scene_with_bank(
    const ktech_project *project, uint8_t scene_index,
    const ktech_soundbank *bank, char *error, size_t error_size)
{
    if (!ktech_project_validate(project, error, error_size) ||
        scene_index >= KTECH_SCENE_COUNT) {
        if (scene_index >= KTECH_SCENE_COUNT)
            set_error(error, error_size, "scene %u is outside 0..15",
                      scene_index);
        return NULL;
    }
    ktech_scene resolved;
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
        ktech_track scratch;
        const ktech_track *source = ktech_project_resolve_track(
            project, scene_index, track, &scratch);
        if (!source) {
            set_error(error, error_size, "cannot resolve scene pattern");
            return NULL;
        }
        resolved.tracks[track] = *source;
    }
    const ktech_scene *scene = &resolved;
    uint16_t supercycle = 1u;
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track)
        supercycle = lcm_u16(supercycle, scene->tracks[track].length);
    uint32_t rows32 = (uint32_t)supercycle * KTECH_PERFORMANCE_SUPERCYCLES;
    if (supercycle > 192u || rows32 > KTECH_COMPILED_ROWS_MAX ||
        rows32 > UINT16_MAX) {
        set_error(error, error_size, "compiled horizon exceeds bounds");
        return NULL;
    }

    ktech_compiled *compiled = calloc(1u, sizeof *compiled);
    if (!compiled) {
        set_error(error, error_size, "cannot allocate compiled scene");
        return NULL;
    }
    const size_t cell_count = (size_t)rows32 * KTECH_TRACK_COUNT;
    compiled->cells = calloc(cell_count, sizeof *compiled->cells);
    if (!compiled->cells) {
        free(compiled);
        set_error(error, error_size, "cannot allocate compiled event grid");
        return NULL;
    }
    compiled->rows = (uint16_t)rows32;
    compiled->supercycle_rows = supercycle;
    compiled->scene = scene_index;
    build_instruments(compiled, scene, bank);

    uint64_t hash = UINT64_C(14695981039346656037);
    hash = fnv_u32(hash, project->seed);
    hash = fnv_u32(hash, project->tempo);
    hash = fnv_byte(hash, project->swing);
    hash = fnv_byte(hash, project->performance_fill);
    hash = fnv_byte(hash, project->performance_variation);
    hash = fnv_byte(hash, scene_index);
    hash = fnv_u32(hash, supercycle);
    for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track)
        hash = fnv_byte(hash, scene->tracks[track].role);
    for (uint32_t row = 0u; row < rows32; ++row) {
        for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
            const ktech_track *source = &scene->tracks[track];
            const uint8_t local_step = (uint8_t)(row % source->length);
            const uint32_t loop = row / source->length;
            const ktech_step *step = &source->steps[local_step];
            chipseq_cell *cell =
                &compiled->cells[(size_t)row * KTECH_TRACK_COUNT + track];
            cell->note = CHIPSEQ_NOTE_OFF;
            cell->vol = CHIPSEQ_VOL_NONE;
            if (performance_hit(project, scene_index, track, loop,
                                local_step, source, step)) {
                cell->note = step->note;
                cell->inst = (uint8_t)(track * GATE_VARIANTS +
                                       gate_variant(step->gate));
                cell->vol = (uint8_t)(((uint16_t)step->velocity * 64u + 63u) /
                                      127u);
                if (step->ratchet > 1u) {
                    cell->fx = CHIPSEQ_FX_RETRIGGER;
                    cell->fxp = (uint8_t)(TICKS_PER_ROW / step->ratchet);
                }
            }
            hash = fnv_byte(hash, cell->note);
            hash = fnv_byte(hash, cell->inst);
            hash = fnv_byte(hash, cell->vol);
            hash = fnv_byte(hash, cell->fx);
            hash = fnv_byte(hash, cell->fxp);
        }
    }

    compiled->pattern.cells = compiled->cells;
    compiled->pattern.rows = compiled->rows;
    compiled->order[0] = 0u;
    (void)snprintf(compiled->name, sizeof compiled->name, "%s",
                   project->name);
    compiled->song.name = compiled->name;
    compiled->song.instruments = compiled->instruments;
    compiled->song.instrument_count = COMPILED_INSTRUMENTS;
    compiled->song.patterns = &compiled->pattern;
    compiled->song.pattern_count = 1u;
    compiled->song.order = compiled->order;
    compiled->song.order_length = 1u;
    compiled->song.loop_order = 0u;
    compiled->song.channels = KTECH_TRACK_COUNT;
    compiled->song.rows_per_beat = 4u;
    compiled->song.ticks_per_row = TICKS_PER_ROW;
    compiled->song.bpm_q8 = CHIPSEQ_BPM(project->tempo);
    compiled->hash = hash;

    if (!chipseq_song_validate(&compiled->song, error, error_size)) {
        ktech_compiled_free(compiled);
        return NULL;
    }
    if (error && error_size) error[0] = '\0';
    return compiled;
}

ktech_compiled *ktech_compile_scene(const ktech_project *project,
                                    uint8_t scene_index, char *error,
                                    size_t error_size)
{
    return ktech_compile_scene_with_bank(project, scene_index, NULL, error,
                                         error_size);
}

ktech_compiled *ktech_compile_audition(ktech_role role, uint8_t note,
                                       uint16_t tempo,
                                       const ktech_soundbank *bank,
                                       char *error, size_t error_size)
{
    if (role >= KTECH_ROLE_COUNT || note > 127u || tempo < 20u ||
        tempo > 255u) {
        set_error(error, error_size, "invalid audition request");
        return NULL;
    }
    ktech_compiled *compiled = calloc(1u, sizeof *compiled);
    if (!compiled) {
        set_error(error, error_size, "cannot allocate audition");
        return NULL;
    }
    compiled->cells = calloc(4u, sizeof *compiled->cells);
    if (!compiled->cells) {
        free(compiled);
        set_error(error, error_size, "cannot allocate audition cells");
        return NULL;
    }
    configure_instrument(&compiled->instruments[0], (uint8_t)role, bank);
    for (unsigned tick = 0u; tick < TICKS_PER_ROW; ++tick)
        compiled->volume_values[0][tick] =
            (int8_t)base_volume((uint8_t)role, tick);
    compiled->volume_sequences[0] = (chipseq_seq){
        compiled->volume_values[0], TICKS_PER_ROW,
        CHIPSEQ_SEQ_NO_LOOP, CHIPSEQ_SEQ_NO_RELEASE
    };
    compiled->instruments[0].vol_seq = &compiled->volume_sequences[0];
    for (unsigned row = 0u; row < 4u; ++row) {
        compiled->cells[row].note = row == 0u ? note : CHIPSEQ_NOTE_OFF;
        compiled->cells[row].inst = 0u;
        compiled->cells[row].vol = row == 0u ? 52u : CHIPSEQ_VOL_NONE;
    }
    compiled->rows = 4u;
    compiled->supercycle_rows = 4u;
    compiled->pattern = (chipseq_pattern){compiled->cells, 4u};
    compiled->order[0] = 0u;
    (void)snprintf(compiled->name, sizeof compiled->name, "Preview %s",
                   ktech_role_name(role));
    compiled->song = (chipseq_song){
        compiled->name, compiled->instruments, 1u,
        &compiled->pattern, 1u, compiled->order, 1u, CHIPSEQ_NO_LOOP,
        1u, 4u, TICKS_PER_ROW, CHIPSEQ_BPM(tempo)
    };
    if (!chipseq_song_validate(&compiled->song, error, error_size)) {
        ktech_compiled_free(compiled);
        return NULL;
    }
    if (error && error_size > 0u) error[0] = '\0';
    return compiled;
}

static const ktech_track *resolve_arrangement_track(
    const ktech_project *project, const ktech_arrangement_entry *entry,
    uint8_t track, ktech_track *scratch)
{
    if (entry->patterns[track] == KTECH_PATTERN_NONE)
        return ktech_project_resolve_track(project, entry->scene, track,
                                           scratch);
    uint8_t reference = entry->patterns[track];
    if (reference >= project->pattern_count) return NULL;
    *scratch = project->scenes[entry->scene].tracks[track];
    const ktech_pattern *pattern = &project->patterns[reference];
    scratch->role = pattern->role;
    scratch->length = pattern->length;
    memcpy(scratch->steps, pattern->steps, sizeof scratch->steps);
    return scratch;
}

static void build_arrangement_instruments(ktech_compiled *compiled,
                                          const ktech_soundbank *bank)
{
    for (uint8_t role = 0u; role < KTECH_ROLE_COUNT; ++role)
        for (uint8_t gate = 0u; gate < ARRANGEMENT_GATE_VARIANTS; ++gate) {
            size_t index = (size_t)role * ARRANGEMENT_GATE_VARIANTS + gate;
            chipseq_instrument *instrument = &compiled->instruments[index];
            configure_instrument(instrument, role, bank);
            unsigned cut = 1u + (unsigned)gate * (TICKS_PER_ROW - 1u) /
                                      (ARRANGEMENT_GATE_VARIANTS - 1u);
            for (unsigned tick = 0u; tick < TICKS_PER_ROW; ++tick)
                compiled->volume_values[index][tick] =
                    (int8_t)(tick < cut ? base_volume(role, tick) : 0);
            compiled->volume_sequences[index] = (chipseq_seq){
                compiled->volume_values[index], TICKS_PER_ROW,
                CHIPSEQ_SEQ_NO_LOOP, CHIPSEQ_SEQ_NO_RELEASE
            };
            instrument->vol_seq = &compiled->volume_sequences[index];
        }
}

ktech_compiled *ktech_compile_arrangement_with_bank(
    const ktech_project *project, const ktech_soundbank *bank,
    char *error, size_t error_size)
{
    if (!ktech_project_validate(project, error, error_size) ||
        project->arrangement_count == 0u) {
        if (project && project->arrangement_count == 0u)
            set_error(error, error_size, "arrangement is empty");
        return NULL;
    }
    uint16_t bars = ktech_project_arrangement_bars(project);
    uint32_t rows32 = (uint32_t)bars * 16u;
    if (rows32 == 0u || rows32 > KTECH_COMPILED_ROWS_MAX ||
        rows32 > UINT16_MAX) {
        set_error(error, error_size, "arrangement horizon exceeds bounds");
        return NULL;
    }
    ktech_compiled *compiled = calloc(1u, sizeof *compiled);
    if (!compiled) {
        set_error(error, error_size, "cannot allocate arrangement");
        return NULL;
    }
    compiled->cells = calloc((size_t)rows32 * KTECH_TRACK_COUNT,
                             sizeof *compiled->cells);
    if (!compiled->cells) {
        free(compiled);
        set_error(error, error_size, "cannot allocate arrangement grid");
        return NULL;
    }
    compiled->rows = (uint16_t)rows32;
    compiled->supercycle_rows = (uint16_t)rows32;
    compiled->scene = project->arrangement[0].scene;
    build_arrangement_instruments(compiled, bank);
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = fnv_u32(hash, project->seed);
    hash = fnv_u32(hash, project->tempo);
    uint32_t global_row = 0u;
    for (uint16_t segment = 0u; segment < project->arrangement_count;
         ++segment) {
        const ktech_arrangement_entry *entry = &project->arrangement[segment];
        uint32_t segment_rows = (uint32_t)entry->bars * 16u;
        for (uint32_t row = 0u; row < segment_rows; ++row) {
            for (uint8_t track = 0u; track < KTECH_TRACK_COUNT; ++track) {
                ktech_track scratch;
                const ktech_track *source = resolve_arrangement_track(
                    project, entry, track, &scratch);
                if (!source) {
                    ktech_compiled_free(compiled);
                    set_error(error, error_size,
                              "cannot resolve arrangement pattern");
                    return NULL;
                }
                uint8_t local_step = (uint8_t)(row % source->length);
                uint32_t loop = row / source->length + segment * 257u;
                const ktech_step *step = &source->steps[local_step];
                chipseq_cell *cell = &compiled->cells[
                    (size_t)global_row * KTECH_TRACK_COUNT + track];
                cell->note = CHIPSEQ_NOTE_OFF;
                cell->vol = CHIPSEQ_VOL_NONE;
                if (performance_hit(project, entry->scene, track, loop,
                                    local_step, source, step)) {
                    unsigned gate = ((unsigned)(step->gate - 1u) *
                                     (ARRANGEMENT_GATE_VARIANTS - 1u) + 49u) /
                                    99u;
                    cell->note = step->note;
                    cell->inst = (uint8_t)(source->role *
                                           ARRANGEMENT_GATE_VARIANTS + gate);
                    cell->vol = (uint8_t)(((uint16_t)step->velocity * 64u +
                                           63u) / 127u);
                    if (step->ratchet > 1u) {
                        cell->fx = CHIPSEQ_FX_RETRIGGER;
                        cell->fxp = (uint8_t)(TICKS_PER_ROW / step->ratchet);
                    }
                }
                hash = fnv_byte(hash, cell->note);
                hash = fnv_byte(hash, cell->inst);
                hash = fnv_byte(hash, cell->vol);
            }
            ++global_row;
        }
    }
    compiled->pattern = (chipseq_pattern){compiled->cells, compiled->rows};
    compiled->order[0] = 0u;
    (void)snprintf(compiled->name, sizeof compiled->name, "%.50s arrangement",
                   project->name);
    compiled->song = (chipseq_song){
        compiled->name, compiled->instruments,
        COMPILED_INSTRUMENTS_MAX, &compiled->pattern, 1u,
        compiled->order, 1u, 0u, KTECH_TRACK_COUNT, 4u,
        TICKS_PER_ROW, CHIPSEQ_BPM(project->tempo)
    };
    compiled->hash = hash;
    if (!chipseq_song_validate(&compiled->song, error, error_size)) {
        ktech_compiled_free(compiled);
        return NULL;
    }
    if (error && error_size > 0u) error[0] = '\0';
    return compiled;
}

ktech_compiled *ktech_compile_arrangement(
    const ktech_project *project, char *error, size_t error_size)
{
    return ktech_compile_arrangement_with_bank(project, NULL, error,
                                               error_size);
}

void ktech_compiled_free(ktech_compiled *compiled)
{
    if (!compiled) return;
    free(compiled->cells);
    memset(compiled, 0, sizeof *compiled);
    free(compiled);
}

const chipseq_song *ktech_compiled_song(const ktech_compiled *compiled)
{
    return compiled ? &compiled->song : NULL;
}

uint16_t ktech_compiled_rows(const ktech_compiled *compiled)
{
    return compiled ? compiled->rows : 0u;
}

uint16_t ktech_compiled_supercycle_rows(const ktech_compiled *compiled)
{
    return compiled ? compiled->supercycle_rows : 0u;
}

uint64_t ktech_compiled_hash(const ktech_compiled *compiled)
{
    return compiled ? compiled->hash : 0u;
}

uint8_t ktech_compiled_scene(const ktech_compiled *compiled)
{
    return compiled ? compiled->scene : 0u;
}
