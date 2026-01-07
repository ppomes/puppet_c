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

/* Convert array command format "[cmd, arg1, arg2]" to "cmd arg1 arg2" */
static char *convert_array_command(const char *cmd) {
    if (!cmd || cmd[0] != '[') return NULL;

    size_t len = strlen(cmd);
    if (len < 2 || cmd[len-1] != ']') return NULL;

    /* Allocate buffer for result (same size is safe) */
    char *result = puppet_malloc(len + 1);
    char *out = result;

    /* Skip opening bracket and whitespace */
    const char *p = cmd + 1;
    while (*p == ' ') p++;

    bool first = true;
    while (*p && *p != ']') {
        /* Skip whitespace and commas */
        while (*p == ' ' || *p == ',') p++;
        if (*p == ']' || *p == '\0') break;

        if (!first) *out++ = ' ';
        first = false;

        /* Copy element until comma, space or end bracket */
        while (*p && *p != ',' && *p != ']') {
            *out++ = *p++;
        }
        /* Trim trailing space from element */
        while (out > result && *(out-1) == ' ') out--;
    }
    *out = '\0';

    return result;
}

static apply_result_t exec_apply(const resource_t *resource, apply_context_t *ctx) {
    /* Get command - either from 'command' parameter or resource title */
    const char *command = resource_get_param(resource, "command");
    if (!command || strlen(command) == 0) {
        command = resource->title;
    }

    /* Check refreshonly - if true, skip normal apply (only run on refresh) */
    const char *refreshonly = resource_get_param(resource, "refreshonly");
    if (refreshonly && (strcmp(refreshonly, "true") == 0 || strcmp(refreshonly, "1") == 0)) {
        if (ctx->verbose) {
            print_info("Exec[%s]: Skipping (refreshonly=true, waiting for refresh)",
                      resource->title);
        }
        return APPLY_NOOP;
    }

    /* Handle array format: [cmd, arg1, arg2] -> "cmd arg1 arg2" */
    char *converted_cmd = NULL;
    if (command && command[0] == '[') {
        converted_cmd = convert_array_command(command);
        if (converted_cmd) {
            command = converted_cmd;
        }
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
        if (converted_cmd) puppet_free(converted_cmd);
        return APPLY_NOOP;
    }

    /* Check 'unless' condition - skip if command succeeds (returns 0) */
    if (unless_cmd) {
        if (run_check_command(unless_cmd) == 0) {
            if (ctx->verbose) {
                print_info("Exec[%s]: Skipping because 'unless' command succeeded",
                          resource->title);
            }
            if (converted_cmd) puppet_free(converted_cmd);
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
            if (converted_cmd) puppet_free(converted_cmd);
            return APPLY_NOOP;
        }
    }

    /* Noop mode - just report what would happen */
    if (ctx->noop) {
        print_resource_noop("Exec", resource->title, "returns", "would be executed");
        if (converted_cmd) puppet_free(converted_cmd);
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

    apply_result_t result;
    if (exit_code == expected_return) {
        print_resource_change("Exec", resource->title, "returns",
                             "executed successfully (exit code %d)", exit_code);
        result = APPLY_CHANGED;
    } else {
        apply_context_set_error(ctx, "Command returned %d, expected %d",
                               exit_code, expected_return);
        result = APPLY_FAILED;
    }

    if (converted_cmd) puppet_free(converted_cmd);
    return result;
}

/**
 * @brief Refresh an exec resource (re-run the command)
 *
 * This is called when a resource that this exec subscribes to changes,
 * or when a resource notifies this exec.
 */
static apply_result_t exec_refresh(const resource_t *resource, apply_context_t *ctx) {
    /* For refresh, we just re-run the command (skipping 'creates' check for refreshonly execs) */
    const char *command = resource_get_param(resource, "command");
    if (!command || strlen(command) == 0) {
        command = resource->title;
    }

    /* Handle array format */
    char *converted_cmd = NULL;
    if (command && command[0] == '[') {
        converted_cmd = convert_array_command(command);
        if (converted_cmd) {
            command = converted_cmd;
        }
    }

    const char *cwd = resource_get_param(resource, "cwd");
    const char *user = resource_get_param(resource, "user");
    const char *environment = resource_get_param(resource, "environment");
    const char *returns_str = resource_get_param(resource, "returns");

    int expected_return = returns_str ? atoi(returns_str) : 0;

    if (ctx->noop) {
        print_resource_noop("Exec", resource->title, "refresh", "would be executed");
        if (converted_cmd) puppet_free(converted_cmd);
        return APPLY_SKIPPED;
    }

    if (ctx->verbose) {
        print_info("Exec[%s]: Refreshing (running '%s')", resource->title, command);
    }

    int ret = run_as_user(command, user, cwd, environment, ctx->verbose);
    int exit_code = WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;

    apply_result_t result;
    if (exit_code == expected_return) {
        print_resource_change("Exec", resource->title, "refresh",
                             "executed on refresh (exit code %d)", exit_code);
        ctx->changes_made++;
        result = APPLY_CHANGED;
    } else {
        apply_context_set_error(ctx, "Refresh command returned %d, expected %d",
                               exit_code, expected_return);
        result = APPLY_FAILED;
    }

    if (converted_cmd) puppet_free(converted_cmd);
    return result;
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
    .refresh = exec_refresh,
    .cleanup = NULL
};

void provider_exec_register(void) {
    provider_register(&exec_provider);
}
