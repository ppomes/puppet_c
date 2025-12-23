/**
 * @file provider_notify.c
 * @brief Notify resource provider
 *
 * Outputs messages during catalog application.
 */

#include <stdio.h>
#include <string.h>

#include "puppet_provider.h"

/* ============================================================================
 * Notify Provider Implementation
 * ============================================================================ */

static apply_result_t notify_apply(const resource_t *resource, apply_context_t *ctx) {
    const char *message = resource_get_param(resource, "message");

    /* Use title as message if no message parameter */
    if (!message || strlen(message) == 0) {
        message = resource->title;
    }

    if (ctx->noop) {
        printf("  Would notify: %s\n", message);
        return APPLY_SKIPPED;
    }

    printf("  Notice: %s\n", message);
    return APPLY_SUCCESS;
}

static resource_state_t notify_check(const resource_t *resource, apply_context_t *ctx) {
    (void)resource;
    (void)ctx;
    return RESOURCE_STATE_UNKNOWN;
}

/* ============================================================================
 * Provider Registration
 * ============================================================================ */

static const provider_t notify_provider = {
    .name = "notify",
    .resource_type = "notify",
    .os_family = OS_FAMILY_UNKNOWN,  /* Generic - works everywhere */
    .apply = notify_apply,
    .check = notify_check,
    .cleanup = NULL
};

void provider_notify_register(void) {
    provider_register(&notify_provider);
}
