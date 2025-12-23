/**
 * @file puppet_provider.c
 * @brief Resource provider registry and core functions
 */

#include <stdio.h>
#include <stdlib.h>
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
    resource_t *r = calloc(1, sizeof(resource_t));
    if (!r) return NULL;

    r->type = strdup(type);
    r->title = strdup(title);

    if (!r->type || !r->title) {
        free(r->type);
        free(r->title);
        free(r);
        return NULL;
    }

    return r;
}

int resource_add_param(resource_t *resource, const char *name, const char *value) {
    if (!resource || !name) return -1;

    resource_param_t *new_params = realloc(resource->params,
                                           (resource->param_count + 1) * sizeof(resource_param_t));
    if (!new_params) return -1;

    resource->params = new_params;
    resource->params[resource->param_count].name = strdup(name);
    resource->params[resource->param_count].value = value ? strdup(value) : NULL;
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

    free(resource->type);
    free(resource->title);

    for (size_t i = 0; i < resource->param_count; i++) {
        free(resource->params[i].name);
        free(resource->params[i].value);
    }
    free(resource->params);
    free(resource);
}

/* ============================================================================
 * Apply Context
 * ============================================================================ */

apply_context_t *apply_context_create(os_family_t os_family, bool noop, bool verbose) {
    apply_context_t *ctx = calloc(1, sizeof(apply_context_t));
    if (!ctx) return NULL;

    ctx->os_family = os_family;
    ctx->noop = noop;
    ctx->verbose = verbose;

    return ctx;
}

void apply_context_free(apply_context_t *ctx) {
    if (!ctx) return;
    free(ctx->last_error);
    free(ctx);
}

void apply_context_set_error(apply_context_t *ctx, const char *fmt, ...) {
    if (!ctx) return;

    free(ctx->last_error);

    va_list args;
    va_start(args, fmt);

    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    ctx->last_error = strdup(buf);

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

/* Simple JSON string extraction helper */
static char *json_extract_string(const char *json, const char *key) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *start = strstr(json, search);
    if (!start) return NULL;

    start = strchr(start, ':');
    if (!start) return NULL;
    start++;

    /* Skip whitespace */
    while (*start == ' ' || *start == '\t' || *start == '\n') start++;

    if (*start != '"') return NULL;
    start++;  /* Skip opening quote */

    const char *end = strchr(start, '"');
    if (!end) return NULL;

    size_t len = end - start;
    char *result = malloc(len + 1);
    if (!result) return NULL;

    memcpy(result, start, len);
    result[len] = '\0';

    return result;
}

/* Parse a resource from JSON object (simplified) */
static resource_t *parse_resource_json(const char *json_start, const char **json_end) {
    /* Find type and title */
    char *type = json_extract_string(json_start, "type");
    char *title = json_extract_string(json_start, "title");

    if (!type || !title) {
        free(type);
        free(title);
        return NULL;
    }

    resource_t *resource = resource_create(type, title);
    free(type);
    free(title);

    if (!resource) return NULL;

    /* Find parameters object */
    const char *params_start = strstr(json_start, "\"parameters\":");
    if (params_start) {
        params_start = strchr(params_start, '{');
        if (params_start) {
            params_start++;

            /* Parse key-value pairs until closing brace */
            const char *p = params_start;
            while (*p && *p != '}') {
                /* Skip whitespace */
                while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == ',')) p++;

                if (*p == '}') break;
                if (*p != '"') { p++; continue; }

                /* Extract key */
                p++;  /* Skip opening quote */
                const char *key_start = p;
                while (*p && *p != '"') p++;
                if (!*p) break;

                size_t key_len = p - key_start;
                char *key = malloc(key_len + 1);
                if (!key) break;
                memcpy(key, key_start, key_len);
                key[key_len] = '\0';

                p++;  /* Skip closing quote */

                /* Find colon */
                while (*p && *p != ':') p++;
                if (!*p) { free(key); break; }
                p++;

                /* Skip whitespace */
                while (*p && (*p == ' ' || *p == '\t')) p++;

                /* Extract value */
                char *value = NULL;
                if (*p == '"') {
                    p++;
                    const char *val_start = p;
                    while (*p && *p != '"') {
                        if (*p == '\\' && *(p+1)) p++;  /* Skip escaped chars */
                        p++;
                    }
                    size_t val_len = p - val_start;
                    value = malloc(val_len + 1);
                    if (value) {
                        memcpy(value, val_start, val_len);
                        value[val_len] = '\0';
                    }
                    if (*p == '"') p++;
                } else if (*p == 't' || *p == 'f') {
                    /* Boolean */
                    if (strncmp(p, "true", 4) == 0) {
                        value = strdup("true");
                        p += 4;
                    } else if (strncmp(p, "false", 5) == 0) {
                        value = strdup("false");
                        p += 5;
                    }
                } else if (*p == '-' || (*p >= '0' && *p <= '9')) {
                    /* Number */
                    const char *num_start = p;
                    while (*p && (*p == '-' || *p == '.' || (*p >= '0' && *p <= '9'))) p++;
                    size_t num_len = p - num_start;
                    value = malloc(num_len + 1);
                    if (value) {
                        memcpy(value, num_start, num_len);
                        value[num_len] = '\0';
                    }
                }

                if (value) {
                    resource_add_param(resource, key, value);
                    free(value);
                }
                free(key);
            }
        }
    }

    /* Find end of this resource object */
    int brace_count = 1;
    const char *p = strchr(json_start, '{');
    if (p) {
        p++;
        while (*p && brace_count > 0) {
            if (*p == '{') brace_count++;
            else if (*p == '}') brace_count--;
            p++;
        }
        *json_end = p;
    } else {
        *json_end = json_start + strlen(json_start);
    }

    return resource;
}

int catalog_apply(const char *catalog_json, apply_context_t *ctx) {
    if (!catalog_json || !ctx) return -1;

    printf("\n=== Applying Catalog ===\n");

    /* Find resources array */
    const char *resources_start = strstr(catalog_json, "\"resources\":");
    if (!resources_start) {
        printf("No resources in catalog.\n");
        return 0;
    }

    resources_start = strchr(resources_start, '[');
    if (!resources_start) return 0;
    resources_start++;

    /* Parse and apply each resource */
    const char *p = resources_start;
    int applied = 0;

    while (*p) {
        /* Skip whitespace */
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == ',')) p++;

        if (*p == ']') break;
        if (*p != '{') { p++; continue; }

        const char *end = p;
        resource_t *resource = parse_resource_json(p, &end);

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

        p = end;
    }

    printf("\n========================\n");
    printf("Applied: %d resources\n", applied);
    printf("Changed: %d\n", ctx->changes_made);
    printf("Failed: %d\n", ctx->failures);
    printf("========================\n\n");

    return ctx->failures;
}
