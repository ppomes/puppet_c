/**
 * @file provider_service.c
 * @brief Service resource provider (systemd)
 *
 * Manages system services using systemd (default on modern Linux).
 * Generic provider works on all systemd-based distributions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "puppet_provider.h"

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static int run_command(const char *cmd, char *output, size_t output_size) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    if (output && output_size > 0) {
        output[0] = '\0';
        size_t total = 0;
        char buf[256];
        while (fgets(buf, sizeof(buf), fp) && total < output_size - 1) {
            size_t len = strlen(buf);
            if (total + len < output_size - 1) {
                strcat(output, buf);
                total += len;
            }
        }
    }

    int status = pclose(fp);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int exec_command(const char *cmd, bool verbose) {
    if (verbose) {
        fprintf(stderr, "[EXEC] %s\n", cmd);
    }
    return system(cmd);
}

/* ============================================================================
 * Systemd Service Functions
 * ============================================================================ */

static bool systemd_is_active(const char *service) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "systemctl is-active '%s' >/dev/null 2>&1", service);
    return run_command(cmd, NULL, 0) == 0;
}

static bool systemd_is_enabled(const char *service) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "systemctl is-enabled '%s' >/dev/null 2>&1", service);
    return run_command(cmd, NULL, 0) == 0;
}

static bool systemd_service_exists(const char *service) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "systemctl list-unit-files '%s.service' 2>/dev/null | grep -q '%s'",
             service, service);
    return run_command(cmd, NULL, 0) == 0;
}

static int systemd_start(const char *service, bool verbose) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "systemctl start '%s' 2>&1", service);
    return exec_command(cmd, verbose);
}

static int systemd_stop(const char *service, bool verbose) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "systemctl stop '%s' 2>&1", service);
    return exec_command(cmd, verbose);
}

static int systemd_restart(const char *service, bool verbose) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "systemctl restart '%s' 2>&1", service);
    return exec_command(cmd, verbose);
}

static int systemd_enable(const char *service, bool verbose) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "systemctl enable '%s' 2>&1", service);
    return exec_command(cmd, verbose);
}

static int systemd_disable(const char *service, bool verbose) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "systemctl disable '%s' 2>&1", service);
    return exec_command(cmd, verbose);
}

/* ============================================================================
 * Systemd Provider Implementation
 * ============================================================================ */

static resource_state_t systemd_check(const resource_t *resource, apply_context_t *ctx) {
    (void)ctx;
    const char *service = resource->title;

    if (systemd_is_active(service)) {
        return RESOURCE_STATE_RUNNING;
    }
    return RESOURCE_STATE_STOPPED;
}

static apply_result_t systemd_apply(const resource_t *resource, apply_context_t *ctx) {
    const char *service = resource->title;
    const char *ensure = resource_get_param(resource, "ensure");
    const char *enable = resource_get_param(resource, "enable");

    /* Default values */
    if (!ensure) ensure = "running";

    bool changed = false;

    /* Check if service exists */
    if (!systemd_service_exists(service)) {
        apply_context_set_error(ctx, "Service not found: %s", service);
        return APPLY_FAILED;
    }

    bool is_active = systemd_is_active(service);
    bool is_enabled = systemd_is_enabled(service);

    /* Handle ensure parameter */
    if (strcmp(ensure, "running") == 0) {
        if (!is_active) {
            if (ctx->noop) {
                printf("  Would start service: %s\n", service);
            } else {
                if (systemd_start(service, ctx->verbose) != 0) {
                    apply_context_set_error(ctx, "Failed to start service: %s", service);
                    return APPLY_FAILED;
                }
                printf("  Started service: %s\n", service);
                changed = true;
            }
        }
    } else if (strcmp(ensure, "stopped") == 0) {
        if (is_active) {
            if (ctx->noop) {
                printf("  Would stop service: %s\n", service);
            } else {
                if (systemd_stop(service, ctx->verbose) != 0) {
                    apply_context_set_error(ctx, "Failed to stop service: %s", service);
                    return APPLY_FAILED;
                }
                printf("  Stopped service: %s\n", service);
                changed = true;
            }
        }
    }

    /* Handle enable parameter */
    if (enable) {
        bool want_enabled = (strcmp(enable, "true") == 0);

        if (want_enabled && !is_enabled) {
            if (ctx->noop) {
                printf("  Would enable service: %s\n", service);
            } else {
                if (systemd_enable(service, ctx->verbose) != 0) {
                    apply_context_set_error(ctx, "Failed to enable service: %s", service);
                    return APPLY_FAILED;
                }
                printf("  Enabled service: %s\n", service);
                changed = true;
            }
        } else if (!want_enabled && is_enabled) {
            if (ctx->noop) {
                printf("  Would disable service: %s\n", service);
            } else {
                if (systemd_disable(service, ctx->verbose) != 0) {
                    apply_context_set_error(ctx, "Failed to disable service: %s", service);
                    return APPLY_FAILED;
                }
                printf("  Disabled service: %s\n", service);
                changed = true;
            }
        }
    }

    if (ctx->noop && changed) {
        return APPLY_SKIPPED;
    }

    return changed ? APPLY_CHANGED : APPLY_NOOP;
}

/* ============================================================================
 * Provider Definition
 * ============================================================================ */

static const provider_t systemd_provider = {
    .name = "systemd",
    .resource_type = "service",
    .os_family = 0,  /* Generic - works on all systemd-based systems */
    .apply = systemd_apply,
    .check = systemd_check,
    .cleanup = NULL
};

void provider_service_register(void) {
    provider_register(&systemd_provider);
}
