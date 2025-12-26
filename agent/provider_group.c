/**
 * @file provider_group.c
 * @brief Group resource provider
 *
 * Manages system groups via groupadd/groupmod/groupdel.
 *
 * Supported parameters:
 *   - name (title): The group name
 *   - ensure: present or absent (default: present)
 *   - gid: Group ID (optional)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
 * @brief Check if a group exists
 */
static bool group_exists(const char *name) {
    struct group *gr = getgrnam(name);
    return gr != NULL;
}

/**
 * @brief Get group's GID
 */
static gid_t get_group_gid(const char *name) {
    struct group *gr = getgrnam(name);
    return gr ? gr->gr_gid : (gid_t)-1;
}

/**
 * @brief Run a command and return success/failure
 */
static int run_group_command(const char *cmd, bool verbose) {
    if (verbose) {
        fprintf(stderr, "[GROUP] Running: %s\n", cmd);
    }
    int ret = system(cmd);
    return WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;
}

/* ============================================================================
 * Group Provider Implementation
 * ============================================================================ */

static resource_state_t group_check(const resource_t *resource, apply_context_t *ctx) {
    (void)ctx;
    return group_exists(resource->title) ? RESOURCE_STATE_PRESENT : RESOURCE_STATE_ABSENT;
}

static apply_result_t group_apply(const resource_t *resource, apply_context_t *ctx) {
    const char *groupname = resource->title;
    const char *ensure = resource_get_param(resource, "ensure");
    const char *gid_str = resource_get_param(resource, "gid");

    if (!ensure) ensure = "present";

    bool want_present = (strcmp(ensure, "present") == 0);
    bool exists = group_exists(groupname);

    /* Handle ensure => absent */
    if (!want_present) {
        if (!exists) {
            return APPLY_SUCCESS;
        }

        if (ctx->noop) {
            print_resource_noop("Group", groupname, "ensure", "would be removed");
            return APPLY_SKIPPED;
        }

        char cmd[512];
        snprintf(cmd, sizeof(cmd), "groupdel '%s' 2>&1", groupname);

        if (run_group_command(cmd, ctx->verbose) != 0) {
            apply_context_set_error(ctx, "Failed to delete group %s", groupname);
            return APPLY_FAILED;
        }

        print_resource_change("Group", groupname, "ensure", "removed");
        return APPLY_CHANGED;
    }

    /* ensure => present */
    if (!exists) {
        /* Create group */
        if (ctx->noop) {
            print_resource_noop("Group", groupname, "ensure", "would be created");
            return APPLY_SKIPPED;
        }

        char cmd[512];
        if (gid_str && strlen(gid_str) > 0) {
            snprintf(cmd, sizeof(cmd), "groupadd -g %s '%s' 2>&1", gid_str, groupname);
        } else {
            snprintf(cmd, sizeof(cmd), "groupadd '%s' 2>&1", groupname);
        }

        if (run_group_command(cmd, ctx->verbose) != 0) {
            apply_context_set_error(ctx, "Failed to create group %s", groupname);
            return APPLY_FAILED;
        }

        print_resource_change("Group", groupname, "ensure", "created");
        return APPLY_CHANGED;
    }

    /* Group exists - check if GID needs updating */
    if (gid_str && strlen(gid_str) > 0) {
        gid_t current_gid = get_group_gid(groupname);
        gid_t desired_gid = (gid_t)atoi(gid_str);

        if (current_gid != desired_gid) {
            if (ctx->noop) {
                char msg[128];
                snprintf(msg, sizeof(msg), "would be changed from %d to %d",
                         (int)current_gid, (int)desired_gid);
                print_resource_noop("Group", groupname, "gid", msg);
                return APPLY_SKIPPED;
            }

            char cmd[512];
            snprintf(cmd, sizeof(cmd), "groupmod -g %s '%s' 2>&1", gid_str, groupname);

            if (run_group_command(cmd, ctx->verbose) != 0) {
                apply_context_set_error(ctx, "Failed to modify group %s", groupname);
                return APPLY_FAILED;
            }

            print_resource_change("Group", groupname, "gid",
                                 "changed from %d to %d",
                                 (int)current_gid, (int)desired_gid);
            return APPLY_CHANGED;
        }
    }

    return APPLY_SUCCESS;
}

/* ============================================================================
 * Provider Registration
 * ============================================================================ */

static const provider_t group_provider = {
    .name = "group",
    .resource_type = "group",
    .os_family = 0,  /* Generic - works on all POSIX platforms */
    .apply = group_apply,
    .check = group_check,
    .cleanup = NULL
};

void provider_group_register(void) {
    provider_register(&group_provider);
}
