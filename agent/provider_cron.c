/**
 * @file provider_cron.c
 * @brief Cron resource provider
 *
 * Manages cron jobs via user crontabs.
 *
 * Supported parameters:
 *   - command: The command to execute (required)
 *   - user: The user whose crontab to modify (default: root)
 *   - minute: Minute (0-59, or step values)
 *   - hour: Hour (0-23, or step values)
 *   - monthday: Day of month (1-31, or step values)
 *   - month: Month (1-12, or step values)
 *   - weekday: Day of week (0-7, 0 and 7 are Sunday)
 *   - special: Special time (reboot, yearly, monthly, weekly, daily, hourly)
 *   - ensure: present or absent (default: present)
 *   - environment: Environment variables (comma-separated KEY=value pairs)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "puppet_memory.h"
#include "color_output.h"
#include "puppet_provider.h"
#include "exec_utils.h"

#define CRON_MARKER "# Puppet Name: "
#define MAX_CRONTAB_SIZE (64 * 1024)
#define MAX_LINE_SIZE 4096

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Read a user's crontab
 * @param user Username (NULL for current user)
 * @return Allocated string with crontab contents, or NULL on error
 */
static char *read_crontab(const char *user) {
    char cmd[256];

    if (user) {
        char *escaped_user = shell_escape(user);
        if (!escaped_user) return NULL;
        snprintf(cmd, sizeof(cmd), "crontab -u %s -l 2>/dev/null", escaped_user);
        puppet_free(escaped_user);
    } else {
        snprintf(cmd, sizeof(cmd), "crontab -l 2>/dev/null");
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        return NULL;
    }

    char *buffer = puppet_malloc(MAX_CRONTAB_SIZE);
    if (!buffer) {
        pclose(fp);
        return NULL;
    }

    size_t total = 0;
    size_t n;
    while ((n = fread(buffer + total, 1, MAX_CRONTAB_SIZE - total - 1, fp)) > 0) {
        total += n;
        if (total >= MAX_CRONTAB_SIZE - 1) break;
    }
    buffer[total] = '\0';

    pclose(fp);
    return buffer;
}

/**
 * @brief Write a user's crontab
 * @param user Username (NULL for current user)
 * @param content New crontab content
 * @return 0 on success, -1 on error
 */
static int write_crontab(const char *user, const char *content) {
    char cmd[256];

    if (user) {
        char *escaped_user = shell_escape(user);
        if (!escaped_user) return -1;
        snprintf(cmd, sizeof(cmd), "crontab -u %s -", escaped_user);
        puppet_free(escaped_user);
    } else {
        snprintf(cmd, sizeof(cmd), "crontab -");
    }

    FILE *fp = popen(cmd, "w");
    if (!fp) {
        return -1;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, fp);

    int status = pclose(fp);

    if (written != len || status != 0) {
        return -1;
    }

    return 0;
}

/**
 * @brief Find a puppet-managed cron entry in crontab
 * @param crontab The crontab content
 * @param name The cron job name (from resource title)
 * @param entry_start Output: pointer to start of entry (marker line)
 * @param entry_end Output: pointer to end of entry (after cron line)
 * @return true if found
 */
static bool find_cron_entry(const char *crontab, const char *name,
                            const char **entry_start, const char **entry_end) {
    if (!crontab || !name) return false;

    char marker[MAX_LINE_SIZE];
    snprintf(marker, sizeof(marker), "%s%s", CRON_MARKER, name);

    const char *pos = crontab;
    while ((pos = strstr(pos, marker)) != NULL) {
        /* Verify it's at start of line */
        if (pos != crontab && *(pos - 1) != '\n') {
            pos++;
            continue;
        }

        /* Check that marker is followed by newline (exact match) */
        const char *marker_end = pos + strlen(marker);
        if (*marker_end != '\n' && *marker_end != '\0') {
            pos++;
            continue;
        }

        *entry_start = pos;

        /* Find end of the cron line (skip marker line, then the actual cron line) */
        const char *line_end = strchr(pos, '\n');
        if (line_end) {
            line_end++; /* Move past marker line */
            const char *cron_end = strchr(line_end, '\n');
            if (cron_end) {
                *entry_end = cron_end + 1;
            } else {
                *entry_end = line_end + strlen(line_end);
            }
        } else {
            *entry_end = pos + strlen(pos);
        }

        return true;
    }

    return false;
}

/**
 * @brief Build a cron time specification
 * @param minute Minute field
 * @param hour Hour field
 * @param monthday Day of month field
 * @param month Month field
 * @param weekday Day of week field
 * @param special Special time string (overrides other fields)
 * @param buffer Output buffer
 * @param bufsize Buffer size
 */
static void build_cron_time(const char *minute, const char *hour,
                            const char *monthday, const char *month,
                            const char *weekday, const char *special,
                            char *buffer, size_t bufsize) {
    if (special && strlen(special) > 0) {
        /* Special schedules need @ prefix if not already present */
        if (special[0] == '@') {
            snprintf(buffer, bufsize, "%s", special);
        } else {
            snprintf(buffer, bufsize, "@%s", special);
        }
    } else {
        snprintf(buffer, bufsize, "%s %s %s %s %s",
                 minute ? minute : "*",
                 hour ? hour : "*",
                 monthday ? monthday : "*",
                 month ? month : "*",
                 weekday ? weekday : "*");
    }
}

/**
 * @brief Build a complete cron entry (marker + cron line)
 * @return Allocated string with the entry
 */
static char *build_cron_entry(const char *name, const char *command,
                              const char *minute, const char *hour,
                              const char *monthday, const char *month,
                              const char *weekday, const char *special,
                              const char *environment) {
    char time_spec[256];
    build_cron_time(minute, hour, monthday, month, weekday, special,
                    time_spec, sizeof(time_spec));

    /* Calculate size needed */
    size_t size = strlen(CRON_MARKER) + strlen(name) + 1 +  /* marker line */
                  strlen(time_spec) + 1 + strlen(command) + 2; /* cron line + newlines */

    /* Add space for environment if present */
    if (environment && strlen(environment) > 0) {
        size += strlen(environment) + 2;
    }

    char *entry = puppet_malloc(size);
    if (!entry) return NULL;

    if (environment && strlen(environment) > 0) {
        snprintf(entry, size, "%s%s\n%s\n%s %s\n",
                 CRON_MARKER, name, environment, time_spec, command);
    } else {
        snprintf(entry, size, "%s%s\n%s %s\n",
                 CRON_MARKER, name, time_spec, command);
    }

    return entry;
}

/**
 * @brief Extract the cron line from an entry (skipping marker)
 */
static char *extract_cron_line(const char *entry_start, const char *entry_end) {
    /* Skip the marker line */
    const char *cron_start = strchr(entry_start, '\n');
    if (!cron_start || cron_start >= entry_end) return NULL;
    cron_start++; /* Skip newline */

    size_t len = entry_end - cron_start;
    if (len > 0 && *(entry_end - 1) == '\n') {
        len--;
    }

    char *line = puppet_malloc(len + 1);
    if (!line) return NULL;

    memcpy(line, cron_start, len);
    line[len] = '\0';

    return line;
}

/* ============================================================================
 * Cron Provider Implementation
 * ============================================================================ */

static resource_state_t cron_check(const resource_t *resource, apply_context_t *ctx) {
    (void)ctx;

    const char *user = resource_get_param(resource, "user");
    if (!user) user = "root";

    char *crontab = read_crontab(user);
    if (!crontab) {
        return RESOURCE_STATE_ABSENT;
    }

    const char *entry_start, *entry_end;
    bool found = find_cron_entry(crontab, resource->title, &entry_start, &entry_end);

    puppet_free(crontab);

    return found ? RESOURCE_STATE_PRESENT : RESOURCE_STATE_ABSENT;
}

static apply_result_t cron_apply(const resource_t *resource, apply_context_t *ctx) {
    const char *command = resource_get_param(resource, "command");
    const char *user = resource_get_param(resource, "user");
    const char *minute = resource_get_param(resource, "minute");
    const char *hour = resource_get_param(resource, "hour");
    const char *monthday = resource_get_param(resource, "monthday");
    const char *month = resource_get_param(resource, "month");
    const char *weekday = resource_get_param(resource, "weekday");
    const char *special = resource_get_param(resource, "special");
    const char *ensure = resource_get_param(resource, "ensure");
    const char *environment = resource_get_param(resource, "environment");

    /* Default values */
    if (!user) user = "root";
    if (!ensure) ensure = "present";

    /* Validate required parameters */
    bool want_present = (strcmp(ensure, "present") == 0);
    if (want_present && (!command || strlen(command) == 0)) {
        apply_context_set_error(ctx, "Cron resource requires 'command' parameter");
        return APPLY_FAILED;
    }

    /* Read current crontab */
    char *crontab = read_crontab(user);
    if (!crontab) {
        /* No crontab exists - create empty one */
        crontab = puppet_strdup("");
        if (!crontab) {
            apply_context_set_error(ctx, "Memory allocation failed");
            return APPLY_FAILED;
        }
    }

    /* Find existing entry */
    const char *entry_start = NULL, *entry_end = NULL;
    bool found = find_cron_entry(crontab, resource->title, &entry_start, &entry_end);

    /* Determine what to do */
    if (!want_present) {
        /* ensure => absent */
        if (!found) {
            puppet_free(crontab);
            return APPLY_SUCCESS; /* Already absent */
        }

        if (ctx->noop) {
            print_resource_noop("Cron", resource->title, "ensure", "would be removed");
            puppet_free(crontab);
            return APPLY_SKIPPED;
        }

        /* Remove the entry */
        size_t before_len = entry_start - crontab;
        size_t after_len = strlen(entry_end);
        size_t new_size = before_len + after_len + 1;

        char *new_crontab = puppet_malloc(new_size);
        if (!new_crontab) {
            puppet_free(crontab);
            apply_context_set_error(ctx, "Memory allocation failed");
            return APPLY_FAILED;
        }

        memcpy(new_crontab, crontab, before_len);
        memcpy(new_crontab + before_len, entry_end, after_len);
        new_crontab[before_len + after_len] = '\0';

        if (write_crontab(user, new_crontab) != 0) {
            puppet_free(crontab);
            puppet_free(new_crontab);
            apply_context_set_error(ctx, "Failed to write crontab");
            return APPLY_FAILED;
        }

        puppet_free(crontab);
        puppet_free(new_crontab);

        print_resource_change("Cron", resource->title, "ensure", "removed");
        return APPLY_CHANGED;
    }

    /* ensure => present */
    char *new_entry = build_cron_entry(resource->title, command,
                                        minute, hour, monthday, month,
                                        weekday, special, environment);
    if (!new_entry) {
        puppet_free(crontab);
        apply_context_set_error(ctx, "Failed to build cron entry");
        return APPLY_FAILED;
    }

    if (found) {
        /* Check if entry needs updating */
        char *existing_line = extract_cron_line(entry_start, entry_end);
        char time_spec[256];
        build_cron_time(minute, hour, monthday, month, weekday, special,
                        time_spec, sizeof(time_spec));

        char expected_line[MAX_LINE_SIZE];
        if (environment && strlen(environment) > 0) {
            snprintf(expected_line, sizeof(expected_line), "%s\n%s %s",
                     environment, time_spec, command);
        } else {
            snprintf(expected_line, sizeof(expected_line), "%s %s",
                     time_spec, command);
        }

        if (existing_line && strcmp(existing_line, expected_line) == 0) {
            /* Already correct */
            puppet_free(existing_line);
            puppet_free(new_entry);
            puppet_free(crontab);
            return APPLY_SUCCESS;
        }
        puppet_free(existing_line);

        if (ctx->noop) {
            print_resource_noop("Cron", resource->title, "schedule", "would be updated");
            puppet_free(new_entry);
            puppet_free(crontab);
            return APPLY_SKIPPED;
        }

        /* Replace existing entry */
        size_t before_len = entry_start - crontab;
        size_t after_len = strlen(entry_end);
        size_t new_entry_len = strlen(new_entry);
        size_t new_size = before_len + new_entry_len + after_len + 1;

        char *new_crontab = puppet_malloc(new_size);
        if (!new_crontab) {
            puppet_free(new_entry);
            puppet_free(crontab);
            apply_context_set_error(ctx, "Memory allocation failed");
            return APPLY_FAILED;
        }

        memcpy(new_crontab, crontab, before_len);
        memcpy(new_crontab + before_len, new_entry, new_entry_len);
        memcpy(new_crontab + before_len + new_entry_len, entry_end, after_len);
        new_crontab[before_len + new_entry_len + after_len] = '\0';

        if (write_crontab(user, new_crontab) != 0) {
            puppet_free(new_entry);
            puppet_free(crontab);
            puppet_free(new_crontab);
            apply_context_set_error(ctx, "Failed to write crontab");
            return APPLY_FAILED;
        }

        puppet_free(new_entry);
        puppet_free(crontab);
        puppet_free(new_crontab);

        print_resource_change("Cron", resource->title, "schedule", "updated");
        return APPLY_CHANGED;

    } else {
        /* Add new entry */
        if (ctx->noop) {
            print_resource_noop("Cron", resource->title, "ensure", "would be created");
            puppet_free(new_entry);
            puppet_free(crontab);
            return APPLY_SKIPPED;
        }

        size_t crontab_len = strlen(crontab);
        size_t new_entry_len = strlen(new_entry);
        size_t new_size = crontab_len + new_entry_len + 2; /* +2 for possible newline and null */

        char *new_crontab = puppet_malloc(new_size);
        if (!new_crontab) {
            puppet_free(new_entry);
            puppet_free(crontab);
            apply_context_set_error(ctx, "Memory allocation failed");
            return APPLY_FAILED;
        }

        /* Ensure crontab ends with newline before adding new entry */
        if (crontab_len > 0 && crontab[crontab_len - 1] != '\n') {
            snprintf(new_crontab, new_size, "%s\n%s", crontab, new_entry);
        } else {
            snprintf(new_crontab, new_size, "%s%s", crontab, new_entry);
        }

        if (write_crontab(user, new_crontab) != 0) {
            puppet_free(new_entry);
            puppet_free(crontab);
            puppet_free(new_crontab);
            apply_context_set_error(ctx, "Failed to write crontab");
            return APPLY_FAILED;
        }

        puppet_free(new_entry);
        puppet_free(crontab);
        puppet_free(new_crontab);

        print_resource_change("Cron", resource->title, "ensure", "created");
        return APPLY_CHANGED;
    }
}

/* ============================================================================
 * Provider Registration
 * ============================================================================ */

static const provider_t cron_provider = {
    .name = "cron",
    .resource_type = "cron",
    .os_family = 0,  /* Generic - works on all POSIX platforms */
    .apply = cron_apply,
    .check = cron_check,
    .cleanup = NULL
};

void provider_cron_register(void) {
    provider_register(&cron_provider);
}
