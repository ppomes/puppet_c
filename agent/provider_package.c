/**
 * @file provider_package.c
 * @brief Package resource providers
 *
 * Platform-specific package management:
 * - apt (Debian/Ubuntu)
 * - dnf/yum (RedHat/Fedora/CentOS)
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

/**
 * @brief Run a command and capture output
 */
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

/**
 * @brief Execute command with optional output
 */
static int exec_command(const char *cmd, bool verbose) {
    if (verbose) {
        fprintf(stderr, "[EXEC] %s\n", cmd);
    }
    return system(cmd);
}

/* ============================================================================
 * APT Provider (Debian/Ubuntu)
 * ============================================================================ */

static bool apt_is_installed(const char *package) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "dpkg-query -W -f='${Status}' '%s' 2>/dev/null | grep -q 'install ok installed'",
             package);
    return run_command(cmd, NULL, 0) == 0;
}

static char *apt_get_version(const char *package) {
    char cmd[512];
    char output[256];
    snprintf(cmd, sizeof(cmd),
             "dpkg-query -W -f='${Version}' '%s' 2>/dev/null",
             package);

    if (run_command(cmd, output, sizeof(output)) == 0 && output[0]) {
        /* Remove trailing newline */
        char *nl = strchr(output, '\n');
        if (nl) *nl = '\0';
        return strdup(output);
    }
    return NULL;
}

static resource_state_t apt_check(const resource_t *resource, apply_context_t *ctx) {
    (void)ctx;
    const char *package = resource->title;

    if (apt_is_installed(package)) {
        return RESOURCE_STATE_INSTALLED;
    }
    return RESOURCE_STATE_ABSENT;
}

static apply_result_t apt_apply(const resource_t *resource, apply_context_t *ctx) {
    const char *package = resource->title;
    const char *ensure = resource_get_param(resource, "ensure");

    if (!ensure) ensure = "installed";

    bool is_installed = apt_is_installed(package);

    /* Handle ensure => absent/purged */
    if (strcmp(ensure, "absent") == 0 || strcmp(ensure, "purged") == 0) {
        if (!is_installed) {
            return APPLY_NOOP;
        }

        if (ctx->noop) {
            printf("  Would remove package: %s\n", package);
            return APPLY_SKIPPED;
        }

        char cmd[512];
        const char *purge = strcmp(ensure, "purged") == 0 ? "--purge" : "";
        snprintf(cmd, sizeof(cmd),
                 "DEBIAN_FRONTEND=noninteractive apt-get remove -y %s '%s' >/dev/null 2>&1",
                 purge, package);

        if (exec_command(cmd, ctx->verbose) != 0) {
            apply_context_set_error(ctx, "Failed to remove package: %s", package);
            return APPLY_FAILED;
        }

        printf("  Removed package: %s\n", package);
        return APPLY_CHANGED;
    }

    /* Handle ensure => installed/present/latest or version */
    bool need_install = !is_installed;
    bool need_upgrade = false;

    if (strcmp(ensure, "latest") == 0 && is_installed) {
        /* Check if update is available */
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "apt-cache policy '%s' 2>/dev/null | grep -q 'Candidate:.*Installed'",
                 package);
        if (run_command(cmd, NULL, 0) != 0) {
            need_upgrade = true;
        }
    }

    /* Check for specific version */
    if (strcmp(ensure, "installed") != 0 &&
        strcmp(ensure, "present") != 0 &&
        strcmp(ensure, "latest") != 0) {
        /* ensure is a version string */
        if (is_installed) {
            char *current_version = apt_get_version(package);
            if (current_version) {
                if (strcmp(current_version, ensure) != 0) {
                    need_install = true;  /* Different version needed */
                }
                free(current_version);
            }
        } else {
            need_install = true;
        }
    }

    if (!need_install && !need_upgrade) {
        return APPLY_NOOP;
    }

    if (ctx->noop) {
        printf("  Would %s package: %s\n",
               need_upgrade ? "upgrade" : "install", package);
        return APPLY_SKIPPED;
    }

    /* Install/upgrade package */
    char cmd[512];

    /* Update package list first */
    exec_command("apt-get update -qq 2>/dev/null", ctx->verbose);

    if (strcmp(ensure, "installed") == 0 ||
        strcmp(ensure, "present") == 0 ||
        strcmp(ensure, "latest") == 0) {
        snprintf(cmd, sizeof(cmd),
                 "DEBIAN_FRONTEND=noninteractive apt-get install -y '%s' >/dev/null 2>&1",
                 package);
    } else {
        /* Specific version */
        snprintf(cmd, sizeof(cmd),
                 "DEBIAN_FRONTEND=noninteractive apt-get install -y '%s=%s' >/dev/null 2>&1",
                 package, ensure);
    }

    if (exec_command(cmd, ctx->verbose) != 0) {
        apply_context_set_error(ctx, "Failed to install package: %s", package);
        return APPLY_FAILED;
    }

    printf("  %s package: %s\n",
           need_upgrade ? "Upgraded" : "Installed", package);
    return APPLY_CHANGED;
}

/* ============================================================================
 * DNF Provider (RedHat/Fedora/CentOS)
 * ============================================================================ */

static bool dnf_is_installed(const char *package) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rpm -q '%s' >/dev/null 2>&1", package);
    return run_command(cmd, NULL, 0) == 0;
}

static char *dnf_get_version(const char *package) {
    char cmd[512];
    char output[256];
    snprintf(cmd, sizeof(cmd),
             "rpm -q --qf '%%{VERSION}-%%{RELEASE}' '%s' 2>/dev/null",
             package);

    if (run_command(cmd, output, sizeof(output)) == 0 && output[0]) {
        char *nl = strchr(output, '\n');
        if (nl) *nl = '\0';
        return strdup(output);
    }
    return NULL;
}

static resource_state_t dnf_check(const resource_t *resource, apply_context_t *ctx) {
    (void)ctx;
    const char *package = resource->title;

    if (dnf_is_installed(package)) {
        return RESOURCE_STATE_INSTALLED;
    }
    return RESOURCE_STATE_ABSENT;
}

static apply_result_t dnf_apply(const resource_t *resource, apply_context_t *ctx) {
    const char *package = resource->title;
    const char *ensure = resource_get_param(resource, "ensure");

    if (!ensure) ensure = "installed";

    bool is_installed = dnf_is_installed(package);

    /* Handle ensure => absent */
    if (strcmp(ensure, "absent") == 0 || strcmp(ensure, "purged") == 0) {
        if (!is_installed) {
            return APPLY_NOOP;
        }

        if (ctx->noop) {
            printf("  Would remove package: %s\n", package);
            return APPLY_SKIPPED;
        }

        char cmd[512];
        snprintf(cmd, sizeof(cmd), "dnf remove -y '%s' >/dev/null 2>&1", package);

        if (exec_command(cmd, ctx->verbose) != 0) {
            apply_context_set_error(ctx, "Failed to remove package: %s", package);
            return APPLY_FAILED;
        }

        printf("  Removed package: %s\n", package);
        return APPLY_CHANGED;
    }

    /* Handle ensure => installed/present/latest */
    bool need_install = !is_installed;
    bool need_upgrade = false;

    if (strcmp(ensure, "latest") == 0 && is_installed) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "dnf check-update '%s' >/dev/null 2>&1; test $? -eq 100",
                 package);
        if (run_command(cmd, NULL, 0) == 0) {
            need_upgrade = true;
        }
    }

    /* Specific version check */
    if (strcmp(ensure, "installed") != 0 &&
        strcmp(ensure, "present") != 0 &&
        strcmp(ensure, "latest") != 0) {
        if (is_installed) {
            char *current_version = dnf_get_version(package);
            if (current_version) {
                if (strcmp(current_version, ensure) != 0) {
                    need_install = true;
                }
                free(current_version);
            }
        } else {
            need_install = true;
        }
    }

    if (!need_install && !need_upgrade) {
        return APPLY_NOOP;
    }

    if (ctx->noop) {
        printf("  Would %s package: %s\n",
               need_upgrade ? "upgrade" : "install", package);
        return APPLY_SKIPPED;
    }

    char cmd[512];
    if (need_upgrade) {
        snprintf(cmd, sizeof(cmd), "dnf upgrade -y '%s' >/dev/null 2>&1", package);
    } else if (strcmp(ensure, "installed") == 0 ||
               strcmp(ensure, "present") == 0 ||
               strcmp(ensure, "latest") == 0) {
        snprintf(cmd, sizeof(cmd), "dnf install -y '%s' >/dev/null 2>&1", package);
    } else {
        snprintf(cmd, sizeof(cmd), "dnf install -y '%s-%s' >/dev/null 2>&1",
                 package, ensure);
    }

    if (exec_command(cmd, ctx->verbose) != 0) {
        apply_context_set_error(ctx, "Failed to install package: %s", package);
        return APPLY_FAILED;
    }

    printf("  %s package: %s\n",
           need_upgrade ? "Upgraded" : "Installed", package);
    return APPLY_CHANGED;
}

/* ============================================================================
 * Provider Definitions
 * ============================================================================ */

static const provider_t apt_provider = {
    .name = "apt",
    .resource_type = "package",
    .os_family = OS_FAMILY_DEBIAN,
    .apply = apt_apply,
    .check = apt_check,
    .cleanup = NULL
};

static const provider_t dnf_provider = {
    .name = "dnf",
    .resource_type = "package",
    .os_family = OS_FAMILY_REDHAT,
    .apply = dnf_apply,
    .check = dnf_check,
    .cleanup = NULL
};

void provider_package_register(void) {
    provider_register(&apt_provider);
    provider_register(&dnf_provider);
}
