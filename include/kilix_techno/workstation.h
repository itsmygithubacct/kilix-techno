#ifndef KILIX_TECHNO_WORKSTATION_H
#define KILIX_TECHNO_WORKSTATION_H

#include "kilix_techno/project.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool ktech_arrangement_set_state(ktech_project *project, uint16_t selected,
                                 uint16_t queued, uint16_t audible);
bool ktech_automation_lane_add(ktech_project *project,
                               ktech_automation_target target,
                               uint8_t target_index, uint8_t *lane);
bool ktech_automation_point_set(ktech_project *project, uint8_t lane,
                                uint16_t row, int16_t value);
bool ktech_macro_add(ktech_project *project, const char *name,
                     uint8_t *macro);
bool ktech_macro_target_add(ktech_project *project, uint8_t macro,
                            ktech_automation_target target,
                            uint8_t target_index, int8_t depth);
bool ktech_macro_set_value(ktech_project *project, uint8_t macro,
                           uint8_t value);
bool ktech_bus_add(ktech_project *project, const char *name, uint8_t *bus);
bool ktech_bus_route_track(ktech_project *project, uint8_t track,
                           uint8_t bus);
bool ktech_bus_effect_add(ktech_project *project, uint8_t bus,
                          ktech_effect_kind kind, uint8_t wet,
                          const uint8_t *parameters,
                          uint8_t parameter_count);
bool ktech_performance_set(ktech_project *project, uint8_t fill,
                           uint8_t variation);
bool ktech_mute_group_set(ktech_project *project, uint8_t group,
                          uint8_t track_mask);
bool ktech_mute_group_apply(ktech_project *project, uint8_t scene,
                            uint8_t group, bool mute);
bool ktech_export_preset_add(ktech_project *project, const char *name,
                             ktech_export_format format, uint8_t bit_depth,
                             bool normalize, bool include_master,
                             uint16_t bars, uint16_t tail_ms);
bool ktech_resource_add(ktech_project *project, const char *relative_path,
                        const char *license, const uint8_t sha256[32],
                        uint32_t bytes, ktech_resource_format format,
                        ktech_role role);
int ktech_control_evaluate(const ktech_project *project,
                           ktech_automation_target target,
                           uint8_t target_index, uint16_t row,
                           int base_value);

#ifdef __cplusplus
}
#endif

#endif
