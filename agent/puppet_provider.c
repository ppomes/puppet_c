/**
 * @file puppet_provider.c
 * @brief Resource provider registry and core functions
 */

#include <stdio.h>
#include <stdlib.h>
#include "puppet_memory.h"
#include "puppet_json_common.h"
#include <string.h>
#include <stdarg.h>

#include "puppet_provider.h"

#define MAX_PROVIDERS 64

/* Provider registry */
static const provider_t *providers[MAX_PROVIDERS];
static size_t provider_count = 0;
static os_family_t current_os_family = OS_FAMILY_UNKNOWN;

/* ============================================================================
 * OS Family Functions
 * ============================================================================ */

os_family_t os_family_from_string(const char *os_family_str) {
    if (!os_family_str) return OS_FAMILY_UNKNOWN;

    if (strcasecmp(os_family_str, "Debian") == 0) return OS_FAMILY_DEBIAN;
    if (strcasecmp(os_family_str, "RedHat") == 0) return OS_FAMILY_REDHAT;
    if (strcasecmp(os_family_str, "Suse") == 0) return OS_FAMILY_SUSE;
    if (strcasecmp(os_family_str, "Archlinux") == 0) return OS_FAMILY_ARCHLINUX;
    if (strcasecmp(os_family_str, "Gentoo") == 0) return OS_FAMILY_GENTOO;
    if (strcasecmp(os_family_str, "Alpine") == 0) return OS_FAMILY_ALPINE;

    return OS_FAMILY_UNKNOWN;
}

const char *os_family_to_string(os_family_t family) {
    switch (family) {
        case OS_FAMILY_DEBIAN: return "Debian";
        case OS_FAMILY_REDHAT: return "RedHat";
        case OS_FAMILY_SUSE: return "Suse";
        case OS_FAMILY_ARCHLINUX: return "Archlinux";
        case OS_FAMILY_GENTOO: return "Gentoo";
        case OS_FAMILY_ALPINE: return "Alpine";
        default: return "Unknown";
    }
}

/* ============================================================================
 * Provider Registry
 * ============================================================================ */

void providers_init(os_family_t os_family) {
    provider_count = 0;
    current_os_family = os_family;
    memset(providers, 0, sizeof(providers));

    /* Register built-in providers */
    provider_file_register();
    provider_package_register();
    provider_service_register();
    provider_notify_register();
}

void providers_shutdown(void) {
    for (size_t i = 0; i < provider_count; i++) {
        if (providers[i] && providers[i]->cleanup) {
            providers[i]->cleanup();
        }
    }
    provider_count = 0;
}

int provider_register(const provider_t *provider) {
    if (!provider || provider_count >= MAX_PROVIDERS) {
        return -1;
    }

    providers[provider_count++] = provider;
    return 0;
}

const provider_t *provider_get(const char *resource_type) {
    if (!resource_type) return NULL;

    const provider_t *generic = NULL;

    /* Find best matching provider */
    for (size_t i = 0; i < provider_count; i++) {
        if (!providers[i]) continue;
        if (strcmp(providers[i]->resource_type, resource_type) != 0) continue;

        /* Exact OS family match */
        if (providers[i]->os_family == current_os_family) {
            return providers[i];
        }

        /* Generic provider (os_family == 0) */
        if (providers[i]->os_family == 0) {
            generic = providers[i];
        }
    }

    return generic;
}

/* ============================================================================
 * Resource Functions
 * ============================================================================ */

resource_t *resource_create(const char *type, const char *title) {
    resource_t *r = puppet_calloc(1, sizeof(resource_t));
    if (!r) return NULL;

    r->type = puppet_strdup(type);
    r->title = puppet_strdup(title);

    if (!r->type || !r->title) {
        puppet_free(r->type);
        puppet_free(r->title);
        puppet_free(r);
        return NULL;
    }

    return r;
}

int resource_add_param(resource_t *resource, const char *name, const char *value) {
    if (!resource || !name) return -1;

    resource_param_t *new_params = puppet_realloc(resource->params,
                                           (resource->param_count + 1) * sizeof(resource_param_t));
    if (!new_params) return -1;

    resource->params = new_params;
    resource->params[resource->param_count].name = puppet_strdup(name);
    resource->params[resource->param_count].value = value ? puppet_strdup(value) : NULL;
    resource->param_count++;

    return 0;
}

const char *resource_get_param(const resource_t *resource, const char *name) {
    if (!resource || !name) return NULL;

    for (size_t i = 0; i < resource->param_count; i++) {
        if (strcmp(resource->params[i].name, name) == 0) {
            return resource->params[i].value;
        }
    }

    return NULL;
}

void resource_free(resource_t *resource) {
    if (!resource) return;

    puppet_free(resource->type);
    puppet_free(resource->title);

    for (size_t i = 0; i < resource->param_count; i++) {
        puppet_free(resource->params[i].name);
        puppet_free(resource->params[i].value);
    }
    puppet_free(resource->params);
    puppet_free(resource);
}

/* ============================================================================
 * Apply Context
 * ============================================================================ */

apply_context_t *apply_context_create(os_family_t os_family, bool noop, bool verbose) {
    apply_context_t *ctx = puppet_calloc(1, sizeof(apply_context_t));
    if (!ctx) return NULL;

    ctx->os_family = os_family;
    ctx->noop = noop;
    ctx->verbose = verbose;

    return ctx;
}

void apply_context_free(apply_context_t *ctx) {
    if (!ctx) return;
    puppet_free(ctx->last_error);
    puppet_free(ctx);
}

void apply_context_set_error(apply_context_t *ctx, const char *fmt, ...) {
    if (!ctx) return;

    puppet_free(ctx->last_error);

    va_list args;
    va_start(args, fmt);

    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    ctx->last_error = puppet_strdup(buf);

    va_end(args);
}

/* ============================================================================
 * Resource Application
 * ============================================================================ */

apply_result_t resource_apply(const resource_t *resource, apply_context_t *ctx) {
    if (!resource || !ctx) return APPLY_FAILED;

    const provider_t *provider = provider_get(resource->type);
    if (!provider) {
        apply_context_set_error(ctx, "No provider for resource type: %s", resource->type);
        return APPLY_FAILED;
    }

    if (ctx->verbose) {
        fprintf(stderr, "[APPLY] %s[%s] using provider '%s'\n",
                resource->type, resource->title, provider->name);
    }

    apply_result_t result = provider->apply(resource, ctx);

    switch (result) {
        case APPLY_CHANGED:
            ctx->changes_made++;
            break;
        case APPLY_FAILED:
            ctx->failures++;
            break;
        default:
            break;
    }

    return result;
}

/* ============================================================================
 * Catalog Parsing and Application
 * ============================================================================ */

/**
 * @brief Parse a resource from a json_value_t object
 */
static resource_t *parse_resource_from_json(json_value_t *res_obj) {
    if (!res_obj || !json_is_object(res_obj)) return NULL;

    /* Get type and title */
    json_value_t *type_val = json_object_get(res_obj, "type");
    json_value_t *title_val = json_object_get(res_obj, "title");

    if (!type_val || !json_is_string(type_val) ||
        !title_val || !json_is_string(title_val)) {
        return NULL;
    }

    const char *type = json_get_string(type_val);
    const char *title = json_get_string(title_val);

    resource_t *resource = resource_create(type, title);
    if (!resource) return NULL;

    /* Parse parameters object */
    json_value_t *params = json_object_get(res_obj, "parameters");
    if (params && json_is_object(params)) {
        for (size_t i = 0; i < params->data.object.count; i++) {
            const char *key = params->data.object.keys[i];
            json_value_t *val = params->data.object.values[i];

            /* Convert value to string representation */
            char *value_str = NULL;
            if (json_is_string(val)) {
                value_str = puppet_strdup(json_get_string(val));
            } else if (json_is_bool(val)) {
                value_str = puppet_strdup(json_get_bool(val) ? "true" : "false");
            } else if (json_is_number(val)) {
                char buf[64];
                double num = json_get_number(val);
                if (num == (long)num) {
                    snprintf(buf, sizeof(buf), "%ld", (long)num);
                } else {
                    snprintf(buf, sizeof(buf), "%g", num);
                }
                value_str = puppet_strdup(buf);
            } else if (json_is_null(val)) {
                value_str = puppet_strdup("");
            }

            if (value_str) {
                resource_add_param(resource, key, value_str);
                puppet_free(value_str);
            }
        }
    }

    return resource;
}

int catalog_apply(const char *catalog_json, apply_context_t *ctx) {
    if (!catalog_json || !ctx) return -1;

    printf("\n=== Applying Catalog ===\n");

    /* Parse the catalog JSON */
    json_value_t *catalog = json_parse(catalog_json);
    if (!catalog) {
        printf("Failed to parse catalog JSON.\n");
        return -1;
    }

    if (!json_is_object(catalog)) {
        printf("Catalog is not a JSON object.\n");
        json_value_destroy(catalog);
        return -1;
    }

    /* Find resources array */
    json_value_t *resources = json_object_get(catalog, "resources");
    if (!resources || !json_is_array(resources)) {
        printf("No resources in catalog.\n");
        json_value_destroy(catalog);
        return 0;
    }

    /* Apply each resource */
    int applied = 0;
    size_t resource_count = json_array_size(resources);

    for (size_t i = 0; i < resource_count; i++) {
        json_value_t *res_obj = json_array_get(resources, i);
        resource_t *resource = parse_resource_from_json(res_obj);

        if (resource) {
            printf("\n%s[%s]:\n", resource->type, resource->title);

            apply_result_t result = resource_apply(resource, ctx);

            switch (result) {
                case APPLY_SUCCESS:
                case APPLY_NOOP:
                    printf("  Status: OK (no changes needed)\n");
                    break;
                case APPLY_CHANGED:
                    printf("  Status: CHANGED\n");
                    break;
                case APPLY_SKIPPED:
                    printf("  Status: SKIPPED (noop mode)\n");
                    break;
                case APPLY_FAILED:
                    printf("  Status: FAILED - %s\n",
                           ctx->last_error ? ctx->last_error : "unknown error");
                    break;
            }

            resource_free(resource);
            applied++;
        }
    }

    json_value_destroy(catalog);

    printf("\n========================\n");
    printf("Applied: %d resources\n", applied);
    printf("Changed: %d\n", ctx->changes_made);
    printf("Failed: %d\n", ctx->failures);
    printf("========================\n\n");

    return ctx->failures;
}
