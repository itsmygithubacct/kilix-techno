#ifndef KILIX_TECHNO_LAZY_INSTALL_H
#define KILIX_TECHNO_LAZY_INSTALL_H

#include "kilix_techno/project.h"

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ktech_lazy_install {
    pid_t pid;
    ktech_role role;
    char package[64];
} ktech_lazy_install;

typedef enum ktech_lazy_install_result {
    KTECH_LAZY_IDLE = 0,
    KTECH_LAZY_RUNNING,
    KTECH_LAZY_SUCCEEDED,
    KTECH_LAZY_FAILED
} ktech_lazy_install_result;

void ktech_lazy_install_init(ktech_lazy_install *install);
bool ktech_lazy_install_start(ktech_lazy_install *install, ktech_role role,
                              char *error, size_t error_size);
ktech_lazy_install_result ktech_lazy_install_poll(
    ktech_lazy_install *install, int *exit_status);
bool ktech_lazy_install_running(const ktech_lazy_install *install);

#ifdef __cplusplus
}
#endif

#endif
