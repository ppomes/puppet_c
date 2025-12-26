/**
 * @file provider_exec.c
 * @brief Exec resource provider
 *
 * Executes arbitrary commands with conditional execution support.
 */

#include <stdio.h>
#include <stdlib.h>
#include "puppet_memory.h"
#include "exec_utils.h"
#include "file_utils.h"
#include "color_output.h"
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>

#include "puppet_provider.h"

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Run a command and return exit code
 */
static int run_check_command(const char *cmd) {
    return run_command(cmd, NULL, 0);
}

/**
 * @brief Run command as a different user with optional environment
 */
static int run_as_user(const char *cmd, const char *user, const char *cwd,
                       const char *environment, bool verbose) {
    char full_cmd[8192];
    char env_prefix[4096] = "";

    /* Build environment variable prefix if specified */
    if (environment && strlen(environment) > 0) {
        /* Environment can be a single VAR=value or space-separated list */
        /* We prefix the command with export statements */
        snprintf(env_prefix, sizeof(env_prefix), "export %s && ", environment);
    }

    if (user) {
        if (cwd) {
            snprintf(full_cmd, sizeof(full_cmd),
                     "su -s /bin/sh -c 'cd \"%s\" && %s%s' %s 2>&1",
                     cwd, env_prefix, cmd, user);
        } else {
            snprintf(full_cmd, sizeof(full_cmd),
                     "su -s /bin/sh -c '%s%s' %s 2>&1",
                     env_prefix, cmd, user);
        }
    } else {
        if (cwd) {
            snprintf(full_cmd, sizeof(full_cmd),
                     "cd \"%s\" && %s%s 2>&1",
                     cwd, env_prefix, cmd);
        } else {
            snprintf(full_cmd, sizeof(full_cmd), "%s%s 2>&1", env_prefix, cmd);
        }
    }

    return exec_command(full_cmd, verbose);
}

/* ============================================================================
 * Exec Provider Implementation
 * ============================================================================ */

static resource_state_t exec_check(const resource_t *resource, apply_context_t *ctx) {
    (void)resource;
    (void)ctx;
    /* Exec resources don't have a persistent state to check */
    return RESOURCE_STATE_UNKNOWN;
}

static apply_result_t exec_apply(const resource_t *resource, apply_context_t *ctx) {
    /* Get command - either from 'command' parameter or resource title */
    const char *command = resource_get_param(resource, "command");
    if (!command || strlen(command) == 0) {
        command = resource->title;
    }

    /* Get optional parameters */
    const char *creates = resource_get_param(resource, "creates");
    const char *unless_cmd = resource_get_param(resource, "unless");
    const char *onlyif_cmd = resource_get_param(resource, "onlyif");
    const char *cwd = resource_get_param(resource, "cwd");
    const char *user = resource_get_param(resource, "user");
    const char *environment = resource_get_param(resource, "environment");
    const char *returns_str = resource_get_param(resource, "returns");

    /* Default expected return code */
    int expected_return = 0;
    if (returns_str) {
        expected_return = atoi(returns_str);
    }

    /* Check 'creates' condition - skip if file exists */
    if (creates && file_exists(creates)) {
        if (ctx->verbose) {
            print_info("Exec[%s]: Skipping because '%s' exists", resource->title, creates);
        }
        return APPLY_NOOP;
    }

    /* Check 'unless' condition - skip if command succeeds (returns 0) */
    if (unless_cmd) {
        if (run_check_command(unless_cmd) == 0) {
            if (ctx->verbose) {
                print_info("Exec[%s]: Skipping because 'unless' command succeeded",
                          resource->title);
            }
            return APPLY_NOOP;
        }
    }

    /* Check 'onlyif' condition - skip if command fails (returns non-zero) */
    if (onlyif_cmd) {
        if (run_check_command(onlyif_cmd) != 0) {
            if (ctx->verbose) {
                print_info("Exec[%s]: Skipping because 'onlyif' command failed",
                          resource->title);
            }
            return APPLY_NOOP;
        }
    }

    /* Noop mode - just report what would happen */
    if (ctx->noop) {
        print_resource_noop("Exec", resource->title, "returns", "would be executed");
        return APPLY_SKIPPED;
    }

    /* Execute the command */
    if (ctx->verbose) {
        print_info("Exec[%s]: Running '%s'", resource->title, command);
        if (environment) {
            print_info("Exec[%s]: With environment: %s", resource->title, environment);
        }
    }

    int ret = run_as_user(command, user, cwd, environment, ctx->verbose);

    /* Check return code */
    /* Note: system() returns the full status, we need WEXITSTATUS for the actual exit code */
    int exit_code = WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;

    if (exit_code == expected_return) {
        print_resource_change("Exec", resource->title, "returns",
                             "executed successfully (exit code %d)", exit_code);
        return APPLY_CHANGED;
    } else {
        apply_context_set_error(ctx, "Command returned %d, expected %d",
                               exit_code, expected_return);
        return APPLY_FAILED;
    }
}

/* ============================================================================
 * Provider Registration
 * ============================================================================ */

static const provider_t exec_provider = {
    .name = "exec",
    .resource_type = "exec",
    .os_family = 0,  /* Generic - works on all platforms */
    .apply = exec_apply,
    .check = exec_check,
    .cleanup = NULL
};

void provider_exec_register(void) {
    provider_register(&exec_provider);
}
