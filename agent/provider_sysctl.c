/**
 * @file provider_sysctl.c
 * @brief Sysctl resource provider - manages kernel parameters
 *
 * Manages kernel parameters via /proc/sys/ and /etc/sysctl.d/
 *
 * Supported parameters:
 *   - name: Kernel parameter name (e.g., 'net.ipv4.ip_forward')
 *   - value: Desired value for the parameter
 *   - ensure: present (default) or absent
 *   - permanent: Whether to persist in /etc/sysctl.d/ (default: true)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

#include "puppet_provider.h"
#include "puppet_memory.h"
#include "color_output.h"

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Convert sysctl name to /proc/sys path
 *
 * Converts dots to slashes: net.ipv4.ip_forward -> /proc/sys/net/ipv4/ip_forward
 */
static char *sysctl_name_to_path(const char *name) {
    if (!name) return NULL;

    size_t len = strlen(name) + strlen("/proc/sys/") + 1;
    char *path = puppet_malloc(len);
    if (!path) return NULL;

    strcpy(path, "/proc/sys/");
    char *dst = path + strlen("/proc/sys/");

    for (const char *src = name; *src; src++) {
        *dst++ = (*src == '.') ? '/' : *src;
    }
    *dst = '\0';

    return path;
}

/**
 * @brief Get current value of a sysctl parameter
 */
static char *get_sysctl_value(const char *name) {
    char *path = sysctl_name_to_path(name);
    if (!path) return NULL;

    FILE *fp = fopen(path, "r");
    puppet_free(path);

    if (!fp) return NULL;

    char buf[1024];
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    /* Trim trailing whitespace/newline */
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r' ||
                       buf[len-1] == ' ' || buf[len-1] == '\t')) {
        buf[--len] = '\0';
    }

    return puppet_strdup(buf);
}

/**
 * @brief Set a sysctl parameter value (runtime)
 */
static int set_sysctl_value(const char *name, const char *value) {
    char *path = sysctl_name_to_path(name);
    if (!path) return -1;

    FILE *fp = fopen(path, "w");
    puppet_free(path);

    if (!fp) return -1;

    int result = fprintf(fp, "%s\n", value);
    fclose(fp);

    return (result > 0) ? 0 : -1;
}

/**
 * @brief Get the sysctl.d config file path for a parameter
 */
static char *get_sysctl_conf_path(const char *name) {
    char path[512];

    /* Create a safe filename from the parameter name */
    char safe_name[256];
    size_t j = 0;
    for (size_t i = 0; name[i] && j < sizeof(safe_name) - 1; i++) {
        if (isalnum(name[i]) || name[i] == '_' || name[i] == '-') {
            safe_name[j++] = name[i];
        } else if (name[i] == '.') {
            safe_name[j++] = '-';
        }
    }
    safe_name[j] = '\0';

    snprintf(path, sizeof(path), "/etc/sysctl.d/99-puppet-%s.conf", safe_name);
    return puppet_strdup(path);
}

/**
 * @brief Check if a sysctl parameter exists in sysctl.d
 */
static int check_sysctl_permanent(const char *name, char **current_value) {
    char *conf_path = get_sysctl_conf_path(name);
    if (!conf_path) return -1;

    FILE *fp = fopen(conf_path, "r");
    puppet_free(conf_path);

    if (!fp) return 0; /* File doesn't exist */

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        /* Skip comments and empty lines */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        /* Parse name = value */
        char *eq = strchr(p, '=');
        if (!eq) continue;

        /* Extract parameter name */
        char param_name[256];
        size_t name_len = eq - p;
        while (name_len > 0 && (p[name_len-1] == ' ' || p[name_len-1] == '\t')) {
            name_len--;
        }
        if (name_len >= sizeof(param_name)) name_len = sizeof(param_name) - 1;
        strncpy(param_name, p, name_len);
        param_name[name_len] = '\0';

        if (strcmp(param_name, name) == 0) {
            if (current_value) {
                /* Extract value */
                char *val = eq + 1;
                while (*val == ' ' || *val == '\t') val++;

                /* Trim trailing whitespace */
                size_t val_len = strlen(val);
                while (val_len > 0 && (val[val_len-1] == '\n' || val[val_len-1] == '\r' ||
                                       val[val_len-1] == ' ' || val[val_len-1] == '\t')) {
                    val_len--;
                }

                char *value_copy = puppet_malloc(val_len + 1);
                if (value_copy) {
                    strncpy(value_copy, val, val_len);
                    value_copy[val_len] = '\0';
                    *current_value = value_copy;
                }
            }
            fclose(fp);
            return 1; /* Found */
        }
    }

    fclose(fp);
    return 0; /* Not found */
}

/**
 * @brief Write a sysctl parameter to sysctl.d for persistence
 */
static int set_sysctl_permanent(const char *name, const char *value) {
    char *conf_path = get_sysctl_conf_path(name);
    if (!conf_path) return -1;

    /* Ensure directory exists */
    struct stat st;
    if (stat("/etc/sysctl.d", &st) != 0) {
        if (mkdir("/etc/sysctl.d", 0755) != 0) {
            puppet_free(conf_path);
            return -1;
        }
    }

    FILE *fp = fopen(conf_path, "w");
    puppet_free(conf_path);

    if (!fp) return -1;

    fprintf(fp, "# Managed by Puppet\n");
    fprintf(fp, "%s = %s\n", name, value);
    fclose(fp);

    return 0;
}

/**
 * @brief Remove a sysctl parameter from sysctl.d
 */
static int remove_sysctl_permanent(const char *name) {
    char *conf_path = get_sysctl_conf_path(name);
    if (!conf_path) return -1;

    int result = unlink(conf_path);
    puppet_free(conf_path);

    return (result == 0 || errno == ENOENT) ? 0 : -1;
}

/* ============================================================================
 * Provider Implementation
 * ============================================================================ */

static apply_result_t sysctl_apply(const resource_t *resource, apply_context_t *ctx) {
    const char *name = resource->title;
    const char *ensure = resource_get_param(resource, "ensure");
    const char *value = resource_get_param(resource, "value");
    const char *permanent_str = resource_get_param(resource, "permanent");

    /* Defaults */
    if (!ensure) ensure = "present";
    bool permanent = true;
    if (permanent_str && (strcmp(permanent_str, "false") == 0 || strcmp(permanent_str, "0") == 0)) {
        permanent = false;
    }

    /* Handle ensure => absent */
    if (strcmp(ensure, "absent") == 0) {
        if (!permanent) {
            /* Nothing to do - can't "unset" runtime sysctl */
            return APPLY_SUCCESS;
        }

        /* Check if permanent config exists */
        if (!check_sysctl_permanent(name, NULL)) {
            return APPLY_SUCCESS; /* Already absent */
        }

        if (ctx->noop) {
            print_resource_noop("Sysctl", name, "ensure", "would remove from sysctl.d");
            return APPLY_SKIPPED;
        }

        if (remove_sysctl_permanent(name) != 0) {
            apply_context_set_error(ctx, "Failed to remove sysctl.d config");
            return APPLY_FAILED;
        }

        print_resource_change("Sysctl", name, "ensure", "present", "absent");
        return APPLY_CHANGED;
    }

    /* ensure => present */
    if (!value) {
        apply_context_set_error(ctx, "Sysctl resource requires 'value' parameter");
        return APPLY_FAILED;
    }

    /* Check current runtime value */
    char *current = get_sysctl_value(name);
    if (!current) {
        apply_context_set_error(ctx, "Sysctl parameter '%s' does not exist", name);
        return APPLY_FAILED;
    }

    bool runtime_changed = (strcmp(current, value) != 0);
    bool permanent_changed = false;

    if (permanent) {
        char *perm_value = NULL;
        int exists = check_sysctl_permanent(name, &perm_value);

        if (!exists || (perm_value && strcmp(perm_value, value) != 0)) {
            permanent_changed = true;
        }
        puppet_free(perm_value);
    }

    /* Nothing to change? */
    if (!runtime_changed && !permanent_changed) {
        puppet_free(current);
        return APPLY_SUCCESS;
    }

    /* Noop mode */
    if (ctx->noop) {
        if (runtime_changed) {
            print_resource_noop("Sysctl", name, "value", "would change");
        }
        if (permanent_changed) {
            print_resource_noop("Sysctl", name, "permanent", "would update sysctl.d");
        }
        puppet_free(current);
        return APPLY_SKIPPED;
    }

    /* Apply changes */
    if (runtime_changed) {
        if (set_sysctl_value(name, value) != 0) {
            apply_context_set_error(ctx, "Failed to set sysctl value");
            puppet_free(current);
            return APPLY_FAILED;
        }
        print_resource_change("Sysctl", name, "value", current, value);
    }

    if (permanent_changed) {
        if (set_sysctl_permanent(name, value) != 0) {
            apply_context_set_error(ctx, "Failed to write sysctl.d config");
            puppet_free(current);
            return APPLY_FAILED;
        }
        if (!runtime_changed) {
            print_resource_change("Sysctl", name, "permanent", "absent", "present");
        }
    }

    puppet_free(current);
    return APPLY_CHANGED;
}

/* Provider definition */
static const provider_t sysctl_provider = {
    .name = "sysctl",
    .resource_type = "sysctl",
    .os_family = 0, /* Generic - works on all Linux */
    .apply = sysctl_apply,
    .cleanup = NULL
};

void provider_sysctl_register(void) {
    provider_register(&sysctl_provider);
}
