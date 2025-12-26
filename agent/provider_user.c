/**
 * @file provider_user.c
 * @brief User resource provider
 *
 * Manages system users via useradd/usermod/userdel.
 *
 * Supported parameters:
 *   - name (title): The username
 *   - ensure: present or absent (default: present)
 *   - uid: User ID (optional)
 *   - gid: Primary group ID or name (optional)
 *   - groups: Supplementary groups, comma-separated (optional)
 *   - home: Home directory (optional)
 *   - shell: Login shell (optional)
 *   - comment: GECOS field / full name (optional)
 *   - managehome: Create/remove home directory (default: false)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <sys/wait.h>

#include "puppet_memory.h"
#include "color_output.h"
#include "exec_utils.h"
#include "puppet_provider.h"

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Check if a user exists
 */
static bool user_exists(const char *name) {
    struct passwd *pw = getpwnam(name);
    return pw != NULL;
}

/**
 * @brief Get user info
 */
static struct passwd *get_user_info(const char *name) {
    return getpwnam(name);
}

/**
 * @brief Run a command and return success/failure
 */
static int run_user_command(const char *cmd, bool verbose) {
    if (verbose) {
        fprintf(stderr, "[USER] Running: %s\n", cmd);
    }
    int ret = system(cmd);
    return WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;
}

/**
 * @brief Check if a string parameter differs from current value
 */
static bool str_differs(const char *current, const char *desired) {
    if (!desired || strlen(desired) == 0) return false;
    if (!current) return true;
    return strcmp(current, desired) != 0;
}

/* ============================================================================
 * User Provider Implementation
 * ============================================================================ */

static resource_state_t user_check(const resource_t *resource, apply_context_t *ctx) {
    (void)ctx;
    return user_exists(resource->title) ? RESOURCE_STATE_PRESENT : RESOURCE_STATE_ABSENT;
}

static apply_result_t user_apply(const resource_t *resource, apply_context_t *ctx) {
    const char *username = resource->title;
    const char *ensure = resource_get_param(resource, "ensure");
    const char *uid_str = resource_get_param(resource, "uid");
    const char *gid_str = resource_get_param(resource, "gid");
    const char *groups = resource_get_param(resource, "groups");
    const char *home = resource_get_param(resource, "home");
    const char *shell = resource_get_param(resource, "shell");
    const char *comment = resource_get_param(resource, "comment");
    const char *managehome = resource_get_param(resource, "managehome");

    if (!ensure) ensure = "present";

    bool want_present = (strcmp(ensure, "present") == 0);
    bool exists = user_exists(username);
    bool create_home = managehome && (strcmp(managehome, "true") == 0);

    /* Handle ensure => absent */
    if (!want_present) {
        if (!exists) {
            return APPLY_SUCCESS;
        }

        if (ctx->noop) {
            print_resource_noop("User", username, "ensure", "would be removed");
            return APPLY_SKIPPED;
        }

        char cmd[512];
        if (create_home) {
            snprintf(cmd, sizeof(cmd), "userdel -r '%s' 2>&1", username);
        } else {
            snprintf(cmd, sizeof(cmd), "userdel '%s' 2>&1", username);
        }

        if (run_user_command(cmd, ctx->verbose) != 0) {
            apply_context_set_error(ctx, "Failed to delete user %s", username);
            return APPLY_FAILED;
        }

        print_resource_change("User", username, "ensure", "removed");
        return APPLY_CHANGED;
    }

    /* ensure => present */
    if (!exists) {
        /* Create user */
        if (ctx->noop) {
            print_resource_noop("User", username, "ensure", "would be created");
            return APPLY_SKIPPED;
        }

        /* Build useradd command */
        char cmd[1024];
        char *p = cmd;
        size_t remaining = sizeof(cmd);
        int n;

        n = snprintf(p, remaining, "useradd");
        p += n; remaining -= n;

        if (create_home) {
            n = snprintf(p, remaining, " -m");
            p += n; remaining -= n;
        }

        if (uid_str && strlen(uid_str) > 0) {
            n = snprintf(p, remaining, " -u %s", uid_str);
            p += n; remaining -= n;
        }

        if (gid_str && strlen(gid_str) > 0) {
            n = snprintf(p, remaining, " -g '%s'", gid_str);
            p += n; remaining -= n;
        }

        if (groups && strlen(groups) > 0) {
            n = snprintf(p, remaining, " -G '%s'", groups);
            p += n; remaining -= n;
        }

        if (home && strlen(home) > 0) {
            n = snprintf(p, remaining, " -d '%s'", home);
            p += n; remaining -= n;
        }

        if (shell && strlen(shell) > 0) {
            n = snprintf(p, remaining, " -s '%s'", shell);
            p += n; remaining -= n;
        }

        if (comment && strlen(comment) > 0) {
            n = snprintf(p, remaining, " -c '%s'", comment);
            p += n; remaining -= n;
        }

        snprintf(p, remaining, " '%s' 2>&1", username);

        if (run_user_command(cmd, ctx->verbose) != 0) {
            apply_context_set_error(ctx, "Failed to create user %s", username);
            return APPLY_FAILED;
        }

        print_resource_change("User", username, "ensure", "created");
        return APPLY_CHANGED;
    }

    /* User exists - check if modifications needed */
    struct passwd *pw = get_user_info(username);
    if (!pw) {
        apply_context_set_error(ctx, "Failed to get user info for %s", username);
        return APPLY_FAILED;
    }

    bool needs_modify = false;
    char cmd[1024];
    char *p = cmd;
    size_t remaining = sizeof(cmd);
    int n;

    n = snprintf(p, remaining, "usermod");
    p += n; remaining -= n;

    /* Check UID */
    if (uid_str && strlen(uid_str) > 0) {
        uid_t desired_uid = (uid_t)atoi(uid_str);
        if (pw->pw_uid != desired_uid) {
            n = snprintf(p, remaining, " -u %s", uid_str);
            p += n; remaining -= n;
            needs_modify = true;
        }
    }

    /* Check GID */
    if (gid_str && strlen(gid_str) > 0) {
        gid_t desired_gid;
        /* Check if it's a number or group name */
        char *end;
        long gid_num = strtol(gid_str, &end, 10);
        if (*end == '\0') {
            desired_gid = (gid_t)gid_num;
        } else {
            struct group *gr = getgrnam(gid_str);
            desired_gid = gr ? gr->gr_gid : pw->pw_gid;
        }

        if (pw->pw_gid != desired_gid) {
            n = snprintf(p, remaining, " -g '%s'", gid_str);
            p += n; remaining -= n;
            needs_modify = true;
        }
    }

    /* Check home */
    if (str_differs(pw->pw_dir, home)) {
        n = snprintf(p, remaining, " -d '%s'", home);
        p += n; remaining -= n;
        needs_modify = true;
    }

    /* Check shell */
    if (str_differs(pw->pw_shell, shell)) {
        n = snprintf(p, remaining, " -s '%s'", shell);
        p += n; remaining -= n;
        needs_modify = true;
    }

    /* Check comment/GECOS */
    if (str_differs(pw->pw_gecos, comment)) {
        n = snprintf(p, remaining, " -c '%s'", comment);
        p += n; remaining -= n;
        needs_modify = true;
    }

    /* Check supplementary groups (always set if specified) */
    if (groups && strlen(groups) > 0) {
        n = snprintf(p, remaining, " -G '%s'", groups);
        p += n; remaining -= n;
        needs_modify = true;
    }

    if (!needs_modify) {
        return APPLY_SUCCESS;
    }

    if (ctx->noop) {
        print_resource_noop("User", username, "attributes", "would be modified");
        return APPLY_SKIPPED;
    }

    snprintf(p, remaining, " '%s' 2>&1", username);

    if (run_user_command(cmd, ctx->verbose) != 0) {
        apply_context_set_error(ctx, "Failed to modify user %s", username);
        return APPLY_FAILED;
    }

    print_resource_change("User", username, "attributes", "modified");
    return APPLY_CHANGED;
}

/* ============================================================================
 * Provider Registration
 * ============================================================================ */

static const provider_t user_provider = {
    .name = "user",
    .resource_type = "user",
    .os_family = 0,  /* Generic - works on all POSIX platforms */
    .apply = user_apply,
    .check = user_check,
    .cleanup = NULL
};

void provider_user_register(void) {
    provider_register(&user_provider);
}
