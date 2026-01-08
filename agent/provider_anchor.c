/**
 * @file provider_anchor.c
 * @brief Anchor resource provider
 *
 * Anchor is a no-op resource type from puppetlabs-stdlib used purely for
 * ordering dependencies in catalogs. It does nothing when applied.
 */

#include <stdio.h>
#include <string.h>
#include "color_output.h"

#include "puppet_provider.h"

/* ============================================================================
 * Anchor Provider Implementation
 * ============================================================================ */

static apply_result_t anchor_apply(const resource_t *resource, apply_context_t *ctx) {
    (void)resource;
    (void)ctx;
    /* Anchor is a no-op - it exists purely for ordering */
    return APPLY_SUCCESS;
}

static resource_state_t anchor_check(const resource_t *resource, apply_context_t *ctx) {
    (void)resource;
    (void)ctx;
    return RESOURCE_STATE_UNKNOWN;
}

/* ============================================================================
 * Provider Registration
 * ============================================================================ */

static const provider_t anchor_provider = {
    .name = "anchor",
    .resource_type = "anchor",
    .os_family = OS_FAMILY_UNKNOWN,  /* Generic - works everywhere */
    .apply = anchor_apply,
    .check = anchor_check,
    .cleanup = NULL
};

void provider_anchor_register(void) {
    provider_register(&anchor_provider);
}
