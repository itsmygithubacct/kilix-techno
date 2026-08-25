#include "kilix_techno/lazy_install.h"

#include "kilix_techno/soundbank.h"

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

extern char **environ;

void ktech_lazy_install_init(ktech_lazy_install *install)
{
    if (install) memset(install, 0, sizeof *install);
}

bool ktech_lazy_install_start(ktech_lazy_install *install, ktech_role role,
                              char *error, size_t error_size)
{
    if (error && error_size > 0u) error[0] = '\0';
    const char *bank = ktech_soundbank_role_bank(role);
    if (!install || install->pid > 0 || !bank) {
        if (error && error_size > 0u)
            (void)snprintf(error, error_size, "%s",
                           install && install->pid > 0 ?
                           "another soundbank install is running" :
                           "instrument has no optional soundbank");
        return false;
    }
    int count = snprintf(install->package, sizeof install->package,
                         "techno-%s", bank);
    if (count < 0 || (size_t)count >= sizeof install->package) return false;
    char *arguments[] = {
        (char *)"kilix", (char *)"install", install->package, NULL
    };
    pid_t child = 0;
    int result = posix_spawnp(&child, "kilix", NULL, NULL,
                              arguments, environ);
    if (result != 0) {
        if (error && error_size > 0u)
            (void)snprintf(error, error_size, "could not start kilix: %s",
                           strerror(result));
        install->package[0] = '\0';
        return false;
    }
    install->pid = child;
    install->role = role;
    return true;
}

ktech_lazy_install_result ktech_lazy_install_poll(
    ktech_lazy_install *install, int *exit_status)
{
    if (exit_status) *exit_status = 0;
    if (!install || install->pid <= 0) return KTECH_LAZY_IDLE;
    int status = 0;
    pid_t result;
    do {
        result = waitpid(install->pid, &status, WNOHANG);
    } while (result < 0 && errno == EINTR);
    if (result == 0) return KTECH_LAZY_RUNNING;
    install->pid = 0;
    if (result < 0) {
        if (exit_status) *exit_status = errno;
        return KTECH_LAZY_FAILED;
    }
    if (WIFEXITED(status)) {
        if (exit_status) *exit_status = WEXITSTATUS(status);
        return WEXITSTATUS(status) == 0 ? KTECH_LAZY_SUCCEEDED :
                                         KTECH_LAZY_FAILED;
    }
    if (exit_status && WIFSIGNALED(status))
        *exit_status = 128 + WTERMSIG(status);
    return KTECH_LAZY_FAILED;
}

bool ktech_lazy_install_running(const ktech_lazy_install *install)
{
    return install && install->pid > 0;
}
