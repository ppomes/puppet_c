/**
 * @file provider_ssh_authorized_key.c
 * @brief SSH authorized_key resource provider
 *
 * Manages entries in users' ~/.ssh/authorized_keys files.
 *
 * Supported parameters:
 *   - name (title): The key name/comment identifier
 *   - ensure: present or absent (default: present)
 *   - key: The actual public key (base64 encoded part)
 *   - type: Key type (ssh-rsa, ssh-ed25519, ecdsa-sha2-nistp256, etc.)
 *   - user: The user whose authorized_keys file to manage
 *   - target: Optional path to authorized_keys file (default: ~/.ssh/authorized_keys)
 *   - options: Optional SSH options (comma-separated or array)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "puppet_memory.h"
#include "color_output.h"
#include "puppet_provider.h"

#define MAX_LINE_SIZE 8192
#define MAX_FILE_SIZE (256 * 1024)

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Get user's home directory
 */
static char *get_user_home(const char *username) {
    struct passwd *pw = getpwnam(username);
    if (!pw) {
        return NULL;
    }
    return puppet_strdup(pw->pw_dir);
}

/**
 * @brief Get user's uid
 */
static uid_t get_user_uid(const char *username) {
    struct passwd *pw = getpwnam(username);
    if (!pw) {
        return (uid_t)-1;
    }
    return pw->pw_uid;
}

/**
 * @brief Get user's gid
 */
static gid_t get_user_gid(const char *username) {
    struct passwd *pw = getpwnam(username);
    if (!pw) {
        return (gid_t)-1;
    }
    return pw->pw_gid;
}

/**
 * @brief Build path to authorized_keys file
 */
static char *build_authorized_keys_path(const char *username, const char *target) {
    if (target && strlen(target) > 0) {
        return puppet_strdup(target);
    }

    char *home = get_user_home(username);
    if (!home) {
        return NULL;
    }

    size_t path_len = strlen(home) + strlen("/.ssh/authorized_keys") + 1;
    char *path = puppet_malloc(path_len);
    if (!path) {
        puppet_free(home);
        return NULL;
    }

    snprintf(path, path_len, "%s/.ssh/authorized_keys", home);
    puppet_free(home);
    return path;
}

/**
 * @brief Ensure .ssh directory exists with proper permissions
 */
static int ensure_ssh_dir(const char *username) {
    char *home = get_user_home(username);
    if (!home) {
        return -1;
    }

    size_t path_len = strlen(home) + strlen("/.ssh") + 1;
    char *ssh_dir = puppet_malloc(path_len);
    if (!ssh_dir) {
        puppet_free(home);
        return -1;
    }

    snprintf(ssh_dir, path_len, "%s/.ssh", home);

    struct stat st;
    if (stat(ssh_dir, &st) == 0) {
        /* Directory exists */
        puppet_free(home);
        puppet_free(ssh_dir);
        return 0;
    }

    /* Create directory */
    if (mkdir(ssh_dir, 0700) != 0) {
        puppet_free(home);
        puppet_free(ssh_dir);
        return -1;
    }

    /* Set ownership (ignore errors - best effort) */
    uid_t uid = get_user_uid(username);
    gid_t gid = get_user_gid(username);
    if (uid != (uid_t)-1 && gid != (gid_t)-1) {
        if (chown(ssh_dir, uid, gid) != 0) { /* ignore errors */ }
    }

    puppet_free(home);
    puppet_free(ssh_dir);
    return 0;
}

/**
 * @brief Read the authorized_keys file
 */
static char *read_authorized_keys(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        /* File doesn't exist, return empty string */
        return puppet_strdup("");
    }

    char *buffer = puppet_malloc(MAX_FILE_SIZE);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    size_t total = 0;
    size_t n;
    while ((n = fread(buffer + total, 1, MAX_FILE_SIZE - total - 1, fp)) > 0) {
        total += n;
        if (total >= MAX_FILE_SIZE - 1) break;
    }
    buffer[total] = '\0';

    fclose(fp);
    return buffer;
}

/**
 * @brief Write the authorized_keys file with proper permissions
 */
static int write_authorized_keys(const char *path, const char *content,
                                  const char *username) {
    FILE *fp = fopen(path, "w");
    if (!fp) {
        return -1;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, fp);
    fclose(fp);

    if (written != len) {
        return -1;
    }

    /* Set proper permissions (0600) */
    (void)chmod(path, 0600);

    /* Set ownership (ignore errors - best effort) */
    uid_t uid = get_user_uid(username);
    gid_t gid = get_user_gid(username);
    if (uid != (uid_t)-1 && gid != (gid_t)-1) {
        if (chown(path, uid, gid) != 0) { /* ignore errors */ }
    }

    return 0;
}

/**
 * @brief Build a key entry line
 * Format: [options] type key comment
 */
static char *build_key_entry(const char *name, const char *type,
                              const char *key, const char *options) {
    size_t size = strlen(type) + 1 + strlen(key) + 1 + strlen(name) + 2;
    if (options && strlen(options) > 0) {
        size += strlen(options) + 1;
    }

    char *entry = puppet_malloc(size);
    if (!entry) return NULL;

    if (options && strlen(options) > 0) {
        snprintf(entry, size, "%s %s %s %s\n", options, type, key, name);
    } else {
        snprintf(entry, size, "%s %s %s\n", type, key, name);
    }

    return entry;
}

/**
 * @brief Parse options from Puppet format to SSH format
 * Puppet may pass options as array "[opt1, opt2]" or string "opt1,opt2"
 */
static char *parse_options(const char *options_raw) {
    if (!options_raw || strlen(options_raw) == 0) {
        return NULL;
    }

    char *options = NULL;

    /* Handle array format: [opt1, opt2] */
    if (options_raw[0] == '[') {
        size_t len = strlen(options_raw);
        if (len >= 2 && options_raw[len-1] == ']') {
            options = puppet_malloc(len - 1);
            strncpy(options, options_raw + 1, len - 2);
            options[len - 2] = '\0';
        } else {
            options = puppet_strdup(options_raw);
        }
    } else {
        options = puppet_strdup(options_raw);
    }

    return options;
}

/**
 * @brief Find a key entry by key content (base64 part)
 * Returns pointer to start of line, sets line_end to end of line
 */
static const char *find_key_entry(const char *content, const char *key,
                                   const char **line_end) {
    const char *pos = content;

    while (*pos) {
        /* Skip leading whitespace */
        while (*pos == ' ' || *pos == '\t') pos++;

        /* Skip comments and empty lines */
        if (*pos == '#' || *pos == '\n') {
            while (*pos && *pos != '\n') pos++;
            if (*pos == '\n') pos++;
            continue;
        }

        const char *line_start = pos;

        /* Find end of line */
        const char *eol = strchr(pos, '\n');
        if (!eol) eol = pos + strlen(pos);

        /* Search for the key in this line */
        const char *key_pos = strstr(line_start, key);
        if (key_pos && key_pos < eol) {
            /* Verify it's a complete key match (not a substring) */
            /* Key should be preceded by space and followed by space or EOL */
            if (key_pos > line_start &&
                (*(key_pos - 1) == ' ' || *(key_pos - 1) == '\t')) {
                const char *key_end = key_pos + strlen(key);
                if (key_end >= eol || *key_end == ' ' || *key_end == '\t' || *key_end == '\n') {
                    *line_end = (*eol == '\n') ? eol + 1 : eol;
                    return line_start;
                }
            }
        }

        pos = (*eol == '\n') ? eol + 1 : eol;
    }

    return NULL;
}

/**
 * @brief Find a key entry by name/comment
 */
static const char *find_key_by_name(const char *content, const char *name,
                                     const char **line_end) {
    const char *pos = content;

    while (*pos) {
        /* Skip leading whitespace */
        while (*pos == ' ' || *pos == '\t') pos++;

        /* Skip comments and empty lines */
        if (*pos == '#' || *pos == '\n') {
            while (*pos && *pos != '\n') pos++;
            if (*pos == '\n') pos++;
            continue;
        }

        const char *line_start = pos;

        /* Find end of line */
        const char *eol = strchr(pos, '\n');
        if (!eol) eol = pos + strlen(pos);

        /* Check if line ends with the name (comment field) */
        size_t name_len = strlen(name);
        size_t line_len = eol - line_start;

        if (line_len >= name_len) {
            const char *potential_name = eol - name_len;
            /* Check for match with preceding space */
            if (potential_name > line_start &&
                (*(potential_name - 1) == ' ' || *(potential_name - 1) == '\t') &&
                strncmp(potential_name, name, name_len) == 0) {
                *line_end = (*eol == '\n') ? eol + 1 : eol;
                return line_start;
            }
        }

        pos = (*eol == '\n') ? eol + 1 : eol;
    }

    return NULL;
}

/* ============================================================================
 * SSH Authorized Key Provider Implementation
 * ============================================================================ */

static resource_state_t ssh_authorized_key_check(const resource_t *resource,
                                                   apply_context_t *ctx) {
    (void)ctx;

    const char *key = resource_get_param(resource, "key");
    const char *user = resource_get_param(resource, "user");
    const char *target = resource_get_param(resource, "target");

    if (!key || !user) {
        return RESOURCE_STATE_UNKNOWN;
    }

    char *path = build_authorized_keys_path(user, target);
    if (!path) {
        return RESOURCE_STATE_UNKNOWN;
    }

    char *content = read_authorized_keys(path);
    puppet_free(path);

    if (!content) {
        return RESOURCE_STATE_UNKNOWN;
    }

    const char *line_end;
    const char *entry = find_key_entry(content, key, &line_end);

    puppet_free(content);

    return entry ? RESOURCE_STATE_PRESENT : RESOURCE_STATE_ABSENT;
}

static apply_result_t ssh_authorized_key_apply(const resource_t *resource,
                                                apply_context_t *ctx) {
    const char *name = resource->title;
    const char *key = resource_get_param(resource, "key");
    const char *type = resource_get_param(resource, "type");
    const char *user = resource_get_param(resource, "user");
    const char *target = resource_get_param(resource, "target");
    const char *options_raw = resource_get_param(resource, "options");
    const char *ensure = resource_get_param(resource, "ensure");

    if (!ensure) ensure = "present";

    bool want_present = (strcmp(ensure, "present") == 0);

    /* Validate required parameters */
    if (!user || strlen(user) == 0) {
        apply_context_set_error(ctx, "ssh_authorized_key requires 'user' parameter");
        return APPLY_FAILED;
    }

    if (want_present) {
        if (!key || strlen(key) == 0) {
            apply_context_set_error(ctx, "ssh_authorized_key requires 'key' parameter");
            return APPLY_FAILED;
        }
        if (!type || strlen(type) == 0) {
            apply_context_set_error(ctx, "ssh_authorized_key requires 'type' parameter");
            return APPLY_FAILED;
        }
    }

    /* Verify user exists */
    if (!get_user_home(user)) {
        apply_context_set_error(ctx, "User '%s' does not exist", user);
        return APPLY_FAILED;
    }

    /* Ensure .ssh directory exists */
    if (want_present && !target) {
        if (ensure_ssh_dir(user) != 0 && !ctx->noop) {
            apply_context_set_error(ctx, "Failed to create .ssh directory for user '%s'", user);
            return APPLY_FAILED;
        }
    }

    /* Build path to authorized_keys */
    char *path = build_authorized_keys_path(user, target);
    if (!path) {
        apply_context_set_error(ctx, "Failed to determine authorized_keys path");
        return APPLY_FAILED;
    }

    /* Read current file */
    char *content = read_authorized_keys(path);
    if (!content) {
        puppet_free(path);
        apply_context_set_error(ctx, "Failed to read authorized_keys file");
        return APPLY_FAILED;
    }

    /* Parse options */
    char *options = parse_options(options_raw);

    /* Find existing entry by key */
    const char *line_start = NULL;
    const char *line_end = NULL;
    bool found = false;

    if (key) {
        line_start = find_key_entry(content, key, &line_end);
        found = (line_start != NULL);
    }

    /* If not found by key, try by name */
    if (!found && !want_present) {
        line_start = find_key_by_name(content, name, &line_end);
        found = (line_start != NULL);
    }

    /* Handle ensure => absent */
    if (!want_present) {
        if (!found) {
            puppet_free(content);
            puppet_free(path);
            puppet_free(options);
            return APPLY_SUCCESS;
        }

        if (ctx->noop) {
            print_resource_noop("Ssh_authorized_key", name, "ensure", "would be removed");
            puppet_free(content);
            puppet_free(path);
            puppet_free(options);
            return APPLY_SKIPPED;
        }

        /* Remove the entry */
        size_t before_len = line_start - content;
        size_t after_len = strlen(line_end);
        size_t new_size = before_len + after_len + 1;

        char *new_content = puppet_malloc(new_size);
        if (!new_content) {
            puppet_free(content);
            puppet_free(path);
            puppet_free(options);
            apply_context_set_error(ctx, "Memory allocation failed");
            return APPLY_FAILED;
        }

        memcpy(new_content, content, before_len);
        memcpy(new_content + before_len, line_end, after_len);
        new_content[before_len + after_len] = '\0';

        if (write_authorized_keys(path, new_content, user) != 0) {
            apply_context_set_error(ctx, "Failed to write %s: %s", path, strerror(errno));
            puppet_free(content);
            puppet_free(new_content);
            puppet_free(path);
            puppet_free(options);
            return APPLY_FAILED;
        }

        puppet_free(content);
        puppet_free(new_content);
        puppet_free(path);
        puppet_free(options);

        print_resource_change("Ssh_authorized_key", name, "ensure", "removed");
        return APPLY_CHANGED;
    }

    /* ensure => present */
    char *new_entry = build_key_entry(name, type, key, options);
    if (!new_entry) {
        puppet_free(content);
        puppet_free(path);
        puppet_free(options);
        apply_context_set_error(ctx, "Failed to build key entry");
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
            puppet_free(content);
            puppet_free(path);
            puppet_free(options);
            return APPLY_SUCCESS;
        }

        if (ctx->noop) {
            print_resource_noop("Ssh_authorized_key", name, "key", "would be updated");
            puppet_free(new_entry);
            puppet_free(content);
            puppet_free(path);
            puppet_free(options);
            return APPLY_SKIPPED;
        }

        /* Replace existing entry */
        size_t before_len = line_start - content;
        size_t after_len = strlen(line_end);
        size_t new_entry_len = strlen(new_entry);
        size_t new_size = before_len + new_entry_len + after_len + 1;

        char *new_content = puppet_malloc(new_size);
        if (!new_content) {
            puppet_free(new_entry);
            puppet_free(content);
            puppet_free(path);
            puppet_free(options);
            apply_context_set_error(ctx, "Memory allocation failed");
            return APPLY_FAILED;
        }

        memcpy(new_content, content, before_len);
        memcpy(new_content + before_len, new_entry, new_entry_len);
        memcpy(new_content + before_len + new_entry_len, line_end, after_len);
        new_content[before_len + new_entry_len + after_len] = '\0';

        if (write_authorized_keys(path, new_content, user) != 0) {
            apply_context_set_error(ctx, "Failed to write %s: %s", path, strerror(errno));
            puppet_free(new_entry);
            puppet_free(content);
            puppet_free(new_content);
            puppet_free(path);
            puppet_free(options);
            return APPLY_FAILED;
        }

        puppet_free(new_entry);
        puppet_free(content);
        puppet_free(new_content);
        puppet_free(path);
        puppet_free(options);

        print_resource_change("Ssh_authorized_key", name, "key", "updated");
        return APPLY_CHANGED;

    } else {
        /* Add new entry */
        if (ctx->noop) {
            print_resource_noop("Ssh_authorized_key", name, "ensure", "would be created");
            puppet_free(new_entry);
            puppet_free(content);
            puppet_free(path);
            puppet_free(options);
            return APPLY_SKIPPED;
        }

        size_t content_len = strlen(content);
        size_t new_entry_len = strlen(new_entry);
        size_t new_size = content_len + new_entry_len + 2;

        char *new_content = puppet_malloc(new_size);
        if (!new_content) {
            puppet_free(new_entry);
            puppet_free(content);
            puppet_free(path);
            puppet_free(options);
            apply_context_set_error(ctx, "Memory allocation failed");
            return APPLY_FAILED;
        }

        /* Ensure content ends with newline before adding */
        if (content_len > 0 && content[content_len - 1] != '\n') {
            snprintf(new_content, new_size, "%s\n%s", content, new_entry);
        } else {
            snprintf(new_content, new_size, "%s%s", content, new_entry);
        }

        if (write_authorized_keys(path, new_content, user) != 0) {
            apply_context_set_error(ctx, "Failed to write %s: %s", path, strerror(errno));
            puppet_free(new_entry);
            puppet_free(content);
            puppet_free(new_content);
            puppet_free(path);
            puppet_free(options);
            return APPLY_FAILED;
        }

        puppet_free(new_entry);
        puppet_free(content);
        puppet_free(new_content);
        puppet_free(path);
        puppet_free(options);

        print_resource_change("Ssh_authorized_key", name, "ensure", "created");
        return APPLY_CHANGED;
    }
}

/* ============================================================================
 * Provider Registration
 * ============================================================================ */

static const provider_t ssh_authorized_key_provider = {
    .name = "ssh_authorized_key",
    .resource_type = "ssh_authorized_key",
    .os_family = 0,  /* Generic - works on all POSIX platforms */
    .apply = ssh_authorized_key_apply,
    .check = ssh_authorized_key_check,
    .cleanup = NULL
};

void provider_ssh_authorized_key_register(void) {
    provider_register(&ssh_authorized_key_provider);
}
