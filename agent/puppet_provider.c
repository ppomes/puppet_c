/**
 * @file puppet_provider.c
 * @brief Resource provider registry and core functions
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "puppet_memory.h"
#include "puppet_json_common.h"
#include "color_output.h"
#include <string.h>
#include <stdarg.h>

#include "puppet_provider.h"
#include "puppet_agent_ruby.h"

#define MAX_PROVIDERS 64

/* Forward declaration for Deferred evaluation */
static char *evaluate_deferred(json_value_t *deferred_obj);

/* Provider registry */
static const provider_t *providers[MAX_PROVIDERS];
static size_t provider_count = 0;
static os_family_t current_os_family = OS_FAMILY_UNKNOWN;

/* Global Ruby context for provider fallback (set by agent) */
static agent_ruby_context_t *ruby_provider_ctx = NULL;

/**
 * @brief Set the Ruby context for provider fallback
 */
void provider_set_ruby_context(agent_ruby_context_t *ctx) {
    ruby_provider_ctx = ctx;
}

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
 * Deferred Evaluation
 * ============================================================================ */

/**
 * @brief Build JSON string from arguments array
 */
static char *build_args_json(json_value_t *args) {
    if (!args || !json_is_array(args)) return NULL;
    if (args->data.array.count == 0) return puppet_strdup("[]");

    size_t buf_size = 1024;
    char *buf = puppet_malloc(buf_size);
    if (!buf) return NULL;

    size_t pos = 0;
    buf[pos++] = '[';

    for (size_t i = 0; i < args->data.array.count; i++) {
        json_value_t *arg = args->data.array.elements[i];
        if (i > 0) {
            buf[pos++] = ',';
        }

        const char *arg_str = NULL;
        char num_buf[64];
        bool needs_quotes = false;

        if (json_is_string(arg)) {
            arg_str = json_get_string(arg);
            needs_quotes = true;
        } else if (json_is_number(arg)) {
            double num = json_get_number(arg);
            if (num == (long)num) {
                snprintf(num_buf, sizeof(num_buf), "%ld", (long)num);
            } else {
                snprintf(num_buf, sizeof(num_buf), "%g", num);
            }
            arg_str = num_buf;
        } else if (json_is_bool(arg)) {
            arg_str = json_get_bool(arg) ? "true" : "false";
        } else if (json_is_null(arg)) {
            arg_str = "null";
        }

        if (arg_str) {
            size_t arg_len = strlen(arg_str);
            /* Ensure buffer is large enough */
            while (pos + arg_len + 10 >= buf_size) {
                buf_size *= 2;
                char *new_buf = puppet_realloc(buf, buf_size);
                if (!new_buf) {
                    puppet_free(buf);
                    return NULL;
                }
                buf = new_buf;
            }

            if (needs_quotes) {
                buf[pos++] = '"';
                /* Escape special characters */
                for (size_t j = 0; j < arg_len; j++) {
                    if (arg_str[j] == '"' || arg_str[j] == '\\') {
                        buf[pos++] = '\\';
                    }
                    buf[pos++] = arg_str[j];
                }
                buf[pos++] = '"';
            } else {
                memcpy(buf + pos, arg_str, arg_len);
                pos += arg_len;
            }
        }
    }

    buf[pos++] = ']';
    buf[pos] = '\0';
    return buf;
}

/**
 * @brief Evaluate a Deferred function call
 *
 * Handles Deferred values in catalog parameters by evaluating the
 * function at apply time using Ruby.
 *
 * @param deferred_obj JSON object with __ptype: "Deferred"
 * @return Evaluated result as string, or NULL on error
 */
static char *evaluate_deferred(json_value_t *deferred_obj) {
    if (!deferred_obj || !json_is_object(deferred_obj)) return NULL;

    /* Check for __ptype: "Deferred" */
    json_value_t *ptype = json_object_get(deferred_obj, "__ptype");
    if (!ptype || !json_is_string(ptype)) return NULL;
    if (strcmp(json_get_string(ptype), "Deferred") != 0) return NULL;

    /* Get function name */
    json_value_t *name_val = json_object_get(deferred_obj, "name");
    if (!name_val || !json_is_string(name_val)) return NULL;
    const char *func_name = json_get_string(name_val);

    /* If Ruby context is available, evaluate the function */
    if (ruby_provider_ctx) {
        /* Get arguments array */
        json_value_t *args = json_object_get(deferred_obj, "arguments");
        char *args_json = build_args_json(args);

        /* Call Ruby function */
        char *result = agent_ruby_call_function(ruby_provider_ctx, func_name, args_json);
        puppet_free(args_json);

        if (result) {
            return result;
        }
    }

    /* Fallback: return placeholder if Ruby not available */
    char buf[256];
    snprintf(buf, sizeof(buf), "<Deferred:%s>", func_name);
    return puppet_strdup(buf);
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
    provider_exec_register();
    provider_cron_register();
    provider_host_register();
    provider_group_register();
    provider_user_register();
    provider_sysctl_register();
    provider_mount_register();
    provider_anchor_register();
    provider_ssh_authorized_key_register();
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

    /* Try C provider first */
    if (provider) {
        if (ctx->verbose) {
            fprintf(stderr, "[APPLY] %s[%s] using C provider '%s'\n",
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

    /* Fallback to Ruby provider if available */
    if (ruby_provider_ctx && agent_ruby_has_type(ruby_provider_ctx, resource->type)) {
        if (ctx->verbose) {
            fprintf(stderr, "[APPLY] %s[%s] using Ruby provider\n",
                    resource->type, resource->title);
        }

        /* Determine action based on ensure parameter */
        const char *ensure = resource_get_param(resource, "ensure");
        const char *action = "create";
        if (ensure && (strcmp(ensure, "absent") == 0 || strcmp(ensure, "purged") == 0)) {
            action = "destroy";
        }

        ruby_apply_result_t ruby_result = agent_ruby_call_provider(
            ruby_provider_ctx,
            resource->type,
            NULL,  /* Use default provider */
            action,
            resource,
            ctx
        );

        apply_result_t result;
        switch (ruby_result) {
            case RUBY_APPLY_SUCCESS:
                result = APPLY_SUCCESS;
                break;
            case RUBY_APPLY_CHANGED:
                result = APPLY_CHANGED;
                ctx->changes_made++;
                break;
            case RUBY_APPLY_SKIPPED:
            case RUBY_APPLY_NOOP:
                result = APPLY_NOOP;
                break;
            case RUBY_APPLY_FAILED:
            default:
                result = APPLY_FAILED;
                ctx->failures++;
                break;
        }

        return result;
    }

    /* No provider available */
    apply_context_set_error(ctx, "No provider for resource type: %s", resource->type);
    return APPLY_FAILED;
}

apply_result_t resource_refresh(const resource_t *resource, apply_context_t *ctx) {
    if (!resource || !ctx) return APPLY_FAILED;

    const provider_t *provider = provider_get(resource->type);

    /* Try C provider first */
    if (provider) {
        /* If provider has no refresh function, it doesn't support refresh */
        if (!provider->refresh) {
            if (ctx->verbose) {
                print_info("%s[%s]: No refresh action (C provider doesn't support refresh)",
                          resource->type, resource->title);
            }
            return APPLY_NOOP;
        }
        return provider->refresh(resource, ctx);
    }

    /* Fallback to Ruby provider */
    if (ruby_provider_ctx && agent_ruby_has_type(ruby_provider_ctx, resource->type)) {
        ruby_apply_result_t ruby_result = agent_ruby_call_provider(
            ruby_provider_ctx,
            resource->type,
            NULL,
            "refresh",
            resource,
            ctx
        );

        switch (ruby_result) {
            case RUBY_APPLY_SUCCESS:
            case RUBY_APPLY_CHANGED:
                return APPLY_CHANGED;
            case RUBY_APPLY_SKIPPED:
            case RUBY_APPLY_NOOP:
                return APPLY_NOOP;
            default:
                return APPLY_FAILED;
        }
    }

    apply_context_set_error(ctx, "No provider for type: %s", resource->type);
    return APPLY_FAILED;
}

/* ============================================================================
 * Catalog Parsing and Application
 * ============================================================================ */

#define MAX_DEPS 64
#define MAX_NOTIFY 64
#define MAX_RESOURCES 1024

/**
 * @brief Resource entry with dependencies for ordering and refresh tracking
 */
typedef struct {
    resource_t *resource;
    json_value_t *json_obj;     /* Keep reference to parse dependencies */
    size_t deps[MAX_DEPS];      /* Indices of resources this depends on */
    size_t dep_count;
    size_t notify_targets[MAX_NOTIFY];  /* Resources to notify on change */
    size_t notify_count;
    size_t in_degree;           /* For topological sort */
    bool applied;
    bool changed;               /* Did this resource change during apply? */
    bool needs_refresh;         /* Should this resource be refreshed? */
} resource_entry_t;

/**
 * @brief Find resource index by type and title
 */
static int find_resource_index(resource_entry_t *entries, size_t count,
                               const char *type, const char *title) {
    for (size_t i = 0; i < count; i++) {
        if (entries[i].resource &&
            strcasecmp(entries[i].resource->type, type) == 0 &&
            strcmp(entries[i].resource->title, title) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Add a dependency edge (from depends on to)
 */
static void add_dependency(resource_entry_t *entries, size_t from, size_t to) {
    if (from == to) return;  /* No self-dependencies */

    resource_entry_t *entry = &entries[from];
    /* Check if already exists */
    for (size_t i = 0; i < entry->dep_count; i++) {
        if (entry->deps[i] == to) return;
    }
    if (entry->dep_count < MAX_DEPS) {
        entry->deps[entry->dep_count++] = to;
    }
}

/**
 * @brief Add a notify relationship (from notifies to)
 */
static void add_notify(resource_entry_t *entries, size_t from, size_t to) {
    if (from == to) return;  /* No self-notification */

    resource_entry_t *entry = &entries[from];
    /* Check if already exists */
    for (size_t i = 0; i < entry->notify_count; i++) {
        if (entry->notify_targets[i] == to) return;
    }
    if (entry->notify_count < MAX_NOTIFY) {
        entry->notify_targets[entry->notify_count++] = to;
    }
}

/**
 * @brief Parse resource reference string like "File[/etc/mysql]"
 * @return 0 on success, -1 on failure
 */
static int parse_resource_ref(const char *ref, char *type_out, size_t type_size,
                              char *title_out, size_t title_size) {
    if (!ref) return -1;

    const char *bracket = strchr(ref, '[');
    if (!bracket) return -1;

    size_t type_len = bracket - ref;
    if (type_len >= type_size) return -1;

    strncpy(type_out, ref, type_len);
    type_out[type_len] = '\0';

    /* Find closing bracket */
    const char *end = strrchr(ref, ']');
    if (!end || end <= bracket + 1) return -1;

    size_t title_len = end - bracket - 1;
    if (title_len >= title_size) return -1;

    strncpy(title_out, bracket + 1, title_len);
    title_out[title_len] = '\0';

    return 0;
}

/**
 * @brief Get parent directory path
 */
static char *get_parent_path(const char *path) {
    if (!path || path[0] != '/') return NULL;

    char *parent = puppet_strdup(path);
    char *last_slash = strrchr(parent, '/');

    if (last_slash == parent) {
        /* Path is like "/foo", parent is "/" */
        parent[1] = '\0';
    } else if (last_slash) {
        *last_slash = '\0';
    } else {
        puppet_free(parent);
        return NULL;
    }

    return parent;
}

/**
 * @brief Add auto-require dependencies for file resources
 * File resources automatically require their parent directories if they exist in catalog
 */
static void add_file_autorequires(resource_entry_t *entries, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (!entries[i].resource) continue;
        if (strcasecmp(entries[i].resource->type, "file") != 0) continue;

        /* Get the path - either from 'path' parameter or title */
        const char *path = resource_get_param(entries[i].resource, "path");
        if (!path || strlen(path) == 0) {
            path = entries[i].resource->title;
        }

        /* Skip if path doesn't look like an absolute path */
        if (!path || path[0] != '/') continue;

        /* Find parent directory in catalog */
        char *parent = get_parent_path(path);
        if (parent && strlen(parent) > 1) {  /* Skip "/" root */
            int parent_idx = find_resource_index(entries, count, "file", parent);
            if (parent_idx >= 0 && (size_t)parent_idx != i) {
                add_dependency(entries, i, (size_t)parent_idx);
            }
        }
        puppet_free(parent);
    }
}

/**
 * @brief Extract dependencies from resource parameters
 */
static void extract_dependencies(resource_entry_t *entries, size_t count, size_t idx) {
    json_value_t *res_obj = entries[idx].json_obj;
    if (!res_obj) return;

    json_value_t *params = json_object_get(res_obj, "parameters");
    if (!params || !json_is_object(params)) return;

    char type_buf[64], title_buf[256];

    /* Process 'require' - this resource requires the listed resources */
    json_value_t *require = json_object_get(params, "require");
    if (require) {
        if (json_is_string(require)) {
            if (parse_resource_ref(json_get_string(require), type_buf, sizeof(type_buf),
                                   title_buf, sizeof(title_buf)) == 0) {
                int dep_idx = find_resource_index(entries, count, type_buf, title_buf);
                if (dep_idx >= 0) {
                    add_dependency(entries, idx, (size_t)dep_idx);
                }
            }
        } else if (json_is_array(require)) {
            for (size_t j = 0; j < require->data.array.count; j++) {
                json_value_t *elem = require->data.array.elements[j];
                if (json_is_string(elem)) {
                    if (parse_resource_ref(json_get_string(elem), type_buf, sizeof(type_buf),
                                           title_buf, sizeof(title_buf)) == 0) {
                        int dep_idx = find_resource_index(entries, count, type_buf, title_buf);
                        if (dep_idx >= 0) {
                            add_dependency(entries, idx, (size_t)dep_idx);
                        }
                    }
                }
            }
        }
    }

    /* Process 'before' - listed resources require this one */
    json_value_t *before = json_object_get(params, "before");
    if (before) {
        if (json_is_string(before)) {
            if (parse_resource_ref(json_get_string(before), type_buf, sizeof(type_buf),
                                   title_buf, sizeof(title_buf)) == 0) {
                int other_idx = find_resource_index(entries, count, type_buf, title_buf);
                if (other_idx >= 0) {
                    add_dependency(entries, (size_t)other_idx, idx);
                }
            }
        } else if (json_is_array(before)) {
            for (size_t j = 0; j < before->data.array.count; j++) {
                json_value_t *elem = before->data.array.elements[j];
                if (json_is_string(elem)) {
                    if (parse_resource_ref(json_get_string(elem), type_buf, sizeof(type_buf),
                                           title_buf, sizeof(title_buf)) == 0) {
                        int other_idx = find_resource_index(entries, count, type_buf, title_buf);
                        if (other_idx >= 0) {
                            add_dependency(entries, (size_t)other_idx, idx);
                        }
                    }
                }
            }
        }
    }

    /* Process 'notify' - this resource notifies the target(s) on change
     * Also adds ordering dependency: target depends on this */
    json_value_t *notify = json_object_get(params, "notify");
    if (notify) {
        if (json_is_string(notify)) {
            if (parse_resource_ref(json_get_string(notify), type_buf, sizeof(type_buf),
                                   title_buf, sizeof(title_buf)) == 0) {
                int other_idx = find_resource_index(entries, count, type_buf, title_buf);
                if (other_idx >= 0) {
                    add_dependency(entries, (size_t)other_idx, idx);
                    add_notify(entries, idx, (size_t)other_idx);  /* This notifies target */
                }
            }
        } else if (json_is_array(notify)) {
            for (size_t j = 0; j < notify->data.array.count; j++) {
                json_value_t *elem = notify->data.array.elements[j];
                if (json_is_string(elem)) {
                    if (parse_resource_ref(json_get_string(elem), type_buf, sizeof(type_buf),
                                           title_buf, sizeof(title_buf)) == 0) {
                        int other_idx = find_resource_index(entries, count, type_buf, title_buf);
                        if (other_idx >= 0) {
                            add_dependency(entries, (size_t)other_idx, idx);
                            add_notify(entries, idx, (size_t)other_idx);
                        }
                    }
                }
            }
        }
    }

    /* Process 'subscribe' - this resource subscribes to (depends on) listed resources
     * The source resources notify this one on change */
    json_value_t *subscribe = json_object_get(params, "subscribe");
    if (subscribe) {
        if (json_is_string(subscribe)) {
            if (parse_resource_ref(json_get_string(subscribe), type_buf, sizeof(type_buf),
                                   title_buf, sizeof(title_buf)) == 0) {
                int dep_idx = find_resource_index(entries, count, type_buf, title_buf);
                if (dep_idx >= 0) {
                    add_dependency(entries, idx, (size_t)dep_idx);
                    add_notify(entries, (size_t)dep_idx, idx);  /* Source notifies this */
                }
            }
        } else if (json_is_array(subscribe)) {
            for (size_t j = 0; j < subscribe->data.array.count; j++) {
                json_value_t *elem = subscribe->data.array.elements[j];
                if (json_is_string(elem)) {
                    if (parse_resource_ref(json_get_string(elem), type_buf, sizeof(type_buf),
                                           title_buf, sizeof(title_buf)) == 0) {
                        int dep_idx = find_resource_index(entries, count, type_buf, title_buf);
                        if (dep_idx >= 0) {
                            add_dependency(entries, idx, (size_t)dep_idx);
                            add_notify(entries, (size_t)dep_idx, idx);
                        }
                    }
                }
            }
        }
    }
}

/**
 * @brief Extract edges from catalog JSON and add as dependencies
 *
 * Edges are explicit ordering relationships from resource chains:
 * Package['ntp'] -> File['/etc/ntp.conf'] ~> Service['ntpd']
 *
 * Edge format in JSON:
 * {
 *   "source": {"type": "Package", "title": "ntp"},
 *   "target": {"type": "File", "title": "/etc/ntp.conf"},
 *   "relationship": "before"  // or "notifies"
 * }
 */
static void extract_edges(resource_entry_t *entries, size_t count, json_value_t *catalog) {
    json_value_t *edges = json_object_get(catalog, "edges");
    if (!edges || !json_is_array(edges)) return;

    size_t edge_count = json_array_size(edges);
    for (size_t i = 0; i < edge_count; i++) {
        json_value_t *edge = json_array_get(edges, i);
        if (!edge || !json_is_object(edge)) continue;

        /* Get source and target */
        json_value_t *source = json_object_get(edge, "source");
        json_value_t *target = json_object_get(edge, "target");
        json_value_t *rel = json_object_get(edge, "relationship");

        if (!source || !json_is_object(source) ||
            !target || !json_is_object(target)) continue;

        /* Extract source type/title */
        json_value_t *src_type = json_object_get(source, "type");
        json_value_t *src_title = json_object_get(source, "title");
        if (!src_type || !json_is_string(src_type) ||
            !src_title || !json_is_string(src_title)) continue;

        /* Extract target type/title */
        json_value_t *tgt_type = json_object_get(target, "type");
        json_value_t *tgt_title = json_object_get(target, "title");
        if (!tgt_type || !json_is_string(tgt_type) ||
            !tgt_title || !json_is_string(tgt_title)) continue;

        /* Find source and target indices */
        int src_idx = find_resource_index(entries, count,
            json_get_string(src_type), json_get_string(src_title));
        int tgt_idx = find_resource_index(entries, count,
            json_get_string(tgt_type), json_get_string(tgt_title));

        if (src_idx < 0 || tgt_idx < 0) continue;

        /* Add dependency: target depends on source */
        add_dependency(entries, (size_t)tgt_idx, (size_t)src_idx);

        /* For notify relationship, also add notify trigger */
        if (rel && json_is_string(rel)) {
            const char *rel_str = json_get_string(rel);
            if (strcmp(rel_str, "notifies") == 0 || strcmp(rel_str, "notify") == 0) {
                add_notify(entries, (size_t)src_idx, (size_t)tgt_idx);
            }
        }
    }
}

/**
 * @brief Topological sort using Kahn's algorithm
 * @return Sorted indices array (caller must free), or NULL on cycle
 */
static size_t *topological_sort(resource_entry_t *entries, size_t count) {
    size_t *result = puppet_malloc(count * sizeof(size_t));
    size_t result_count = 0;

    /* Compute in-degrees */
    for (size_t i = 0; i < count; i++) {
        entries[i].in_degree = 0;
    }
    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < entries[i].dep_count; j++) {
            /* i depends on entries[i].deps[j], so i has in-degree from deps */
            /* Actually, we track: for each dependency d of i, i depends on d
             * So we need reverse: d contributes to i's ability to run */
        }
    }

    /* Build in-degree: for each resource, count how many others depend on it */
    /* Actually, in_degree = how many resources must complete before this one */
    for (size_t i = 0; i < count; i++) {
        entries[i].in_degree = entries[i].dep_count;
    }

    /* Find all resources with no dependencies */
    size_t *queue = puppet_malloc(count * sizeof(size_t));
    size_t queue_head = 0, queue_tail = 0;

    for (size_t i = 0; i < count; i++) {
        if (entries[i].in_degree == 0) {
            queue[queue_tail++] = i;
        }
    }

    /* Process queue */
    while (queue_head < queue_tail) {
        size_t idx = queue[queue_head++];
        result[result_count++] = idx;

        /* Decrease in-degree of resources that depend on this one */
        for (size_t i = 0; i < count; i++) {
            for (size_t j = 0; j < entries[i].dep_count; j++) {
                if (entries[i].deps[j] == idx) {
                    entries[i].in_degree--;
                    if (entries[i].in_degree == 0) {
                        queue[queue_tail++] = i;
                    }
                    break;
                }
            }
        }
    }

    puppet_free(queue);

    /* Check for cycles */
    if (result_count != count) {
        puppet_free(result);
        return NULL;
    }

    return result;
}

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
            } else if (json_is_array(val)) {
                /* Convert array to bracketed string: ["a", "b"] -> "[a, b]" */
                size_t buf_size = 256;
                char *buf = puppet_malloc(buf_size);
                size_t pos = 0;
                buf[pos++] = '[';

                for (size_t j = 0; j < val->data.array.count; j++) {
                    if (j > 0) {
                        buf[pos++] = ',';
                        buf[pos++] = ' ';
                    }
                    json_value_t *elem = val->data.array.elements[j];
                    const char *elem_str = NULL;
                    char num_buf[64];

                    if (json_is_string(elem)) {
                        elem_str = json_get_string(elem);
                    } else if (json_is_number(elem)) {
                        double num = json_get_number(elem);
                        if (num == (long)num) {
                            snprintf(num_buf, sizeof(num_buf), "%ld", (long)num);
                        } else {
                            snprintf(num_buf, sizeof(num_buf), "%g", num);
                        }
                        elem_str = num_buf;
                    } else if (json_is_bool(elem)) {
                        elem_str = json_get_bool(elem) ? "true" : "false";
                    } else if (json_is_array(elem)) {
                        /* Handle nested array - convert to "[a, b, c]" format */
                        static char nested_buf[1024];
                        size_t npos = 0;
                        nested_buf[npos++] = '[';
                        for (size_t k = 0; k < elem->data.array.count; k++) {
                            if (k > 0) {
                                nested_buf[npos++] = ',';
                                nested_buf[npos++] = ' ';
                            }
                            json_value_t *nelem = elem->data.array.elements[k];
                            if (json_is_string(nelem)) {
                                const char *ns = json_get_string(nelem);
                                size_t ns_len = strlen(ns);
                                if (npos + ns_len + 10 < sizeof(nested_buf)) {
                                    strcpy(nested_buf + npos, ns);
                                    npos += ns_len;
                                }
                            }
                        }
                        nested_buf[npos++] = ']';
                        nested_buf[npos] = '\0';
                        elem_str = nested_buf;
                    }

                    if (elem_str) {
                        size_t elem_len = strlen(elem_str);
                        while (pos + elem_len + 10 >= buf_size) {
                            buf_size *= 2;
                            buf = puppet_realloc(buf, buf_size);
                        }
                        strcpy(buf + pos, elem_str);
                        pos += elem_len;
                    }
                }
                buf[pos++] = ']';
                buf[pos] = '\0';
                value_str = buf;
            } else if (json_is_object(val)) {
                /* Check for Deferred object */
                value_str = evaluate_deferred(val);
                if (!value_str) {
                    /* Not a Deferred, just use empty string for unknown objects */
                    value_str = puppet_strdup("{}");
                }
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

    /* Start timing */
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    print_info("Applying catalog");

    /* Parse the catalog JSON */
    json_value_t *catalog = json_parse(catalog_json);
    if (!catalog) {
        print_error("Failed to parse catalog JSON");
        return -1;
    }

    if (!json_is_object(catalog)) {
        print_error("Catalog is not a JSON object");
        json_value_destroy(catalog);
        return -1;
    }

    /* Find resources array */
    json_value_t *resources = json_object_get(catalog, "resources");
    if (!resources || !json_is_array(resources)) {
        print_info("No resources in catalog");
        json_value_destroy(catalog);
        return 0;
    }

    size_t resource_count = json_array_size(resources);
    if (resource_count > MAX_RESOURCES) {
        print_error("Too many resources in catalog (%zu > %d)", resource_count, MAX_RESOURCES);
        json_value_destroy(catalog);
        return -1;
    }

    /* Phase 1: Parse all resources */
    resource_entry_t *entries = puppet_calloc(resource_count, sizeof(resource_entry_t));
    if (!entries) {
        json_value_destroy(catalog);
        return -1;
    }

    for (size_t i = 0; i < resource_count; i++) {
        json_value_t *res_obj = json_array_get(resources, i);
        entries[i].json_obj = res_obj;
        entries[i].resource = parse_resource_from_json(res_obj);
        entries[i].dep_count = 0;
    }

    /* Phase 2: Extract explicit dependencies from metaparameters */
    for (size_t i = 0; i < resource_count; i++) {
        extract_dependencies(entries, resource_count, i);
    }

    /* Phase 2b: Extract edges from catalog (resource chains) */
    extract_edges(entries, resource_count, catalog);

    /* Phase 3: Add auto-require dependencies (file -> parent directory) */
    add_file_autorequires(entries, resource_count);

    /* Phase 4: Topological sort */
    size_t *sorted_order = topological_sort(entries, resource_count);
    if (!sorted_order) {
        print_error("Dependency cycle detected in catalog");
        /* Fall back to original order */
        sorted_order = puppet_malloc(resource_count * sizeof(size_t));
        for (size_t i = 0; i < resource_count; i++) {
            sorted_order[i] = i;
        }
    }

    if (ctx->verbose) {
        print_info("Resource application order (after dependency sorting):");
        for (size_t i = 0; i < resource_count; i++) {
            size_t idx = sorted_order[i];
            if (entries[idx].resource) {
                fprintf(stderr, "  %zu. %s[%s]", i + 1,
                        entries[idx].resource->type, entries[idx].resource->title);
                if (entries[idx].dep_count > 0) {
                    fprintf(stderr, " (depends on: ");
                    for (size_t j = 0; j < entries[idx].dep_count; j++) {
                        size_t dep = entries[idx].deps[j];
                        if (j > 0) fprintf(stderr, ", ");
                        fprintf(stderr, "%s[%s]",
                                entries[dep].resource->type, entries[dep].resource->title);
                    }
                    fprintf(stderr, ")");
                }
                fprintf(stderr, "\n");
            }
        }
    }

    /* Phase 5: Apply resources in sorted order */
    int applied = 0;
    int refreshed = 0;
    for (size_t i = 0; i < resource_count; i++) {
        size_t idx = sorted_order[i];
        resource_t *resource = entries[idx].resource;

        if (resource) {
            apply_result_t result = resource_apply(resource, ctx);

            /* Track if this resource changed (or would change in noop mode) */
            if (result == APPLY_CHANGED || result == APPLY_SKIPPED) {
                entries[idx].changed = true;
                /* Mark all notify targets for refresh */
                for (size_t j = 0; j < entries[idx].notify_count; j++) {
                    size_t target = entries[idx].notify_targets[j];
                    entries[target].needs_refresh = true;
                    if (ctx->verbose) {
                        print_info("%s[%s]: %s %s[%s]",
                                  resource->type, resource->title,
                                  ctx->noop ? "Would refresh" : "Will refresh",
                                  entries[target].resource->type,
                                  entries[target].resource->title);
                    }
                }
            }

            /* Check if this resource needs refresh (from earlier notify) */
            if (entries[idx].needs_refresh && result != APPLY_FAILED) {
                apply_result_t refresh_result = resource_refresh(resource, ctx);
                if (refresh_result == APPLY_CHANGED || refresh_result == APPLY_SKIPPED) {
                    entries[idx].changed = true;
                    refreshed++;
                    /* Propagate refresh to notify targets */
                    for (size_t j = 0; j < entries[idx].notify_count; j++) {
                        size_t target = entries[idx].notify_targets[j];
                        /* Only mark for refresh if not yet applied */
                        if (!entries[target].applied) {
                            entries[target].needs_refresh = true;
                        }
                    }
                } else if (refresh_result == APPLY_FAILED) {
                    print_resource_error(resource->type, resource->title,
                                        "refresh failed: %s",
                                        ctx->last_error ? ctx->last_error : "unknown error");
                }
            }

            entries[idx].applied = true;

            /* Only print status for non-changes in verbose mode */
            switch (result) {
                case APPLY_SUCCESS:
                case APPLY_NOOP:
                    if (ctx->verbose) {
                        print_info("%s[%s]: OK (no changes needed)", resource->type, resource->title);
                    }
                    break;
                case APPLY_CHANGED:
                    /* Changes are printed by the provider itself */
                    break;
                case APPLY_SKIPPED:
                    /* Noop messages are printed by the provider itself */
                    break;
                case APPLY_FAILED:
                    print_resource_error(resource->type, resource->title,
                                        "%s", ctx->last_error ? ctx->last_error : "unknown error");
                    break;
            }

            applied++;
        }
    }

    /* Cleanup */
    puppet_free(sorted_order);
    for (size_t i = 0; i < resource_count; i++) {
        resource_free(entries[i].resource);
    }
    puppet_free(entries);
    json_value_destroy(catalog);

    /* Summary */
    printf("\n");
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (end_time.tv_sec - start_time.tv_sec) +
                     (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
    print_notice("Applied catalog in %.2f seconds", elapsed);
    if (refreshed > 0) {
        print_info("Resources: %d total, %d changed, %d refreshed, %d failed",
                   applied, ctx->changes_made, refreshed, ctx->failures);
    } else {
        print_info("Resources: %d total, %d changed, %d failed",
                   applied, ctx->changes_made, ctx->failures);
    }

    return ctx->failures;
}
