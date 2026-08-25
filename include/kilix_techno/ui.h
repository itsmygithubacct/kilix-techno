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

#define KTECH_UI_INSTRUMENT_GROUP_COUNT 7u

typedef enum ktech_ui_context_page {
    KTECH_UI_CONTEXT_CLOSED = 0,
    KTECH_UI_CONTEXT_ACTIONS,
    KTECH_UI_CONTEXT_GROUPS,
    KTECH_UI_CONTEXT_ROLES
} ktech_ui_context_page;

typedef struct ktech_ui_context_menu {
    ktech_ui_context_page page;
    uint8_t track;
    uint8_t group;
    int anchor_x;
    int anchor_y;
} ktech_ui_context_menu;

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
size_t ktech_ui_context_item_count(const ktech_ui_context_menu *menu);
const char *ktech_ui_instrument_group_name(uint8_t group);
bool ktech_ui_instrument_group_role(uint8_t group, uint8_t item,
                                    ktech_role *role);
void ktech_ui_render_context_menu(sr_canvas *canvas,
                                  const ktech_editor *editor,
                                  const ktech_ui_context_menu *menu);
bool ktech_ui_hit_context_menu(const sr_canvas *canvas,
                               const ktech_ui_context_menu *menu,
                               int x, int y, uint8_t *item);

/* Write a complete compact, color-independent cell view. Returns the bytes
 * that would be required, excluding the terminator. */
size_t ktech_cell_render(char *output, size_t capacity, int columns,
                         const ktech_editor *editor,
                         const ktech_audio_status *audio,
                         const char *message);
size_t ktech_cell_render_context_menu(char *output, size_t capacity,
                                      int columns,
                                      const ktech_editor *editor,
                                      const ktech_ui_context_menu *menu);
bool ktech_cell_hit_context_menu(const ktech_ui_context_menu *menu,
                                 int x, int y, int columns,
                                 uint8_t *item);

#ifdef __cplusplus
}
#endif

#endif
