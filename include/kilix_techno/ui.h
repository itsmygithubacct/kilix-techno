#ifndef KILIX_TECHNO_UI_H
#define KILIX_TECHNO_UI_H

#include "kilix_techno/actions.h"
#include "kilix_techno/audio.h"

#include "soft_raster.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ktech_ui_layout {
    int grid_x;
    int grid_y;
    int grid_width;
    int row_height;
    int cell_width;
    int columns;
    int page_start;
    int track_start;
    int track_rows;
    int scene_x;
    int scene_width;
    int scene_y;
    int scene_height;
} ktech_ui_layout;

void ktech_ui_layout_make(ktech_ui_layout *layout, int width, int height,
                          const ktech_editor *editor);
void ktech_ui_render(sr_canvas *canvas, const ktech_editor *editor,
                     const ktech_audio_status *audio, const char *message);
bool ktech_ui_hit_step(const ktech_ui_layout *layout, int x, int y,
                       uint8_t *track, uint8_t *step);
bool ktech_ui_hit_track(const ktech_ui_layout *layout, int x, int y,
                        uint8_t *track);
bool ktech_ui_hit_scene(const ktech_ui_layout *layout, int x, int y,
                        uint8_t *scene);

/* Write a complete compact, color-independent cell view. Returns the bytes
 * that would be required, excluding the terminator. */
size_t ktech_cell_render(char *output, size_t capacity, int columns,
                         const ktech_editor *editor,
                         const ktech_audio_status *audio,
                         const char *message);

#ifdef __cplusplus
}
#endif

#endif
