/**
 * @file provider_host.c
 * @brief Host resource provider
 *
 * Manages entries in /etc/hosts file.
 *
 * Supported parameters:
 *   - name (title): The hostname to manage
 *   - ip: The IP address for the host
 *   - host_aliases: Comma-separated list of aliases (optional)
 *   - ensure: present or absent (default: present)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "puppet_memory.h"
#include "color_output.h"
#include "puppet_provider.h"

#define HOSTS_FILE "/etc/hosts"
#define MAX_LINE_SIZE 1024
#define MAX_HOSTS_SIZE (64 * 1024)

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Read the entire hosts file
 */
static char *read_hosts_file(void) {
    FILE *fp = fopen(HOSTS_FILE, "r");
    if (!fp) {
        return puppet_strdup("");
    }

    char *buffer = puppet_malloc(MAX_HOSTS_SIZE);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    size_t total = 0;
    size_t n;
    while ((n = fread(buffer + total, 1, MAX_HOSTS_SIZE - total - 1, fp)) > 0) {
        total += n;
        if (total >= MAX_HOSTS_SIZE - 1) break;
    }
    buffer[total] = '\0';

    fclose(fp);
    return buffer;
}

/**
 * @brief Write the hosts file
 */
static int write_hosts_file(const char *content) {
    FILE *fp = fopen(HOSTS_FILE, "w");
    if (!fp) {
        return -1;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, fp);
    fclose(fp);

    return (written == len) ? 0 : -1;
}

/**
 * @brief Find a host entry by hostname
 * Returns pointer to start of line, sets line_end to end of line
 */
static const char *find_host_entry(const char *hosts, const char *hostname,
                                   const char **line_end) {
    const char *pos = hosts;

    while (*pos) {
        /* Skip leading whitespace */
        while (*pos == ' ' || *pos == '\t') pos++;

        /* Skip comments and empty lines */
        if (*pos == '#' || *pos == '\n') {
            while (*pos && *pos != '\n') pos++;
            if (*pos == '\n') pos++;
            continue;
        }

        /* Find end of line */
        const char *eol = strchr(pos, '\n');
        if (!eol) eol = pos + strlen(pos);

        /* Parse line: IP hostname [aliases...] */
        /* Skip IP address */
        const char *p = pos;
        while (p < eol && *p != ' ' && *p != '\t') p++;
        while (p < eol && (*p == ' ' || *p == '\t')) p++;

        /* Check each hostname/alias on this line */
        while (p < eol) {
            const char *word_start = p;
            while (p < eol && *p != ' ' && *p != '\t' && *p != '\n') p++;

            size_t word_len = p - word_start;
            if (word_len == strlen(hostname) &&
                strncmp(word_start, hostname, word_len) == 0) {
                /* Found it */
                *line_end = (*eol == '\n') ? eol + 1 : eol;
                return pos;
            }

            while (p < eol && (*p == ' ' || *p == '\t')) p++;
        }

        pos = (*eol == '\n') ? eol + 1 : eol;
    }

    return NULL;
}

/**
 * @brief Build a hosts entry line
 */
static char *build_host_entry(const char *hostname, const char *ip,
                              const char *aliases) {
    size_t size = strlen(ip) + 1 + strlen(hostname) + 2;
    if (aliases && strlen(aliases) > 0) {
        size += 1 + strlen(aliases);
    }

    char *entry = puppet_malloc(size);
    if (!entry) return NULL;

    if (aliases && strlen(aliases) > 0) {
        snprintf(entry, size, "%s\t%s %s\n", ip, hostname, aliases);
    } else {
        snprintf(entry, size, "%s\t%s\n", ip, hostname);
    }

    return entry;
}

/* ============================================================================
 * Host Provider Implementation
 * ============================================================================ */

static resource_state_t host_check(const resource_t *resource, apply_context_t *ctx) {
    (void)ctx;

    char *hosts = read_hosts_file();
    if (!hosts) {
        return RESOURCE_STATE_UNKNOWN;
    }

    const char *line_end;
    const char *entry = find_host_entry(hosts, resource->title, &line_end);

    puppet_free(hosts);

    return entry ? RESOURCE_STATE_PRESENT : RESOURCE_STATE_ABSENT;
}

static apply_result_t host_apply(const resource_t *resource, apply_context_t *ctx) {
    const char *hostname = resource->title;
    const char *ip = resource_get_param(resource, "ip");
    const char *aliases_raw = resource_get_param(resource, "host_aliases");
    const char *ensure = resource_get_param(resource, "ensure");

    /* Strip brackets from aliases if present (arrays come as "[a, b, c]") */
    char *aliases = NULL;
    if (aliases_raw && aliases_raw[0] == '[') {
        size_t len = strlen(aliases_raw);
        if (len >= 2 && aliases_raw[len-1] == ']') {
            aliases = puppet_malloc(len - 1);
            strncpy(aliases, aliases_raw + 1, len - 2);
            aliases[len - 2] = '\0';
        } else {
            aliases = puppet_strdup(aliases_raw);
        }
    } else if (aliases_raw) {
        aliases = puppet_strdup(aliases_raw);
    }

    if (!ensure) ensure = "present";

    bool want_present = (strcmp(ensure, "present") == 0);

    /* Validate required parameters */
    if (want_present && (!ip || strlen(ip) == 0)) {
        apply_context_set_error(ctx, "Host resource requires 'ip' parameter");
        puppet_free(aliases);
        return APPLY_FAILED;
    }

    /* Read current hosts file */
    char *hosts = read_hosts_file();
    if (!hosts) {
        apply_context_set_error(ctx, "Failed to read %s", HOSTS_FILE);
        puppet_free(aliases);
        return APPLY_FAILED;
    }

    /* Find existing entry */
    const char *line_start = NULL;
    const char *line_end = NULL;
    bool found = (find_host_entry(hosts, hostname, &line_end) != NULL);
    if (found) {
        line_start = find_host_entry(hosts, hostname, &line_end);
    }

    /* Handle ensure => absent */
    if (!want_present) {
        if (!found) {
            puppet_free(hosts);
            puppet_free(aliases);
            return APPLY_SUCCESS;
        }

        if (ctx->noop) {
            print_resource_noop("Host", hostname, "ensure", "would be removed");
            puppet_free(hosts);
            puppet_free(aliases);
            return APPLY_SKIPPED;
        }

        /* Remove the entry */
        size_t before_len = line_start - hosts;
        size_t after_len = strlen(line_end);
        size_t new_size = before_len + after_len + 1;

        char *new_hosts = puppet_malloc(new_size);
        if (!new_hosts) {
            puppet_free(hosts);
            puppet_free(aliases);
            apply_context_set_error(ctx, "Memory allocation failed");
            return APPLY_FAILED;
        }

        memcpy(new_hosts, hosts, before_len);
        memcpy(new_hosts + before_len, line_end, after_len);
        new_hosts[before_len + after_len] = '\0';

        if (write_hosts_file(new_hosts) != 0) {
            puppet_free(hosts);
            puppet_free(new_hosts);
            puppet_free(aliases);
            apply_context_set_error(ctx, "Failed to write %s: %s",
                                   HOSTS_FILE, strerror(errno));
            return APPLY_FAILED;
        }

        puppet_free(hosts);
        puppet_free(new_hosts);
        puppet_free(aliases);

        print_resource_change("Host", hostname, "ensure", "removed");
        return APPLY_CHANGED;
    }

    /* ensure => present */
    char *new_entry = build_host_entry(hostname, ip, aliases);
    if (!new_entry) {
        puppet_free(hosts);
        puppet_free(aliases);
        apply_context_set_error(ctx, "Failed to build host entry");
        return APPLY_FAILED;
    }

    if (found) {
        /* Check if entry matches */
        size_t existing_len = line_end - line_start;
        size_t new_len = strlen(new_entry);

        if (existing_len == new_len &&
            strncmp(line_start, new_entry, existing_len) == 0) {
            /* Already correct */
            puppet_free(new_entry);
            puppet_free(hosts);
            puppet_free(aliases);
            return APPLY_SUCCESS;
        }

        if (ctx->noop) {
            print_resource_noop("Host", hostname, "ip", "would be updated");
            puppet_free(new_entry);
            puppet_free(hosts);
            puppet_free(aliases);
            return APPLY_SKIPPED;
        }

        /* Replace existing entry */
        size_t before_len = line_start - hosts;
        size_t after_len = strlen(line_end);
        size_t new_entry_len = strlen(new_entry);
        size_t new_size = before_len + new_entry_len + after_len + 1;

        char *new_hosts = puppet_malloc(new_size);
        if (!new_hosts) {
            puppet_free(new_entry);
            puppet_free(hosts);
            puppet_free(aliases);
            apply_context_set_error(ctx, "Memory allocation failed");
            return APPLY_FAILED;
        }

        memcpy(new_hosts, hosts, before_len);
        memcpy(new_hosts + before_len, new_entry, new_entry_len);
        memcpy(new_hosts + before_len + new_entry_len, line_end, after_len);
        new_hosts[before_len + new_entry_len + after_len] = '\0';

        if (write_hosts_file(new_hosts) != 0) {
            puppet_free(new_entry);
            puppet_free(hosts);
            puppet_free(new_hosts);
            puppet_free(aliases);
            apply_context_set_error(ctx, "Failed to write %s: %s",
                                   HOSTS_FILE, strerror(errno));
            return APPLY_FAILED;
        }

        puppet_free(new_entry);
        puppet_free(hosts);
        puppet_free(new_hosts);
        puppet_free(aliases);

        print_resource_change("Host", hostname, "ip", "updated to %s", ip);
        return APPLY_CHANGED;

    } else {
        /* Add new entry */
        if (ctx->noop) {
            print_resource_noop("Host", hostname, "ensure", "would be created");
            puppet_free(new_entry);
            puppet_free(hosts);
            puppet_free(aliases);
            return APPLY_SKIPPED;
        }

        size_t hosts_len = strlen(hosts);
        size_t new_entry_len = strlen(new_entry);
        size_t new_size = hosts_len + new_entry_len + 2;

        char *new_hosts = puppet_malloc(new_size);
        if (!new_hosts) {
            puppet_free(new_entry);
            puppet_free(hosts);
            puppet_free(aliases);
            apply_context_set_error(ctx, "Memory allocation failed");
            return APPLY_FAILED;
        }

        /* Ensure hosts ends with newline before adding */
        if (hosts_len > 0 && hosts[hosts_len - 1] != '\n') {
            snprintf(new_hosts, new_size, "%s\n%s", hosts, new_entry);
        } else {
            snprintf(new_hosts, new_size, "%s%s", hosts, new_entry);
        }

        if (write_hosts_file(new_hosts) != 0) {
            puppet_free(new_entry);
            puppet_free(hosts);
            puppet_free(new_hosts);
            puppet_free(aliases);
            apply_context_set_error(ctx, "Failed to write %s: %s",
                                   HOSTS_FILE, strerror(errno));
            return APPLY_FAILED;
        }

        puppet_free(new_entry);
        puppet_free(hosts);
        puppet_free(new_hosts);
        puppet_free(aliases);

        print_resource_change("Host", hostname, "ensure", "created (%s)", ip);
        return APPLY_CHANGED;
    }
}

/* ============================================================================
 * Provider Registration
 * ============================================================================ */

static const provider_t host_provider = {
    .name = "host",
    .resource_type = "host",
    .os_family = 0,  /* Generic - works on all POSIX platforms */
    .apply = host_apply,
    .check = host_check,
    .cleanup = NULL
};

void provider_host_register(void) {
    provider_register(&host_provider);
}
