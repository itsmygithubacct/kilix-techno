#ifndef KILIX_TECHNO_SOUNDBANK_H
#define KILIX_TECHNO_SOUNDBANK_H

#include "kilix_techno/project.h"

#include "chip_sequencer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ktech_soundbank ktech_soundbank;

/* Loads complete, receipt-backed banks from the Kilix Techno XDG data root.
 * Missing or damaged optional banks are ignored and their instruments use the
 * native fallback synth. The environment override is primarily for tests. */
ktech_soundbank *ktech_soundbank_load(uint32_t sample_rate, char *warning,
                                      size_t warning_size);
void ktech_soundbank_free(ktech_soundbank *bank);

const chipseq_pcm *ktech_soundbank_pcm(const ktech_soundbank *bank,
                                       ktech_role role);
bool ktech_soundbank_role_available(const ktech_soundbank *bank,
                                    ktech_role role);
uint8_t ktech_soundbank_installed_count(const ktech_soundbank *bank);
uint32_t ktech_soundbank_installed_mask(const ktech_soundbank *bank);
const char *ktech_soundbank_role_bank(ktech_role role);
int ktech_soundbank_role_bank_index(ktech_role role);

#ifdef __cplusplus
}
#endif

#endif
