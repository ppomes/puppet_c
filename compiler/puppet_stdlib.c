/*
 * Puppet Standard Library Functions
 * 
 * This module implements the core Puppet functions that are essential
 * for catalog compilation and resource management.
 */

#include "puppet_stdlib.h"
#include "puppet_memory.h"
#include "puppet_loader.h"
#include "puppet_interpreter.h"
#include "puppet_erb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <ctype.h>
#include <math.h>
#include <regex.h>
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

// Note: puppet_log_level_t is now defined in puppet_stdlib.h

/* Thread-local current environment for context-aware logging */
static __thread puppet_env_t *current_log_env = NULL;

void puppet_set_log_env(puppet_env_t *env) {
    current_log_env = env;
}

puppet_env_t *puppet_get_log_env(void) {
    return current_log_env;
}

// Get log level string
static const char *puppet_log_level_str(puppet_log_level_t level) {
    switch (level) {
        case PUPPET_LOG_DEBUG:    return "Debug";
        case PUPPET_LOG_INFO:     return "Info";
        case PUPPET_LOG_NOTICE:   return "Notice";
        case PUPPET_LOG_WARNING:  return "Warning";
        case PUPPET_LOG_ERROR:    return "Error";
        case PUPPET_LOG_CRITICAL: return "Critical";
        default:                  return "Unknown";
    }
}

// Format log message with timestamp and level (internal, no location)
static void puppet_log(puppet_log_level_t level, const char *message) {
    time_t now;
    time(&now);
    struct tm *tm_info = localtime(&now);
    char time_buffer[26];
    strftime(time_buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(stderr, "[%s] %s: %s\n", time_buffer, puppet_log_level_str(level), message);

    /* Increment counters via thread-local env */
    if (current_log_env) {
        if (level == PUPPET_LOG_ERROR || level == PUPPET_LOG_CRITICAL) {
            puppet_env_increment_error(current_log_env);
        } else if (level == PUPPET_LOG_WARNING) {
            puppet_env_increment_warning(current_log_env);
        }
    }
}

// Log with source location (public API)
void puppet_log_loc(puppet_log_level_t level, puppet_location_t loc, const char *format, ...) {
    /* Skip INFO and DEBUG messages unless verbose mode is enabled */
    if ((level == PUPPET_LOG_INFO || level == PUPPET_LOG_DEBUG) && !puppet_verbose) {
        return;
    }

    va_list args;
    va_start(args, format);

    char message[2048];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    time_t now;
    time(&now);
    struct tm *tm_info = localtime(&now);
    char time_buffer[26];
    strftime(time_buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);

    if (loc.filename && loc.line > 0) {
        fprintf(stderr, "[%s] %s: %s at %s:%d\n",
                time_buffer, puppet_log_level_str(level), message, loc.filename, loc.line);
    } else if (loc.line > 0) {
        fprintf(stderr, "[%s] %s: %s at line %d\n",
                time_buffer, puppet_log_level_str(level), message, loc.line);
    } else {
        fprintf(stderr, "[%s] %s: %s\n",
                time_buffer, puppet_log_level_str(level), message);
    }

    /* Increment counters via thread-local env */
    if (current_log_env) {
        if (level == PUPPET_LOG_ERROR || level == PUPPET_LOG_CRITICAL) {
            puppet_env_increment_error(current_log_env);
        } else if (level == PUPPET_LOG_WARNING) {
            puppet_env_increment_warning(current_log_env);
        }
    }
}

// Convenience: log error with location
void puppet_error_at(puppet_location_t loc, const char *format, ...) {
    va_list args;
    va_start(args, format);

    char message[2048];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (loc.filename && loc.line > 0) {
        fprintf(stderr, "[ERROR] %s:%d: %s\n", loc.filename, loc.line, message);
    } else if (loc.line > 0) {
        fprintf(stderr, "[ERROR] line %d: %s\n", loc.line, message);
    } else {
        fprintf(stderr, "[ERROR] %s\n", message);
    }

    /* Increment error counter via thread-local env */
    if (current_log_env) {
        puppet_env_increment_error(current_log_env);
    }
}

// Convenience: log warning with location
void puppet_warning_at(puppet_location_t loc, const char *format, ...) {
    va_list args;
    va_start(args, format);

    char message[2048];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (loc.filename && loc.line > 0) {
        fprintf(stderr, "[WARNING] %s:%d: %s\n", loc.filename, loc.line, message);
    } else if (loc.line > 0) {
        fprintf(stderr, "[WARNING] line %d: %s\n", loc.line, message);
    } else {
        fprintf(stderr, "[WARNING] %s\n", message);
    }

    /* Increment warning counter via thread-local env */
    if (current_log_env) {
        puppet_env_increment_warning(current_log_env);
    }
}

// Check if a value is truthy (false and undef are falsy, everything else is truthy)
static bool puppet_value_is_truthy(puppet_value_t *value) {
    if (!value) return false;
    if (value->type == PUPPET_VALUE_UNDEF) return false;
    if (value->type == PUPPET_VALUE_BOOL) return value->data.boolean;
    return true;
}

// Forward declarations for iterator helpers
static void exec_lambda_body(puppet_lambda_t *lambda, puppet_env_t *env);
static puppet_value_t *exec_lambda_with_result(puppet_lambda_t *lambda, puppet_env_t *env);

// Helper to get source location from function arguments
static puppet_location_t get_args_location(puppet_expr_list_t *args) {
    puppet_location_t loc = {0};
    if (args && args->count > 0 && args->exprs[0]) {
        loc = args->exprs[0]->loc;
    }
    return loc;
}

// Convert Puppet value to display string for logging
char *puppet_value_to_display_string(puppet_value_t *value) {
    if (!value) return puppet_strdup("(null)");
    
    char buffer[1024];
    switch (value->type) {
        case PUPPET_VALUE_UNDEF:
            return puppet_strdup("undef");
            
        case PUPPET_VALUE_BOOL:
            return puppet_strdup(value->data.boolean ? "true" : "false");
            
        case PUPPET_VALUE_STRING:
            return puppet_strdup(value->data.string.data);
            
        case PUPPET_VALUE_NUMBER:
            if (value->data.number == (long)value->data.number) {
                snprintf(buffer, sizeof(buffer), "%ld", (long)value->data.number);
            } else {
                snprintf(buffer, sizeof(buffer), "%.2f", value->data.number);
            }
            return puppet_strdup(buffer);
            
        case PUPPET_VALUE_ARRAY: {
            // Build array representation
            size_t offset = 0;
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, "[");
            for (size_t i = 0; i < value->data.array->count && offset < sizeof(buffer) - 10; i++) {
                if (i > 0) {
                    offset += snprintf(buffer + offset, sizeof(buffer) - offset, ", ");
                }
                char *elem_str = puppet_value_to_display_string(value->data.array->items[i]);
                offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%s", elem_str);
                puppet_free(elem_str);
            }
            if (value->data.array->count > 0 && offset >= sizeof(buffer) - 10) {
                offset += snprintf(buffer + offset, sizeof(buffer) - offset, ", ...");
            }
            snprintf(buffer + offset, sizeof(buffer) - offset, "]");
            return puppet_strdup(buffer);
        }
            
        case PUPPET_VALUE_HASH:
            snprintf(buffer, sizeof(buffer), "{hash with %zu entries}", 
                     value->data.hash ? value->data.hash->bucket_count : 0);
            return puppet_strdup(buffer);
            
        default:
            return puppet_strdup("(unknown)");
    }
}

// Helper to concatenate all arguments into a single string
static char *concat_args_to_message(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count == 0) {
        return puppet_strdup("");
    }
    
    // Calculate total length needed
    size_t total_len = 0;
    char **parts = puppet_calloc(args->count, sizeof(char*));
    
    for (size_t i = 0; i < args->count; i++) {
        puppet_value_t *val = puppet_eval_expr(args->exprs[i], env);
        parts[i] = puppet_value_to_display_string(val);
        total_len += strlen(parts[i]);
        if (i > 0) total_len++; // Space between parts
        puppet_value_destroy(val);
    }
    
    // Build final message
    char *message = puppet_malloc(total_len + 1);
    size_t offset = 0;
    for (size_t i = 0; i < args->count; i++) {
        if (i > 0) {
            message[offset++] = ' ';
        }
        strcpy(message + offset, parts[i]);
        offset += strlen(parts[i]);
        puppet_free(parts[i]);
    }
    message[offset] = '\0';
    
    puppet_free(parts);
    return message;
}

/**
 * fail() - Stop catalog compilation with an error message
 * 
 * Usage: fail("Error message")
 *        fail("Error: ", $variable, " is invalid")
 */
puppet_value_t *puppet_func_fail(puppet_expr_list_t *args, puppet_env_t *env) {
    char *message = concat_args_to_message(args, env);

    // Log as critical error
    puppet_log_loc(PUPPET_LOG_CRITICAL, get_args_location(args), "%s", message);

    // Set compilation failure flag in environment
    if (env) {
        env->compilation_failed = 1;
        puppet_env_increment_error(env);
        if (env->failure_message) {
            puppet_free(env->failure_message);
        }
        env->failure_message = puppet_strdup(message);
    }

    puppet_free(message);

    // Return undef (compilation should stop after this)
    return puppet_value_create_undef();
}

/**
 * notice() - Log an informational message
 * 
 * Usage: notice("Processing node: ", $hostname)
 */
puppet_value_t *puppet_func_notice(puppet_expr_list_t *args, puppet_env_t *env) {
    char *message = concat_args_to_message(args, env);
    puppet_log_loc(PUPPET_LOG_NOTICE, get_args_location(args), "%s", message);
    puppet_free(message);
    return puppet_value_create_undef();
}

/**
 * info() - Log an info message
 * 
 * Usage: info("Starting configuration")
 */
puppet_value_t *puppet_func_info(puppet_expr_list_t *args, puppet_env_t *env) {
    char *message = concat_args_to_message(args, env);
    puppet_log_loc(PUPPET_LOG_INFO, get_args_location(args), "%s", message);
    puppet_free(message);
    return puppet_value_create_undef();
}

/**
 * warning() - Log a warning message
 * 
 * Usage: warning("Deprecated parameter used")
 */
puppet_value_t *puppet_func_warning(puppet_expr_list_t *args, puppet_env_t *env) {
    char *message = concat_args_to_message(args, env);
    puppet_log_loc(PUPPET_LOG_WARNING, get_args_location(args), "%s", message);
    puppet_free(message);
    return puppet_value_create_undef();
}

/**
 * err() - Log an error message (but continue compilation)
 * 
 * Usage: err("Configuration error detected")
 */
puppet_value_t *puppet_func_err(puppet_expr_list_t *args, puppet_env_t *env) {
    char *message = concat_args_to_message(args, env);
    puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args), "%s", message);
    puppet_free(message);
    return puppet_value_create_undef();
}

/**
 * debug() - Log a debug message
 * 
 * Usage: debug("Variable value: ", $myvar)
 */
puppet_value_t *puppet_func_debug(puppet_expr_list_t *args, puppet_env_t *env) {
    char *message = concat_args_to_message(args, env);
    puppet_log_loc(PUPPET_LOG_DEBUG, get_args_location(args), "%s", message);
    puppet_free(message);
    return puppet_value_create_undef();
}

/**
 * defined() - Check if a resource or class is defined
 * 
 * Usage: if defined(File['/etc/motd']) { ... }
 *        if defined('apache') { ... }
 */
puppet_value_t *puppet_func_defined(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count != 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "defined() requires exactly one argument");
        return puppet_value_create_bool(false);
    }
    
    puppet_value_t *arg = puppet_eval_expr(args->exprs[0], env);
    bool is_defined = false;
    
    if (arg->type == PUPPET_VALUE_STRING) {
        const char *name = arg->data.string.data;

        // Check if it's a class that's been loaded
        if (env->loader && puppet_loader_is_class_loaded(env->loader, name)) {
            is_defined = true;
        }

        // Check if it's a class definition in the registry
        if (!is_defined && puppet_find_class_def(env, name)) {
            is_defined = true;
        }

        // Check defined resources (by type::title format)
        if (!is_defined && env->defined_resources) {
            puppet_value_t *resource_val = puppet_hash_get(env->defined_resources, name, strlen(name));
            if (resource_val) {
                is_defined = true;
            }
        }

        // Check resource catalog (type::title format)
        if (!is_defined && env->resource_catalog) {
            puppet_value_t *catalog_val = puppet_hash_get(env->resource_catalog, name, strlen(name));
            if (catalog_val) {
                is_defined = true;
            }
        }

        // Handle resource reference format like File['/etc/motd']
        if (!is_defined) {
            const char *bracket = strchr(name, '[');
            if (bracket && name[strlen(name) - 1] == ']') {
                // Extract type and title
                size_t type_len = bracket - name;
                char *type = puppet_malloc(type_len + 1);
                memcpy(type, name, type_len);
                type[type_len] = '\0';

                size_t title_len = strlen(name) - type_len - 2;  // -2 for [ and ]
                char *title = puppet_malloc(title_len + 1);
                memcpy(title, bracket + 1, title_len);
                title[title_len] = '\0';

                // Remove quotes from title if present
                if (title_len >= 2 && (title[0] == '\'' || title[0] == '"')) {
                    memmove(title, title + 1, title_len - 2);
                    title[title_len - 2] = '\0';
                }

                // Build catalog key (lowercase type::title)
                char catalog_key[512];
                snprintf(catalog_key, sizeof(catalog_key), "%s::%s", type, title);
                // Convert type to lowercase for lookup
                for (char *p = catalog_key; *p && *p != ':'; p++) {
                    *p = (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p;
                }

                if (env->resource_catalog) {
                    puppet_value_t *val = puppet_hash_get(env->resource_catalog, catalog_key, strlen(catalog_key));
                    if (val) is_defined = true;
                }

                puppet_free(type);
                puppet_free(title);
            }
        }
    }
    
    puppet_value_destroy(arg);
    return puppet_value_create_bool(is_defined);
}

/**
 * Helper function to apply a single virtual resource
 * Also used by resource collectors
 */
void realize_single_resource(puppet_stmt_t *stmt, size_t instance_idx, puppet_env_t *env) {
    if (!stmt || stmt->type != PUPPET_STMT_RESOURCE) {
        return;
    }

    if (instance_idx >= stmt->data.resource.instance_count) {
        return;
    }

    puppet_resource_instance_t *instance = &stmt->data.resource.instances[instance_idx];
    if (!instance->title) {
        return;
    }

    puppet_value_t *title_val = puppet_eval_expr(instance->title, env);
    char *title_str = puppet_value_to_display_string(title_val);

    /* Build resource identifier */
    size_t res_id_len = strlen(stmt->data.resource.type.data) + strlen(title_str) + 3;
    char *resource_id = puppet_malloc(res_id_len);
    snprintf(resource_id, res_id_len, "%s[%s]", stmt->data.resource.type.data, title_str);

    /* Check for duplicate resource */
    puppet_value_t *existing = puppet_hash_get(env->resource_catalog, resource_id, strlen(resource_id));
    if (existing) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Duplicate declaration - %s is already declared", resource_id);
        puppet_log(PUPPET_LOG_ERROR, msg);
        puppet_free(resource_id);
        puppet_free(title_str);
        puppet_value_destroy(title_val);
        return;
    }

    /* Add to duplicate detection catalog */
    puppet_value_t *marker = puppet_value_create_bool(true);
    puppet_hash_set(env->resource_catalog, resource_id, strlen(resource_id), marker);

    puppet_debug("Realizing virtual resource: %s", resource_id);

    /* Collect parameters for catalog */
    puppet_catalog_param_t *params = NULL;
    size_t param_count = instance->attr_count;
    if (env->build_catalog && param_count > 0) {
        params = puppet_calloc(param_count, sizeof(puppet_catalog_param_t));
    }

    /* Process attributes */
    for (size_t j = 0; j < instance->attr_count; j++) {
        puppet_value_t *attr_val = puppet_eval_expr(instance->attributes[j].value, env);

        /* Store in catalog params if building catalog */
        if (env->build_catalog && params) {
            params[j].name = puppet_strdup(instance->attributes[j].name.data);
            params[j].value = puppet_value_copy(attr_val);
        }

        puppet_value_destroy(attr_val);
    }

    /* Add to resource catalog if building */
    if (env->build_catalog && env->catalog) {
        puppet_catalog_add_resource(env->catalog,
                                    stmt->data.resource.type.data,
                                    title_str,
                                    params,
                                    param_count,
                                    stmt->loc.filename,
                                    stmt->loc.line);
    }

    puppet_free(resource_id);
    puppet_free(title_str);
    puppet_value_destroy(title_val);
}

/**
 * realize() - Realize virtual resources
 *
 * Usage: realize(User['john'], User['jane'])
 *
 * Materializes virtual resources (@resource syntax) into the catalog.
 * Virtual resources must be declared before they can be realized.
 * Uses pre-evaluated attribute values stored at declaration time.
 */
puppet_value_t *puppet_func_realize(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count == 0) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args), "realize() requires at least one argument");
        return puppet_value_create_undef();
    }

    /* Process each resource reference */
    for (size_t i = 0; i < args->count; i++) {
        /* Evaluate the resource reference expression */
        puppet_value_t *val = puppet_eval_expr(args->exprs[i], env);
        char *ref_str = NULL;
        bool needs_free = false;

        if (val->type == PUPPET_VALUE_STRING) {
            ref_str = val->data.string.data;
        } else {
            ref_str = puppet_value_to_display_string(val);
            needs_free = true;
        }

        /* Normalize reference to lowercase for lookup
         * Virtual resources are stored with lowercase type (e.g., "user[name]")
         * but resource references use capitalized type (e.g., "User['name']")
         */
        char *lookup_key = puppet_strdup(ref_str);
        for (char *p = lookup_key; *p && *p != '['; p++) {
            *p = tolower((unsigned char)*p);
        }
        /* Also handle quoted titles: User['name'] -> user[name] */
        char *src = lookup_key, *dst = lookup_key;
        while (*src) {
            if (*src != '\'') {
                *dst++ = *src;
            }
            src++;
        }
        *dst = '\0';

        /* Look up virtual resource */
        puppet_value_t *stored = puppet_hash_get(env->virtual_resources, lookup_key, strlen(lookup_key));
        if (stored) {
            /* Extract pre-evaluated virtual resource */
            puppet_virtual_resource_t *vres = (puppet_virtual_resource_t *)stored->data.string.data;

            if (vres->realized) {
                puppet_log_loc(PUPPET_LOG_WARNING, args->exprs[i]->loc,
                    "Virtual resource %s already realized", ref_str);
            } else {
                /* Build resource identifier for duplicate check */
                size_t res_id_len = strlen(vres->type) + strlen(vres->title) + 3;
                char *resource_id = puppet_malloc(res_id_len);
                snprintf(resource_id, res_id_len, "%s[%s]", vres->type, vres->title);

                /* Check for duplicate in resource catalog */
                puppet_value_t *existing = puppet_hash_get(env->resource_catalog, resource_id, strlen(resource_id));
                if (existing) {
                    puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[i]->loc,
                        "Duplicate declaration - %s is already declared", resource_id);
                    puppet_free(resource_id);
                } else {
                    /* Add to resource catalog */
                    puppet_value_t *marker = puppet_value_create_bool(true);
                    puppet_hash_set(env->resource_catalog, resource_id, strlen(resource_id), marker);

                    puppet_debug("Realizing virtual resource: %s", resource_id);

                    /* Build catalog resource from pre-evaluated values */
                    if (env->build_catalog && env->catalog) {
                        puppet_catalog_param_t *params = NULL;
                        if (vres->attr_count > 0) {
                            params = puppet_calloc(vres->attr_count, sizeof(puppet_catalog_param_t));
                            for (size_t j = 0; j < vres->attr_count; j++) {
                                if (vres->attrs[j].name) {
                                    params[j].name = puppet_strdup(vres->attrs[j].name);
                                    params[j].value = puppet_value_copy(vres->attrs[j].value);
                                }
                            }
                        }

                        puppet_catalog_add_resource(env->catalog,
                                                    vres->type,
                                                    vres->title,
                                                    params,
                                                    vres->attr_count,
                                                    NULL, 0);  /* TODO: add file/line to virtual_resource */
                    }

                    vres->realized = true;
                    puppet_free(resource_id);

                    puppet_log_loc(PUPPET_LOG_INFO, args->exprs[i]->loc, "Realized: %s", ref_str);
                }
            }
        } else {
            puppet_log_loc(PUPPET_LOG_WARNING, args->exprs[i]->loc,
                "realize: %s is not a virtual resource", ref_str);
        }

        puppet_free(lookup_key);
        if (needs_free) {
            puppet_free(ref_str);
        }
        puppet_value_destroy(val);
    }

    return puppet_value_create_undef();
}

/**
 * tag() - Add tags to the current scope
 * 
 * Usage: tag('webserver', 'production')
 */
puppet_value_t *puppet_func_tag(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count == 0) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "tag() requires at least one argument");
        return puppet_value_create_undef();
    }

    // Initialize tags array if not already present
    if (!env->current_tags) {
        env->current_tags = puppet_value_create_array();
    }

    // Add each tag to the current scope
    for (size_t i = 0; i < args->count; i++) {
        puppet_value_t *tag_val = puppet_eval_expr(args->exprs[i], env);

        if (tag_val->type == PUPPET_VALUE_STRING) {
            // Add to tags array
            puppet_array_append(env->current_tags->data.array, puppet_value_copy(tag_val));

            puppet_log_loc(PUPPET_LOG_DEBUG, args->exprs[i]->loc,
                "Added tag: %s", tag_val->data.string.data);
        } else {
            puppet_log_loc(PUPPET_LOG_WARNING, args->exprs[i]->loc,
                "tag() argument must be a string");
        }

        puppet_value_destroy(tag_val);
    }
    
    return puppet_value_create_undef();
}

/**
 * tagged() - Check if current scope has specific tags
 * 
 * Usage: if tagged('webserver') { ... }
 */
puppet_value_t *puppet_func_tagged(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count != 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "tagged() requires exactly one argument");
        return puppet_value_create_bool(false);
    }
    
    puppet_value_t *search_tag = puppet_eval_expr(args->exprs[0], env);
    bool has_tag = false;
    
    if (search_tag->type == PUPPET_VALUE_STRING && env->current_tags && 
        env->current_tags->type == PUPPET_VALUE_ARRAY) {
        
        // Search through current tags
        for (size_t i = 0; i < env->current_tags->data.array->count; i++) {
            puppet_value_t *tag = env->current_tags->data.array->items[i];
            if (tag->type == PUPPET_VALUE_STRING &&
                strcmp(tag->data.string.data, search_tag->data.string.data) == 0) {
                has_tag = true;
                break;
            }
        }
    }
    
    puppet_value_destroy(search_tag);
    return puppet_value_create_bool(has_tag);
}

/**
 * @brief Lookup function for Hiera data lookups
 * 
 * Supports multiple call signatures:
 * - lookup(key) - simple lookup with nil default
 * - lookup(key, default) - lookup with default value
 * - lookup(key, type, default) - with type checking
 * - lookup(options_hash) - hash with key, default, merge options
 */
puppet_value_t *puppet_func_lookup(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count == 0) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "lookup() requires at least one argument");
        return puppet_value_create_undef();
    }
    
    puppet_value_t *first_arg = puppet_eval_expr(args->exprs[0], env);
    
    // Check if first argument is a hash (options style)
    if (first_arg->type == PUPPET_VALUE_HASH) {
        // Extract options from hash
        puppet_value_t *key_val = puppet_hash_get(first_arg->data.hash, "key", strlen("key"));
        puppet_value_t *default_val = puppet_hash_get(first_arg->data.hash, "default", strlen("default"));
        /* puppet_value_t *merge_val = puppet_hash_get(first_arg->data.hash, "merge", strlen("merge")); */ /* Currently unused */
        
        if (!key_val || key_val->type != PUPPET_VALUE_STRING) {
            puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
                "lookup() options hash must contain 'key' string");
            puppet_value_destroy(first_arg);
            return puppet_value_create_undef();
        }
        
        const char *key = key_val->data.string.data;
        puppet_value_t *result = NULL;
        
        // Try data provider lookup first
        for (size_t i = 0; i < env->data_provider_count; i++) {
            if (env->data_providers[i] && env->data_providers[i]->lookup) {
                result = env->data_providers[i]->lookup(key, env, env->data_providers[i]->data);
                if (result) break;
            }
        }
        
        // Fall back to variable lookup
        if (!result) {
            result = puppet_variable_lookup_chain(env, key);
        }
        
        // Use default if not found
        if (!result && default_val) {
            result = puppet_value_copy(default_val);
        }
        
        puppet_value_destroy(first_arg);
        return result ? result : puppet_value_create_undef();
    }
    
    // Simple lookup(key) or lookup(key, default) style
    if (first_arg->type != PUPPET_VALUE_STRING) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "lookup() key must be a string");
        puppet_value_destroy(first_arg);
        return puppet_value_create_undef();
    }
    
    const char *key = first_arg->data.string.data;
    puppet_value_t *default_value = NULL;
    
    // Get default value if provided
    if (args->count > 1) {
        default_value = puppet_eval_expr(args->exprs[1], env);
    }
    
    // Perform lookup - try data providers first
    puppet_value_t *result = NULL;
    
    for (size_t i = 0; i < env->data_provider_count; i++) {
        if (env->data_providers[i] && env->data_providers[i]->lookup) {
            result = env->data_providers[i]->lookup(key, env, env->data_providers[i]->data);
            if (result) break;
        }
    }
    
    // Fall back to variable lookup
    if (!result) {
        result = puppet_variable_lookup_chain(env, key);
    }
    
    // Use default if not found
    if (!result && default_value) {
        result = puppet_value_copy(default_value);
    }
    
    puppet_value_destroy(first_arg);
    if (default_value) puppet_value_destroy(default_value);

    return result ? result : puppet_value_create_undef();
}

/**
 * @brief Puppet split() function - split a string into an array
 *
 * Usage: split(string, pattern)
 * Returns an array of strings split by the pattern
 *
 * Examples:
 *   split('a,b,c', ',')       => ['a', 'b', 'c']
 *   split('one:two:three', ':') => ['one', 'two', 'three']
 *   split('hello', '')        => ['h', 'e', 'l', 'l', 'o']
 */
puppet_value_t *puppet_func_split(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 2) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "split() requires 2 arguments: string and pattern");
        return puppet_value_create_undef();
    }

    /* Evaluate arguments */
    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *pattern_val = puppet_eval_expr(args->exprs[1], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "split() first argument must be a string");
        if (str_val) puppet_value_destroy(str_val);
        if (pattern_val) puppet_value_destroy(pattern_val);
        return puppet_value_create_undef();
    }

    if (!pattern_val || pattern_val->type != PUPPET_VALUE_STRING) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[1]->loc,
            "split() second argument must be a string pattern");
        puppet_value_destroy(str_val);
        if (pattern_val) puppet_value_destroy(pattern_val);
        return puppet_value_create_undef();
    }

    const char *str = str_val->data.string.data;
    const char *pattern = pattern_val->data.string.data;
    size_t pattern_len = strlen(pattern);

    /* Create result array */
    puppet_value_t *result = puppet_value_create_array();

    /* Handle empty pattern - split into individual characters */
    if (pattern_len == 0) {
        for (size_t i = 0; str[i] != '\0'; i++) {
            char single[2] = { str[i], '\0' };
            puppet_array_append(result->data.array, puppet_value_create_string(single, 1));
        }
    } else {
        /* Split by pattern */
        const char *start = str;
        const char *found;

        while ((found = strstr(start, pattern)) != NULL) {
            /* Create substring from start to found */
            size_t len = found - start;
            char *part = puppet_malloc(len + 1);
            strncpy(part, start, len);
            part[len] = '\0';
            puppet_array_append(result->data.array, puppet_value_create_string(part, len));
            puppet_free(part);

            start = found + pattern_len;
        }

        /* Add remaining part */
        size_t remaining_len = strlen(start);
        puppet_array_append(result->data.array, puppet_value_create_string(start, remaining_len));
    }

    puppet_value_destroy(str_val);
    puppet_value_destroy(pattern_val);

    return result;
}

/**
 * @brief Puppet join() function - join array elements into a string
 *
 * Usage: join(array, separator)
 * Returns a string with array elements joined by separator
 *
 * Examples:
 *   join(['a', 'b', 'c'], ',')     => 'a,b,c'
 *   join(['one', 'two'], ' - ')   => 'one - two'
 *   join([1, 2, 3], ':')          => '1:2:3'
 */
puppet_value_t *puppet_func_join(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 2) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "join() requires 2 arguments: array and separator");
        return puppet_value_create_undef();
    }

    /* Evaluate arguments */
    puppet_value_t *array_val = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *sep_val = puppet_eval_expr(args->exprs[1], env);

    if (!array_val || array_val->type != PUPPET_VALUE_ARRAY) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "join() first argument must be an array");
        if (array_val) puppet_value_destroy(array_val);
        if (sep_val) puppet_value_destroy(sep_val);
        return puppet_value_create_undef();
    }

    if (!sep_val || sep_val->type != PUPPET_VALUE_STRING) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[1]->loc,
            "join() second argument must be a string separator");
        puppet_value_destroy(array_val);
        if (sep_val) puppet_value_destroy(sep_val);
        return puppet_value_create_undef();
    }

    const char *separator = sep_val->data.string.data;
    size_t sep_len = strlen(separator);
    puppet_array_t *array = array_val->data.array;

    /* Handle empty array */
    if (array->count == 0) {
        puppet_value_destroy(array_val);
        puppet_value_destroy(sep_val);
        return puppet_value_create_string("", 0);
    }

    /* Calculate total length needed */
    size_t total_len = 0;
    char **parts = puppet_calloc(array->count, sizeof(char*));

    for (size_t i = 0; i < array->count; i++) {
        parts[i] = puppet_value_to_display_string(array->items[i]);
        total_len += strlen(parts[i]);
        if (i > 0) total_len += sep_len;
    }

    /* Build result string */
    char *result_str = puppet_malloc(total_len + 1);
    size_t offset = 0;

    for (size_t i = 0; i < array->count; i++) {
        if (i > 0) {
            memcpy(result_str + offset, separator, sep_len);
            offset += sep_len;
        }
        size_t part_len = strlen(parts[i]);
        memcpy(result_str + offset, parts[i], part_len);
        offset += part_len;
        puppet_free(parts[i]);
    }
    result_str[total_len] = '\0';

    puppet_free(parts);
    puppet_value_destroy(array_val);
    puppet_value_destroy(sep_val);

    puppet_value_t *result = puppet_value_create_string(result_str, total_len);
    puppet_free(result_str);

    return result;
}

/**
 * @brief Puppet downcase() function - convert string to lowercase
 *
 * Usage: downcase(string)
 * Returns the string with all characters converted to lowercase
 *
 * Examples:
 *   downcase('HELLO')     => 'hello'
 *   downcase('Hello')     => 'hello'
 *   downcase('hello')     => 'hello'
 */
puppet_value_t *puppet_func_downcase(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "downcase() requires 1 argument: string");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "downcase() argument must be a string");
        if (str_val) puppet_value_destroy(str_val);
        return puppet_value_create_undef();
    }

    const char *src = str_val->data.string.data;
    size_t len = str_val->data.string.len;
    char *result_str = puppet_malloc(len + 1);

    for (size_t i = 0; i < len; i++) {
        unsigned char c = src[i];
        if (c >= 'A' && c <= 'Z') {
            result_str[i] = c + ('a' - 'A');
        } else {
            result_str[i] = c;
        }
    }
    result_str[len] = '\0';

    puppet_value_t *result = puppet_value_create_string(result_str, len);
    puppet_free(result_str);
    puppet_value_destroy(str_val);

    return result;
}

/**
 * @brief Puppet upcase() function - convert string to uppercase
 *
 * Usage: upcase(string)
 * Returns the string with all characters converted to uppercase
 *
 * Examples:
 *   upcase('hello')     => 'HELLO'
 *   upcase('Hello')     => 'HELLO'
 *   upcase('HELLO')     => 'HELLO'
 */
puppet_value_t *puppet_func_upcase(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "upcase() requires 1 argument: string");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "upcase() argument must be a string");
        if (str_val) puppet_value_destroy(str_val);
        return puppet_value_create_undef();
    }

    const char *src = str_val->data.string.data;
    size_t len = str_val->data.string.len;
    char *result_str = puppet_malloc(len + 1);

    for (size_t i = 0; i < len; i++) {
        unsigned char c = src[i];
        if (c >= 'a' && c <= 'z') {
            result_str[i] = c - ('a' - 'A');
        } else {
            result_str[i] = c;
        }
    }
    result_str[len] = '\0';

    puppet_value_t *result = puppet_value_create_string(result_str, len);
    puppet_free(result_str);
    puppet_value_destroy(str_val);

    return result;
}

/**
 * @brief Puppet strip() function - remove leading and trailing whitespace
 *
 * Usage: strip(string)
 * Returns the string with leading and trailing whitespace removed
 *
 * Examples:
 *   strip('  hello  ')     => 'hello'
 *   strip('\thello\n')     => 'hello'
 *   strip('hello')         => 'hello'
 */
puppet_value_t *puppet_func_strip(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "strip() requires 1 argument: string");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "strip() argument must be a string");
        if (str_val) puppet_value_destroy(str_val);
        return puppet_value_create_undef();
    }

    const char *src = str_val->data.string.data;
    size_t len = str_val->data.string.len;

    /* Find start (skip leading whitespace) */
    size_t start = 0;
    while (start < len && (src[start] == ' ' || src[start] == '\t' ||
                           src[start] == '\n' || src[start] == '\r')) {
        start++;
    }

    /* Find end (skip trailing whitespace) */
    size_t end = len;
    while (end > start && (src[end - 1] == ' ' || src[end - 1] == '\t' ||
                           src[end - 1] == '\n' || src[end - 1] == '\r')) {
        end--;
    }

    size_t new_len = end - start;
    char *result_str = puppet_malloc(new_len + 1);
    if (new_len > 0) {
        memcpy(result_str, src + start, new_len);
    }
    result_str[new_len] = '\0';

    puppet_value_t *result = puppet_value_create_string(result_str, new_len);
    puppet_free(result_str);
    puppet_value_destroy(str_val);

    return result;
}

/**
 * @brief Puppet lstrip() function - remove leading whitespace
 *
 * Usage: lstrip(string)
 * Returns the string with leading whitespace removed
 *
 * Examples:
 *   lstrip('  hello')   => 'hello'
 *   lstrip('\thello')   => 'hello'
 */
puppet_value_t *puppet_func_lstrip(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "lstrip() requires 1 argument: string");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "lstrip() argument must be a string");
        if (str_val) puppet_value_destroy(str_val);
        return puppet_value_create_undef();
    }

    const char *src = str_val->data.string.data;
    size_t len = str_val->data.string.len;

    /* Find start (skip leading whitespace) */
    size_t start = 0;
    while (start < len && (src[start] == ' ' || src[start] == '\t' ||
                           src[start] == '\n' || src[start] == '\r')) {
        start++;
    }

    size_t new_len = len - start;
    char *result_str = puppet_malloc(new_len + 1);
    if (new_len > 0) {
        memcpy(result_str, src + start, new_len);
    }
    result_str[new_len] = '\0';

    puppet_value_t *result = puppet_value_create_string(result_str, new_len);
    puppet_free(result_str);
    puppet_value_destroy(str_val);

    return result;
}

/**
 * @brief Puppet rstrip() function - remove trailing whitespace
 *
 * Usage: rstrip(string)
 * Returns the string with trailing whitespace removed
 *
 * Examples:
 *   rstrip('hello  ')   => 'hello'
 *   rstrip('hello\n')   => 'hello'
 */
puppet_value_t *puppet_func_rstrip(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "rstrip() requires 1 argument: string");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "rstrip() argument must be a string");
        if (str_val) puppet_value_destroy(str_val);
        return puppet_value_create_undef();
    }

    const char *src = str_val->data.string.data;
    size_t len = str_val->data.string.len;

    /* Find end (skip trailing whitespace) */
    size_t end = len;
    while (end > 0 && (src[end - 1] == ' ' || src[end - 1] == '\t' ||
                       src[end - 1] == '\n' || src[end - 1] == '\r')) {
        end--;
    }

    char *result_str = puppet_malloc(end + 1);
    if (end > 0) {
        memcpy(result_str, src, end);
    }
    result_str[end] = '\0';

    puppet_value_t *result = puppet_value_create_string(result_str, end);
    puppet_free(result_str);
    puppet_value_destroy(str_val);

    return result;
}

/**
 * @brief Puppet chomp() function - remove trailing newline
 *
 * Usage: chomp(string)
 * Returns the string with trailing newline(s) removed
 *
 * Examples:
 *   chomp("hello\n")     => 'hello'
 *   chomp("hello\r\n")   => 'hello'
 *   chomp("hello")       => 'hello'
 */
puppet_value_t *puppet_func_chomp(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "chomp() requires 1 argument: string");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "chomp() argument must be a string");
        if (str_val) puppet_value_destroy(str_val);
        return puppet_value_create_undef();
    }

    const char *src = str_val->data.string.data;
    size_t len = str_val->data.string.len;

    /* Remove trailing \n and \r only */
    size_t end = len;
    while (end > 0 && (src[end - 1] == '\n' || src[end - 1] == '\r')) {
        end--;
    }

    char *result_str = puppet_malloc(end + 1);
    if (end > 0) {
        memcpy(result_str, src, end);
    }
    result_str[end] = '\0';

    puppet_value_t *result = puppet_value_create_string(result_str, end);
    puppet_free(result_str);
    puppet_value_destroy(str_val);

    return result;
}

/**
 * @brief Puppet chop() function - remove last character
 *
 * Usage: chop(string)
 * Returns the string with the last character removed
 *
 * Examples:
 *   chop('hello')   => 'hell'
 *   chop('hello\n') => 'hello'
 *   chop('')        => ''
 */
puppet_value_t *puppet_func_chop(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "chop() requires 1 argument: string");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "chop() argument must be a string");
        if (str_val) puppet_value_destroy(str_val);
        return puppet_value_create_undef();
    }

    size_t len = str_val->data.string.len;
    size_t new_len = (len > 0) ? len - 1 : 0;

    char *result_str = puppet_malloc(new_len + 1);
    if (new_len > 0) {
        memcpy(result_str, str_val->data.string.data, new_len);
    }
    result_str[new_len] = '\0';

    puppet_value_t *result = puppet_value_create_string(result_str, new_len);
    puppet_free(result_str);
    puppet_value_destroy(str_val);

    return result;
}

/**
 * @brief Puppet capitalize() function - capitalize first letter
 *
 * Usage: capitalize(string)
 * Returns the string with the first character uppercased and rest lowercased
 *
 * Examples:
 *   capitalize('hello')   => 'Hello'
 *   capitalize('HELLO')   => 'Hello'
 *   capitalize('hELLO')   => 'Hello'
 */
puppet_value_t *puppet_func_capitalize(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "capitalize() requires 1 argument: string");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "capitalize() argument must be a string");
        if (str_val) puppet_value_destroy(str_val);
        return puppet_value_create_undef();
    }

    size_t len = str_val->data.string.len;
    char *result_str = puppet_malloc(len + 1);

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str_val->data.string.data[i];
        if (i == 0) {
            result_str[i] = (char)toupper(c);
        } else {
            result_str[i] = (char)tolower(c);
        }
    }
    result_str[len] = '\0';

    puppet_value_t *result = puppet_value_create_string(result_str, len);
    puppet_free(result_str);
    puppet_value_destroy(str_val);

    return result;
}

/**
 * @brief Puppet size() function - get length of string, array, or hash
 *
 * Usage: size(value)
 * Returns the length/count of the value
 *
 * Examples:
 *   size('hello')        => 5
 *   size([1, 2, 3])      => 3
 *   size({a => 1, b => 2}) => 2
 */
puppet_value_t *puppet_func_size(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "size() requires 1 argument");
        return puppet_value_create_undef();
    }

    puppet_value_t *val = puppet_eval_expr(args->exprs[0], env);
    double result = 0;

    switch (val->type) {
        case PUPPET_VALUE_STRING:
            result = (double)val->data.string.len;
            break;
        case PUPPET_VALUE_ARRAY:
            result = (double)val->data.array->count;
            break;
        case PUPPET_VALUE_HASH:
            /* Count hash entries */
            if (val->data.hash) {
                for (size_t i = 0; i < val->data.hash->bucket_count; i++) {
                    puppet_hash_entry_t *entry = val->data.hash->buckets[i];
                    while (entry) {
                        result++;
                        entry = entry->next;
                    }
                }
            }
            break;
        default:
            puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
                "size() argument must be a string, array, or hash");
            puppet_value_destroy(val);
            return puppet_value_create_undef();
    }

    puppet_value_destroy(val);
    return puppet_value_create_number(result);
}

/**
 * @brief Puppet empty() function - check if value is empty
 *
 * Usage: empty(value)
 * Returns true if the value is empty
 *
 * Examples:
 *   empty('')           => true
 *   empty('hello')      => false
 *   empty([])           => true
 *   empty([1, 2])       => false
 *   empty({})           => true
 */
puppet_value_t *puppet_func_empty(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "empty() requires 1 argument");
        return puppet_value_create_undef();
    }

    puppet_value_t *val = puppet_eval_expr(args->exprs[0], env);
    bool is_empty = false;

    switch (val->type) {
        case PUPPET_VALUE_UNDEF:
            is_empty = true;
            break;
        case PUPPET_VALUE_STRING:
            is_empty = (val->data.string.len == 0);
            break;
        case PUPPET_VALUE_ARRAY:
            is_empty = (val->data.array->count == 0);
            break;
        case PUPPET_VALUE_HASH:
            /* Check if hash has any entries */
            is_empty = true;
            if (val->data.hash) {
                for (size_t i = 0; i < val->data.hash->bucket_count && is_empty; i++) {
                    if (val->data.hash->buckets[i]) {
                        is_empty = false;
                    }
                }
            }
            break;
        default:
            is_empty = false;
            break;
    }

    puppet_value_destroy(val);
    return puppet_value_create_bool(is_empty);
}

/**
 * @brief Puppet keys() function - get array of hash keys
 *
 * Usage: keys(hash)
 * Returns an array containing all keys from the hash
 *
 * Examples:
 *   keys({a => 1, b => 2}) => ['a', 'b']
 *   keys({})               => []
 */
puppet_value_t *puppet_func_keys(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "keys() requires 1 argument: hash");
        return puppet_value_create_undef();
    }

    puppet_value_t *hash_val = puppet_eval_expr(args->exprs[0], env);

    if (!hash_val || hash_val->type != PUPPET_VALUE_HASH) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "keys() argument must be a hash");
        if (hash_val) puppet_value_destroy(hash_val);
        return puppet_value_create_undef();
    }

    puppet_value_t *result = puppet_value_create_array();

    if (hash_val->data.hash) {
        for (size_t i = 0; i < hash_val->data.hash->bucket_count; i++) {
            puppet_hash_entry_t *entry = hash_val->data.hash->buckets[i];
            while (entry) {
                puppet_array_append(result->data.array,
                    puppet_value_create_string(entry->key.data, entry->key.len));
                entry = entry->next;
            }
        }
    }

    puppet_value_destroy(hash_val);
    return result;
}

/**
 * @brief Puppet values() function - get array of hash values
 *
 * Usage: values(hash)
 * Returns an array containing all values from the hash
 *
 * Examples:
 *   values({a => 1, b => 2}) => [1, 2]
 *   values({})               => []
 */
puppet_value_t *puppet_func_values(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "values() requires 1 argument: hash");
        return puppet_value_create_undef();
    }

    puppet_value_t *hash_val = puppet_eval_expr(args->exprs[0], env);

    if (!hash_val || hash_val->type != PUPPET_VALUE_HASH) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "values() argument must be a hash");
        if (hash_val) puppet_value_destroy(hash_val);
        return puppet_value_create_undef();
    }

    puppet_value_t *result = puppet_value_create_array();

    if (hash_val->data.hash) {
        for (size_t i = 0; i < hash_val->data.hash->bucket_count; i++) {
            puppet_hash_entry_t *entry = hash_val->data.hash->buckets[i];
            while (entry) {
                puppet_array_append(result->data.array, puppet_value_copy(entry->value));
                entry = entry->next;
            }
        }
    }

    puppet_value_destroy(hash_val);
    return result;
}

/**
 * @brief Puppet has_key() function - check if hash contains a key
 *
 * Usage: has_key(hash, key)
 * Returns true if the hash contains the specified key
 *
 * Examples:
 *   has_key({a => 1, b => 2}, 'a') => true
 *   has_key({a => 1, b => 2}, 'c') => false
 */
puppet_value_t *puppet_func_has_key(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 2) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "has_key() requires 2 arguments: hash and key");
        return puppet_value_create_bool(false);
    }

    puppet_value_t *hash_val = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *key_val = puppet_eval_expr(args->exprs[1], env);

    if (!hash_val || hash_val->type != PUPPET_VALUE_HASH) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "has_key() first argument must be a hash");
        if (hash_val) puppet_value_destroy(hash_val);
        if (key_val) puppet_value_destroy(key_val);
        return puppet_value_create_bool(false);
    }

    if (!key_val || key_val->type != PUPPET_VALUE_STRING) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[1]->loc,
            "has_key() second argument must be a string");
        puppet_value_destroy(hash_val);
        if (key_val) puppet_value_destroy(key_val);
        return puppet_value_create_bool(false);
    }

    const char *key = key_val->data.string.data;
    size_t key_len = key_val->data.string.len;

    puppet_value_t *found = puppet_hash_get(hash_val->data.hash, key, key_len);
    bool has = (found != NULL);

    puppet_value_destroy(hash_val);
    puppet_value_destroy(key_val);

    return puppet_value_create_bool(has);
}

/**
 * @brief Puppet member() / contain() function - check if array contains a value
 *
 * Usage: member(array, value) or contain(array, value)
 * Returns true if the array contains the specified value
 *
 * Examples:
 *   member(['a', 'b', 'c'], 'b') => true
 *   member(['a', 'b', 'c'], 'd') => false
 *   contain([1, 2, 3], 2)        => true
 */
puppet_value_t *puppet_func_member(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 2) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "member() requires 2 arguments: array and value");
        return puppet_value_create_bool(false);
    }

    puppet_value_t *array_val = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *search_val = puppet_eval_expr(args->exprs[1], env);

    if (!array_val || array_val->type != PUPPET_VALUE_ARRAY) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "member() first argument must be an array");
        if (array_val) puppet_value_destroy(array_val);
        if (search_val) puppet_value_destroy(search_val);
        return puppet_value_create_bool(false);
    }

    bool found = false;
    puppet_array_t *arr = array_val->data.array;

    for (size_t i = 0; i < arr->count && !found; i++) {
        puppet_value_t *item = arr->items[i];

        /* Compare based on type */
        if (item->type == search_val->type) {
            switch (item->type) {
                case PUPPET_VALUE_STRING:
                    if (item->data.string.len == search_val->data.string.len &&
                        strcmp(item->data.string.data, search_val->data.string.data) == 0) {
                        found = true;
                    }
                    break;
                case PUPPET_VALUE_NUMBER:
                    if (item->data.number == search_val->data.number) {
                        found = true;
                    }
                    break;
                case PUPPET_VALUE_BOOL:
                    if (item->data.boolean == search_val->data.boolean) {
                        found = true;
                    }
                    break;
                default:
                    break;
            }
        }
    }

    puppet_value_destroy(array_val);
    puppet_value_destroy(search_val);

    return puppet_value_create_bool(found);
}

/**
 * @brief Puppet reverse() function - reverse array elements
 *
 * Usage: reverse(array)
 * Returns a new array with elements in reverse order
 *
 * Examples:
 *   reverse([1, 2, 3])     => [3, 2, 1]
 *   reverse(['a', 'b'])    => ['b', 'a']
 */
puppet_value_t *puppet_func_reverse(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "reverse() requires 1 argument: array");
        return puppet_value_create_undef();
    }

    puppet_value_t *array_val = puppet_eval_expr(args->exprs[0], env);

    if (!array_val || array_val->type != PUPPET_VALUE_ARRAY) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "reverse() argument must be an array");
        if (array_val) puppet_value_destroy(array_val);
        return puppet_value_create_undef();
    }

    puppet_value_t *result = puppet_value_create_array();
    puppet_array_t *arr = array_val->data.array;

    /* Add elements in reverse order */
    for (size_t i = arr->count; i > 0; i--) {
        puppet_array_append(result->data.array, puppet_value_copy(arr->items[i - 1]));
    }

    puppet_value_destroy(array_val);
    return result;
}

/**
 * @brief Puppet unique() function - remove duplicate elements from array
 *
 * Usage: unique(array)
 * Returns a new array with duplicate elements removed (first occurrence kept)
 *
 * Examples:
 *   unique([1, 2, 1, 3, 2])     => [1, 2, 3]
 *   unique(['a', 'b', 'a'])     => ['a', 'b']
 */
puppet_value_t *puppet_func_unique(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "unique() requires 1 argument: array");
        return puppet_value_create_undef();
    }

    puppet_value_t *array_val = puppet_eval_expr(args->exprs[0], env);

    if (!array_val || array_val->type != PUPPET_VALUE_ARRAY) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "unique() argument must be an array");
        if (array_val) puppet_value_destroy(array_val);
        return puppet_value_create_undef();
    }

    puppet_value_t *result = puppet_value_create_array();
    puppet_array_t *arr = array_val->data.array;

    for (size_t i = 0; i < arr->count; i++) {
        puppet_value_t *item = arr->items[i];
        bool is_duplicate = false;

        /* Check if this item already exists in result */
        for (size_t j = 0; j < result->data.array->count && !is_duplicate; j++) {
            puppet_value_t *existing = result->data.array->items[j];

            if (item->type == existing->type) {
                switch (item->type) {
                    case PUPPET_VALUE_STRING:
                        if (item->data.string.len == existing->data.string.len &&
                            strcmp(item->data.string.data, existing->data.string.data) == 0) {
                            is_duplicate = true;
                        }
                        break;
                    case PUPPET_VALUE_NUMBER:
                        if (item->data.number == existing->data.number) {
                            is_duplicate = true;
                        }
                        break;
                    case PUPPET_VALUE_BOOL:
                        if (item->data.boolean == existing->data.boolean) {
                            is_duplicate = true;
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        if (!is_duplicate) {
            puppet_array_append(result->data.array, puppet_value_copy(item));
        }
    }

    puppet_value_destroy(array_val);
    return result;
}

/* Comparison function for sorting strings */
static int compare_strings(const void *a, const void *b) {
    puppet_value_t *va = *(puppet_value_t **)a;
    puppet_value_t *vb = *(puppet_value_t **)b;
    return strcmp(va->data.string.data, vb->data.string.data);
}

/* Comparison function for sorting numbers */
static int compare_numbers(const void *a, const void *b) {
    puppet_value_t *va = *(puppet_value_t **)a;
    puppet_value_t *vb = *(puppet_value_t **)b;
    if (va->data.number < vb->data.number) return -1;
    if (va->data.number > vb->data.number) return 1;
    return 0;
}

/**
 * @brief Puppet sort() function - sort array elements
 *
 * Usage: sort(array)
 * Returns a new array with elements sorted
 *
 * Examples:
 *   sort([3, 1, 2])           => [1, 2, 3]
 *   sort(['c', 'a', 'b'])     => ['a', 'b', 'c']
 */
puppet_value_t *puppet_func_sort(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "sort() requires 1 argument: array");
        return puppet_value_create_undef();
    }

    puppet_value_t *array_val = puppet_eval_expr(args->exprs[0], env);

    if (!array_val || array_val->type != PUPPET_VALUE_ARRAY) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "sort() argument must be an array");
        if (array_val) puppet_value_destroy(array_val);
        return puppet_value_create_undef();
    }

    puppet_array_t *arr = array_val->data.array;

    if (arr->count == 0) {
        puppet_value_destroy(array_val);
        return puppet_value_create_array();
    }

    /* Create result array with copies of elements */
    puppet_value_t *result = puppet_value_create_array();
    for (size_t i = 0; i < arr->count; i++) {
        puppet_array_append(result->data.array, puppet_value_copy(arr->items[i]));
    }

    /* Determine element type and sort accordingly */
    puppet_value_type_t first_type = arr->items[0]->type;

    if (first_type == PUPPET_VALUE_STRING) {
        qsort(result->data.array->items, result->data.array->count,
              sizeof(puppet_value_t *), compare_strings);
    } else if (first_type == PUPPET_VALUE_NUMBER) {
        qsort(result->data.array->items, result->data.array->count,
              sizeof(puppet_value_t *), compare_numbers);
    }
    /* For mixed types or other types, leave unsorted */

    puppet_value_destroy(array_val);
    return result;
}

/* Helper function to recursively flatten arrays */
static void flatten_recursive(puppet_value_t *item, puppet_array_t *result) {
    if (item->type == PUPPET_VALUE_ARRAY) {
        for (size_t i = 0; i < item->data.array->count; i++) {
            flatten_recursive(item->data.array->items[i], result);
        }
    } else {
        puppet_array_append(result, puppet_value_copy(item));
    }
}

/**
 * @brief Puppet flatten() function - flatten nested arrays
 *
 * Usage: flatten(array)
 * Returns a new array with all nested arrays flattened to a single level
 *
 * Examples:
 *   flatten([[1, 2], [3, 4]])     => [1, 2, 3, 4]
 *   flatten([1, [2, [3, 4]], 5])  => [1, 2, 3, 4, 5]
 */
puppet_value_t *puppet_func_flatten(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "flatten() requires 1 argument: array");
        return puppet_value_create_undef();
    }

    puppet_value_t *array_val = puppet_eval_expr(args->exprs[0], env);

    if (!array_val || array_val->type != PUPPET_VALUE_ARRAY) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "flatten() argument must be an array");
        if (array_val) puppet_value_destroy(array_val);
        return puppet_value_create_undef();
    }

    puppet_value_t *result = puppet_value_create_array();

    /* Recursively flatten */
    for (size_t i = 0; i < array_val->data.array->count; i++) {
        flatten_recursive(array_val->data.array->items[i], result->data.array);
    }

    puppet_value_destroy(array_val);
    return result;
}

/**
 * @brief Puppet concat() function - concatenate arrays
 *
 * Usage: concat(array1, array2, ...)
 * Returns a new array with all elements from all input arrays
 *
 * Examples:
 *   concat([1, 2], [3, 4])        => [1, 2, 3, 4]
 *   concat([1], [2], [3])         => [1, 2, 3]
 */
puppet_value_t *puppet_func_concat(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "concat() requires at least 1 argument");
        return puppet_value_create_undef();
    }

    puppet_value_t *result = puppet_value_create_array();

    for (size_t i = 0; i < args->count; i++) {
        puppet_value_t *val = puppet_eval_expr(args->exprs[i], env);
        if (val && val->type == PUPPET_VALUE_ARRAY) {
            for (size_t j = 0; j < val->data.array->count; j++) {
                puppet_array_append(result->data.array,
                    puppet_value_copy(val->data.array->items[j]));
            }
        } else if (val) {
            /* Non-array values are added as single elements */
            puppet_array_append(result->data.array, puppet_value_copy(val));
        }
        if (val) puppet_value_destroy(val);
    }

    return result;
}

/**
 * @brief Puppet delete() function - delete elements from array or hash
 *
 * Usage: delete(array, value) or delete(hash, key)
 * Returns a new array/hash with the specified element(s) removed
 *
 * Examples:
 *   delete(['a', 'b', 'c'], 'b')   => ['a', 'c']
 *   delete({a => 1, b => 2}, 'a') => {b => 2}
 */
puppet_value_t *puppet_func_delete(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 2) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "delete() requires 2 arguments: collection, value");
        return puppet_value_create_undef();
    }

    puppet_value_t *coll = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *target = puppet_eval_expr(args->exprs[1], env);

    if (!coll) {
        if (target) puppet_value_destroy(target);
        return puppet_value_create_undef();
    }

    if (coll->type == PUPPET_VALUE_ARRAY) {
        puppet_value_t *result = puppet_value_create_array();

        for (size_t i = 0; i < coll->data.array->count; i++) {
            puppet_value_t *item = coll->data.array->items[i];
            bool should_delete = false;

            /* Compare values */
            if (target && item->type == target->type) {
                if (item->type == PUPPET_VALUE_STRING) {
                    if (item->data.string.len == target->data.string.len &&
                        memcmp(item->data.string.data, target->data.string.data,
                               item->data.string.len) == 0) {
                        should_delete = true;
                    }
                } else if (item->type == PUPPET_VALUE_NUMBER) {
                    if (item->data.number == target->data.number) {
                        should_delete = true;
                    }
                }
            }

            if (!should_delete) {
                puppet_array_append(result->data.array, puppet_value_copy(item));
            }
        }

        puppet_value_destroy(coll);
        puppet_value_destroy(target);
        return result;
    } else if (coll->type == PUPPET_VALUE_HASH) {
        puppet_value_t *result = puppet_value_create_hash();

        /* Get key to delete */
        const char *del_key = NULL;
        size_t del_key_len = 0;
        if (target && target->type == PUPPET_VALUE_STRING) {
            del_key = target->data.string.data;
            del_key_len = target->data.string.len;
        }

        /* Copy all entries except the one to delete */
        for (size_t i = 0; i < coll->data.hash->bucket_count; i++) {
            puppet_hash_entry_t *entry = coll->data.hash->buckets[i];
            while (entry) {
                bool should_delete = false;
                if (del_key && entry->key.len == del_key_len &&
                    memcmp(entry->key.data, del_key, del_key_len) == 0) {
                    should_delete = true;
                }
                if (!should_delete) {
                    puppet_hash_set(result->data.hash, entry->key.data, entry->key.len,
                                   puppet_value_copy(entry->value));
                }
                entry = entry->next;
            }
        }

        puppet_value_destroy(coll);
        puppet_value_destroy(target);
        return result;
    }

    puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
        "delete() first argument must be an array or hash");
    puppet_value_destroy(coll);
    if (target) puppet_value_destroy(target);
    return puppet_value_create_undef();
}

/**
 * @brief Puppet delete_at() function - delete element at index
 *
 * Usage: delete_at(array, index)
 * Returns a new array with the element at the specified index removed
 *
 * Examples:
 *   delete_at(['a', 'b', 'c'], 1)   => ['a', 'c']
 *   delete_at(['a', 'b', 'c'], 0)   => ['b', 'c']
 *   delete_at(['a', 'b', 'c'], -1)  => ['a', 'b']
 */
puppet_value_t *puppet_func_delete_at(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 2) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "delete_at() requires 2 arguments: array, index");
        return puppet_value_create_undef();
    }

    puppet_value_t *arr = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *idx_val = puppet_eval_expr(args->exprs[1], env);

    if (!arr || arr->type != PUPPET_VALUE_ARRAY) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "delete_at() first argument must be an array");
        if (arr) puppet_value_destroy(arr);
        if (idx_val) puppet_value_destroy(idx_val);
        return puppet_value_create_undef();
    }

    if (!idx_val || idx_val->type != PUPPET_VALUE_NUMBER) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[1]->loc,
            "delete_at() second argument must be a number");
        puppet_value_destroy(arr);
        if (idx_val) puppet_value_destroy(idx_val);
        return puppet_value_create_undef();
    }

    int idx = (int)idx_val->data.number;
    size_t count = arr->data.array->count;

    /* Handle negative index */
    if (idx < 0) {
        idx = (int)count + idx;
    }

    puppet_value_t *result = puppet_value_create_array();

    for (size_t i = 0; i < count; i++) {
        if ((int)i != idx) {
            puppet_array_append(result->data.array,
                puppet_value_copy(arr->data.array->items[i]));
        }
    }

    puppet_value_destroy(arr);
    puppet_value_destroy(idx_val);
    return result;
}

/**
 * @brief Puppet first() function - get first element of array
 *
 * Usage: first(array)
 * Returns the first element of the array, or undef if empty
 *
 * Examples:
 *   first(['a', 'b', 'c'])   => 'a'
 *   first([])                => undef
 */
puppet_value_t *puppet_func_first(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "first() requires 1 argument: array");
        return puppet_value_create_undef();
    }

    puppet_value_t *arr = puppet_eval_expr(args->exprs[0], env);

    if (!arr || arr->type != PUPPET_VALUE_ARRAY) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "first() argument must be an array");
        if (arr) puppet_value_destroy(arr);
        return puppet_value_create_undef();
    }

    if (arr->data.array->count == 0) {
        puppet_value_destroy(arr);
        return puppet_value_create_undef();
    }

    puppet_value_t *result = puppet_value_copy(arr->data.array->items[0]);
    puppet_value_destroy(arr);
    return result;
}

/**
 * @brief Puppet last() function - get last element of array
 *
 * Usage: last(array)
 * Returns the last element of the array, or undef if empty
 *
 * Examples:
 *   last(['a', 'b', 'c'])   => 'c'
 *   last([])                => undef
 */
puppet_value_t *puppet_func_last(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "last() requires 1 argument: array");
        return puppet_value_create_undef();
    }

    puppet_value_t *arr = puppet_eval_expr(args->exprs[0], env);

    if (!arr || arr->type != PUPPET_VALUE_ARRAY) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "last() argument must be an array");
        if (arr) puppet_value_destroy(arr);
        return puppet_value_create_undef();
    }

    if (arr->data.array->count == 0) {
        puppet_value_destroy(arr);
        return puppet_value_create_undef();
    }

    puppet_value_t *result = puppet_value_copy(arr->data.array->items[arr->data.array->count - 1]);
    puppet_value_destroy(arr);
    return result;
}

/**
 * @brief Puppet range() function - create array of sequential values
 *
 * Usage: range(start, end) or range(start, end, step)
 * Returns an array of values from start to end (inclusive)
 *
 * Examples:
 *   range(1, 5)        => [1, 2, 3, 4, 5]
 *   range(1, 10, 2)    => [1, 3, 5, 7, 9]
 *   range(5, 1)        => [5, 4, 3, 2, 1]
 *   range('a', 'e')    => ['a', 'b', 'c', 'd', 'e']
 */
puppet_value_t *puppet_func_range(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 2) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "range() requires at least 2 arguments: start, end");
        return puppet_value_create_undef();
    }

    puppet_value_t *start_val = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *end_val = puppet_eval_expr(args->exprs[1], env);
    puppet_value_t *step_val = NULL;

    if (args->count >= 3) {
        step_val = puppet_eval_expr(args->exprs[2], env);
    }

    puppet_value_t *result = puppet_value_create_array();

    /* Handle numeric range */
    if (start_val && start_val->type == PUPPET_VALUE_NUMBER &&
        end_val && end_val->type == PUPPET_VALUE_NUMBER) {

        int start = (int)start_val->data.number;
        int end = (int)end_val->data.number;
        int step = 1;

        if (step_val && step_val->type == PUPPET_VALUE_NUMBER) {
            step = (int)step_val->data.number;
            if (step == 0) step = 1;
        }

        /* Auto-detect direction */
        if (start > end && step > 0) {
            step = -step;
        } else if (start < end && step < 0) {
            step = -step;
        }

        if (step > 0) {
            for (int i = start; i <= end; i += step) {
                puppet_array_append(result->data.array, puppet_value_create_number((double)i));
            }
        } else {
            for (int i = start; i >= end; i += step) {
                puppet_array_append(result->data.array, puppet_value_create_number((double)i));
            }
        }
    }
    /* Handle character range */
    else if (start_val && start_val->type == PUPPET_VALUE_STRING &&
             start_val->data.string.len == 1 &&
             end_val && end_val->type == PUPPET_VALUE_STRING &&
             end_val->data.string.len == 1) {

        char start_char = start_val->data.string.data[0];
        char end_char = end_val->data.string.data[0];
        int step = 1;

        if (step_val && step_val->type == PUPPET_VALUE_NUMBER) {
            step = (int)step_val->data.number;
            if (step == 0) step = 1;
        }

        if (start_char > end_char && step > 0) {
            step = -step;
        } else if (start_char < end_char && step < 0) {
            step = -step;
        }

        char buf[2] = {0, 0};
        if (step > 0) {
            for (char c = start_char; c <= end_char; c += step) {
                buf[0] = c;
                puppet_array_append(result->data.array, puppet_value_create_string(buf, 1));
            }
        } else {
            for (char c = start_char; c >= end_char; c += step) {
                buf[0] = c;
                puppet_array_append(result->data.array, puppet_value_create_string(buf, 1));
            }
        }
    } else {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "range() requires numeric or single-character string arguments");
    }

    puppet_value_destroy(start_val);
    puppet_value_destroy(end_val);
    if (step_val) puppet_value_destroy(step_val);

    return result;
}

/**
 * @brief Puppet merge() function - merge hashes
 *
 * Usage: merge(hash1, hash2, ...)
 * Returns a new hash containing all key-value pairs from all input hashes.
 * Later hashes override earlier ones for duplicate keys.
 *
 * Examples:
 *   merge({a => 1}, {b => 2})       => {a => 1, b => 2}
 *   merge({a => 1}, {a => 2})       => {a => 2}
 */
puppet_value_t *puppet_func_merge(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "merge() requires at least 1 argument");
        return puppet_value_create_undef();
    }

    puppet_value_t *result = puppet_value_create_hash();

    for (size_t i = 0; i < args->count; i++) {
        puppet_value_t *hash_val = puppet_eval_expr(args->exprs[i], env);

        /* Skip undef values silently - common pattern in Puppet */
        if (!hash_val || hash_val->type == PUPPET_VALUE_UNDEF) {
            if (hash_val) puppet_value_destroy(hash_val);
            continue;
        }

        if (hash_val->type != PUPPET_VALUE_HASH) {
            puppet_log_loc(PUPPET_LOG_WARNING, args->exprs[i]->loc,
                "merge() skipping non-hash argument");
            puppet_value_destroy(hash_val);
            continue;
        }

        /* Copy all entries from this hash to result */
        for (size_t j = 0; j < hash_val->data.hash->bucket_count; j++) {
            puppet_hash_entry_t *entry = hash_val->data.hash->buckets[j];
            while (entry) {
                puppet_hash_set(result->data.hash, entry->key.data, entry->key.len,
                               puppet_value_copy(entry->value));
                entry = entry->next;
            }
        }

        puppet_value_destroy(hash_val);
    }

    return result;
}

/**
 * @brief Puppet is_string() function - check if value is a string
 */
puppet_value_t *puppet_func_is_string(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        return puppet_value_create_bool(false);
    }
    puppet_value_t *val = puppet_eval_expr(args->exprs[0], env);
    bool result = val && val->type == PUPPET_VALUE_STRING;
    if (val) puppet_value_destroy(val);
    return puppet_value_create_bool(result);
}

/**
 * @brief Puppet is_array() function - check if value is an array
 */
puppet_value_t *puppet_func_is_array(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        return puppet_value_create_bool(false);
    }
    puppet_value_t *val = puppet_eval_expr(args->exprs[0], env);
    bool result = val && val->type == PUPPET_VALUE_ARRAY;
    if (val) puppet_value_destroy(val);
    return puppet_value_create_bool(result);
}

/**
 * @brief Puppet is_hash() function - check if value is a hash
 */
puppet_value_t *puppet_func_is_hash(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        return puppet_value_create_bool(false);
    }
    puppet_value_t *val = puppet_eval_expr(args->exprs[0], env);
    bool result = val && val->type == PUPPET_VALUE_HASH;
    if (val) puppet_value_destroy(val);
    return puppet_value_create_bool(result);
}

/**
 * @brief Puppet is_numeric() function - check if value is a number
 */
puppet_value_t *puppet_func_is_numeric(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        return puppet_value_create_bool(false);
    }
    puppet_value_t *val = puppet_eval_expr(args->exprs[0], env);
    bool result = val && val->type == PUPPET_VALUE_NUMBER;
    if (val) puppet_value_destroy(val);
    return puppet_value_create_bool(result);
}

/**
 * @brief Puppet is_bool() function - check if value is a boolean
 */
puppet_value_t *puppet_func_is_bool(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        return puppet_value_create_bool(false);
    }
    puppet_value_t *val = puppet_eval_expr(args->exprs[0], env);
    bool result = val && val->type == PUPPET_VALUE_BOOL;
    if (val) puppet_value_destroy(val);
    return puppet_value_create_bool(result);
}

/**
 * @brief Puppet abs() function - get absolute value of a number
 *
 * Usage: abs(number)
 * Returns the absolute value of the number
 *
 * Examples:
 *   abs(-5)      => 5
 *   abs(5)       => 5
 *   abs(-3.14)   => 3.14
 */
puppet_value_t *puppet_func_abs(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "abs() requires 1 argument: number");
        return puppet_value_create_undef();
    }

    puppet_value_t *num_val = puppet_eval_expr(args->exprs[0], env);

    if (!num_val || num_val->type != PUPPET_VALUE_NUMBER) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "abs() argument must be a number");
        if (num_val) puppet_value_destroy(num_val);
        return puppet_value_create_undef();
    }

    double result = num_val->data.number;
    if (result < 0) {
        result = -result;
    }

    puppet_value_destroy(num_val);
    return puppet_value_create_number(result);
}

/**
 * @brief Puppet min() function - get minimum of values
 *
 * Usage: min(a, b, ...) or min(array)
 * Returns the smallest value
 *
 * Examples:
 *   min(3, 1, 2)     => 1
 *   min([5, 2, 8])   => 2
 *   min(-1, 1)       => -1
 */
puppet_value_t *puppet_func_min(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "min() requires at least 1 argument");
        return puppet_value_create_undef();
    }

    /* Check if first argument is an array */
    puppet_value_t *first = puppet_eval_expr(args->exprs[0], env);

    if (first && first->type == PUPPET_VALUE_ARRAY) {
        /* min(array) form */
        puppet_array_t *arr = first->data.array;
        if (arr->count == 0) {
            puppet_value_destroy(first);
            return puppet_value_create_undef();
        }

        double min_val = 0;
        bool has_min = false;

        for (size_t i = 0; i < arr->count; i++) {
            if (arr->items[i]->type == PUPPET_VALUE_NUMBER) {
                if (!has_min || arr->items[i]->data.number < min_val) {
                    min_val = arr->items[i]->data.number;
                    has_min = true;
                }
            }
        }

        puppet_value_destroy(first);
        if (!has_min) {
            return puppet_value_create_undef();
        }
        return puppet_value_create_number(min_val);
    }

    /* min(a, b, ...) form */
    if (!first || first->type != PUPPET_VALUE_NUMBER) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "min() arguments must be numbers or an array");
        if (first) puppet_value_destroy(first);
        return puppet_value_create_undef();
    }

    double min_val = first->data.number;
    puppet_value_destroy(first);

    for (size_t i = 1; i < args->count; i++) {
        puppet_value_t *val = puppet_eval_expr(args->exprs[i], env);
        if (val && val->type == PUPPET_VALUE_NUMBER) {
            if (val->data.number < min_val) {
                min_val = val->data.number;
            }
        }
        if (val) puppet_value_destroy(val);
    }

    return puppet_value_create_number(min_val);
}

/**
 * @brief Puppet max() function - get maximum of values
 *
 * Usage: max(a, b, ...) or max(array)
 * Returns the largest value
 *
 * Examples:
 *   max(3, 1, 2)     => 3
 *   max([5, 2, 8])   => 8
 *   max(-1, 1)       => 1
 */
puppet_value_t *puppet_func_max(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "max() requires at least 1 argument");
        return puppet_value_create_undef();
    }

    /* Check if first argument is an array */
    puppet_value_t *first = puppet_eval_expr(args->exprs[0], env);

    if (first && first->type == PUPPET_VALUE_ARRAY) {
        /* max(array) form */
        puppet_array_t *arr = first->data.array;
        if (arr->count == 0) {
            puppet_value_destroy(first);
            return puppet_value_create_undef();
        }

        double max_val = 0;
        bool has_max = false;

        for (size_t i = 0; i < arr->count; i++) {
            if (arr->items[i]->type == PUPPET_VALUE_NUMBER) {
                if (!has_max || arr->items[i]->data.number > max_val) {
                    max_val = arr->items[i]->data.number;
                    has_max = true;
                }
            }
        }

        puppet_value_destroy(first);
        if (!has_max) {
            return puppet_value_create_undef();
        }
        return puppet_value_create_number(max_val);
    }

    /* max(a, b, ...) form */
    if (!first || first->type != PUPPET_VALUE_NUMBER) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "max() arguments must be numbers or an array");
        if (first) puppet_value_destroy(first);
        return puppet_value_create_undef();
    }

    double max_val = first->data.number;
    puppet_value_destroy(first);

    for (size_t i = 1; i < args->count; i++) {
        puppet_value_t *val = puppet_eval_expr(args->exprs[i], env);
        if (val && val->type == PUPPET_VALUE_NUMBER) {
            if (val->data.number > max_val) {
                max_val = val->data.number;
            }
        }
        if (val) puppet_value_destroy(val);
    }

    return puppet_value_create_number(max_val);
}

/**
 * @brief Puppet floor() function - round down to nearest integer
 */
puppet_value_t *puppet_func_floor(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "floor() requires 1 argument: number");
        return puppet_value_create_undef();
    }

    puppet_value_t *num_val = puppet_eval_expr(args->exprs[0], env);

    if (!num_val || num_val->type != PUPPET_VALUE_NUMBER) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "floor() argument must be a number");
        if (num_val) puppet_value_destroy(num_val);
        return puppet_value_create_undef();
    }

    double result = floor(num_val->data.number);
    puppet_value_destroy(num_val);
    return puppet_value_create_number(result);
}

/**
 * @brief Puppet ceil() function - round up to nearest integer
 */
puppet_value_t *puppet_func_ceil(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "ceil() requires 1 argument: number");
        return puppet_value_create_undef();
    }

    puppet_value_t *num_val = puppet_eval_expr(args->exprs[0], env);

    if (!num_val || num_val->type != PUPPET_VALUE_NUMBER) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "ceil() argument must be a number");
        if (num_val) puppet_value_destroy(num_val);
        return puppet_value_create_undef();
    }

    double result = ceil(num_val->data.number);
    puppet_value_destroy(num_val);
    return puppet_value_create_number(result);
}

/**
 * @brief Puppet round() function - round to nearest integer
 */
puppet_value_t *puppet_func_round(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "round() requires 1 argument: number");
        return puppet_value_create_undef();
    }

    puppet_value_t *num_val = puppet_eval_expr(args->exprs[0], env);

    if (!num_val || num_val->type != PUPPET_VALUE_NUMBER) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "round() argument must be a number");
        if (num_val) puppet_value_destroy(num_val);
        return puppet_value_create_undef();
    }

    double result = round(num_val->data.number);
    puppet_value_destroy(num_val);
    return puppet_value_create_number(result);
}

/**
 * @brief Puppet sqrt() function - square root
 */
puppet_value_t *puppet_func_sqrt(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "sqrt() requires 1 argument: number");
        return puppet_value_create_undef();
    }

    puppet_value_t *num_val = puppet_eval_expr(args->exprs[0], env);

    if (!num_val || num_val->type != PUPPET_VALUE_NUMBER) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "sqrt() argument must be a number");
        if (num_val) puppet_value_destroy(num_val);
        return puppet_value_create_undef();
    }

    if (num_val->data.number < 0) {
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "sqrt() argument must be non-negative");
        puppet_value_destroy(num_val);
        return puppet_value_create_undef();
    }

    double result = sqrt(num_val->data.number);
    puppet_value_destroy(num_val);
    return puppet_value_create_number(result);
}

/**
 * @brief Puppet basename() function - get filename from path
 *
 * Usage: basename(path) or basename(path, suffix)
 * Returns the filename portion of a path, optionally removing suffix
 *
 * Examples:
 *   basename('/etc/passwd')           => 'passwd'
 *   basename('/etc/nginx/nginx.conf') => 'nginx.conf'
 *   basename('file.txt', '.txt')      => 'file'
 */
puppet_value_t *puppet_func_basename(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "basename() requires 1 argument: path");
        return puppet_value_create_undef();
    }

    puppet_value_t *path_val = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *suffix_val = NULL;

    if (args->count >= 2) {
        suffix_val = puppet_eval_expr(args->exprs[1], env);
    }

    if (!path_val || path_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "basename() path must be a string");
        if (path_val) puppet_value_destroy(path_val);
        if (suffix_val) puppet_value_destroy(suffix_val);
        return puppet_value_create_undef();
    }

    const char *path = path_val->data.string.data;
    size_t path_len = path_val->data.string.len;

    /* Find last slash */
    const char *base = path;
    for (size_t i = 0; i < path_len; i++) {
        if (path[i] == '/') {
            base = path + i + 1;
        }
    }

    size_t base_len = path_len - (base - path);

    /* Remove suffix if specified */
    if (suffix_val && suffix_val->type == PUPPET_VALUE_STRING) {
        size_t suffix_len = suffix_val->data.string.len;
        if (base_len > suffix_len) {
            if (memcmp(base + base_len - suffix_len,
                       suffix_val->data.string.data, suffix_len) == 0) {
                base_len -= suffix_len;
            }
        }
    }

    char *result_str = puppet_malloc(base_len + 1);
    memcpy(result_str, base, base_len);
    result_str[base_len] = '\0';

    puppet_value_t *result = puppet_value_create_string(result_str, base_len);
    puppet_free(result_str);
    puppet_value_destroy(path_val);
    if (suffix_val) puppet_value_destroy(suffix_val);

    return result;
}

/**
 * @brief Puppet dirname() function - get directory from path
 *
 * Usage: dirname(path)
 * Returns the directory portion of a path
 *
 * Examples:
 *   dirname('/etc/passwd')           => '/etc'
 *   dirname('/etc/nginx/nginx.conf') => '/etc/nginx'
 *   dirname('file.txt')              => '.'
 */
puppet_value_t *puppet_func_dirname(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log(PUPPET_LOG_ERROR, "dirname() requires 1 argument: path");
        return puppet_value_create_undef();
    }

    puppet_value_t *path_val = puppet_eval_expr(args->exprs[0], env);

    if (!path_val || path_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "dirname() path must be a string");
        if (path_val) puppet_value_destroy(path_val);
        return puppet_value_create_undef();
    }

    const char *path = path_val->data.string.data;
    size_t path_len = path_val->data.string.len;

    /* Find last slash */
    ssize_t last_slash = -1;
    for (size_t i = 0; i < path_len; i++) {
        if (path[i] == '/') {
            last_slash = (ssize_t)i;
        }
    }

    puppet_value_t *result;
    if (last_slash == -1) {
        /* No slash - return "." */
        result = puppet_value_create_string(".", 1);
    } else if (last_slash == 0) {
        /* Root directory */
        result = puppet_value_create_string("/", 1);
    } else {
        char *result_str = puppet_malloc(last_slash + 1);
        memcpy(result_str, path, last_slash);
        result_str[last_slash] = '\0';
        result = puppet_value_create_string(result_str, last_slash);
        puppet_free(result_str);
    }

    puppet_value_destroy(path_val);
    return result;
}

/**
 * @brief Puppet extname() function - get file extension
 *
 * Usage: extname(path)
 * Returns the file extension including the dot
 *
 * Examples:
 *   extname('file.txt')     => '.txt'
 *   extname('archive.tar.gz') => '.gz'
 *   extname('README')       => ''
 */
puppet_value_t *puppet_func_extname(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log(PUPPET_LOG_ERROR, "extname() requires 1 argument: path");
        return puppet_value_create_undef();
    }

    puppet_value_t *path_val = puppet_eval_expr(args->exprs[0], env);

    if (!path_val || path_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "extname() path must be a string");
        if (path_val) puppet_value_destroy(path_val);
        return puppet_value_create_undef();
    }

    const char *path = path_val->data.string.data;
    size_t path_len = path_val->data.string.len;

    /* Find basename first (after last slash) */
    const char *base = path;
    for (size_t i = 0; i < path_len; i++) {
        if (path[i] == '/') {
            base = path + i + 1;
        }
    }

    size_t base_len = path_len - (base - path);

    /* Find last dot in basename */
    ssize_t last_dot = -1;
    for (size_t i = 0; i < base_len; i++) {
        if (base[i] == '.') {
            last_dot = (ssize_t)i;
        }
    }

    puppet_value_t *result;
    if (last_dot <= 0) {
        /* No extension or hidden file (.bashrc) */
        result = puppet_value_create_string("", 0);
    } else {
        size_t ext_len = base_len - last_dot;
        char *result_str = puppet_malloc(ext_len + 1);
        memcpy(result_str, base + last_dot, ext_len);
        result_str[ext_len] = '\0';
        result = puppet_value_create_string(result_str, ext_len);
        puppet_free(result_str);
    }

    puppet_value_destroy(path_val);
    return result;
}

/**
 * @brief Puppet regsubst() function - regex substitution
 *
 * Usage: regsubst(string, pattern, replacement) or regsubst(string, pattern, replacement, flags)
 * Returns string with pattern replaced by replacement
 * Flags: 'g' for global replace, 'i' for case-insensitive
 *
 * Examples:
 *   regsubst('hello world', 'world', 'puppet')    => 'hello puppet'
 *   regsubst('foo bar foo', 'foo', 'baz', 'g')    => 'baz bar baz'
 */
puppet_value_t *puppet_func_regsubst(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 3) {
        puppet_log(PUPPET_LOG_ERROR, "regsubst() requires 3 arguments: string, pattern, replacement");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *pattern_val = puppet_eval_expr(args->exprs[1], env);
    puppet_value_t *repl_val = puppet_eval_expr(args->exprs[2], env);
    puppet_value_t *flags_val = NULL;

    if (args->count >= 4) {
        flags_val = puppet_eval_expr(args->exprs[3], env);
    }

    if (!str_val || str_val->type != PUPPET_VALUE_STRING ||
        !pattern_val || pattern_val->type != PUPPET_VALUE_STRING ||
        !repl_val || repl_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "regsubst() arguments must be strings");
        if (str_val) puppet_value_destroy(str_val);
        if (pattern_val) puppet_value_destroy(pattern_val);
        if (repl_val) puppet_value_destroy(repl_val);
        if (flags_val) puppet_value_destroy(flags_val);
        return puppet_value_create_undef();
    }

    /* Parse flags */
    int global_replace = 0;
    int cflags = REG_EXTENDED;

    if (flags_val && flags_val->type == PUPPET_VALUE_STRING) {
        for (size_t i = 0; i < flags_val->data.string.len; i++) {
            char c = flags_val->data.string.data[i];
            if (c == 'g' || c == 'G') global_replace = 1;
            if (c == 'i' || c == 'I') cflags |= REG_ICASE;
        }
    }

    /* Compile regex */
    regex_t regex;
    char *pattern_cstr = puppet_malloc(pattern_val->data.string.len + 1);
    memcpy(pattern_cstr, pattern_val->data.string.data, pattern_val->data.string.len);
    pattern_cstr[pattern_val->data.string.len] = '\0';

    int ret = regcomp(&regex, pattern_cstr, cflags);
    puppet_free(pattern_cstr);

    if (ret != 0) {
        puppet_log(PUPPET_LOG_ERROR, "regsubst() invalid regex pattern");
        puppet_value_destroy(str_val);
        puppet_value_destroy(pattern_val);
        puppet_value_destroy(repl_val);
        if (flags_val) puppet_value_destroy(flags_val);
        return puppet_value_create_undef();
    }

    /* Build result with replacements */
    const char *src = str_val->data.string.data;
    size_t src_len = str_val->data.string.len;
    const char *repl = repl_val->data.string.data;
    size_t repl_len = repl_val->data.string.len;

    /* Create null-terminated copy for regex */
    char *src_cstr = puppet_malloc(src_len + 1);
    memcpy(src_cstr, src, src_len);
    src_cstr[src_len] = '\0';

    /* Estimate result size and allocate */
    size_t result_capacity = src_len * 2 + repl_len * 10;
    char *result_str = puppet_malloc(result_capacity);
    size_t result_len = 0;

    regmatch_t match;
    size_t offset = 0;

    while (offset <= src_len) {
        ret = regexec(&regex, src_cstr + offset, 1, &match, offset > 0 ? REG_NOTBOL : 0);
        if (ret != 0) {
            /* No more matches - copy rest of string */
            size_t remaining = src_len - offset;
            if (result_len + remaining >= result_capacity) {
                result_capacity = result_len + remaining + 1;
                result_str = puppet_realloc(result_str, result_capacity);
            }
            memcpy(result_str + result_len, src + offset, remaining);
            result_len += remaining;
            break;
        }

        /* Copy text before match */
        size_t before_len = match.rm_so;
        if (result_len + before_len >= result_capacity) {
            result_capacity = (result_capacity + before_len) * 2;
            result_str = puppet_realloc(result_str, result_capacity);
        }
        memcpy(result_str + result_len, src + offset, before_len);
        result_len += before_len;

        /* Copy replacement */
        if (result_len + repl_len >= result_capacity) {
            result_capacity = (result_capacity + repl_len) * 2;
            result_str = puppet_realloc(result_str, result_capacity);
        }
        memcpy(result_str + result_len, repl, repl_len);
        result_len += repl_len;

        /* Move past match */
        offset += match.rm_eo;
        if (match.rm_eo == 0) offset++; /* Avoid infinite loop on zero-width match */

        if (!global_replace) {
            /* Copy rest of string */
            size_t remaining = src_len - offset;
            if (result_len + remaining >= result_capacity) {
                result_capacity = result_len + remaining + 1;
                result_str = puppet_realloc(result_str, result_capacity);
            }
            memcpy(result_str + result_len, src + offset, remaining);
            result_len += remaining;
            break;
        }
    }

    result_str[result_len] = '\0';

    puppet_free(src_cstr);
    regfree(&regex);

    puppet_value_t *result_final = puppet_value_create_string(result_str, result_len);
    puppet_free(result_str);

    puppet_value_destroy(str_val);
    puppet_value_destroy(pattern_val);
    puppet_value_destroy(repl_val);
    if (flags_val) puppet_value_destroy(flags_val);

    return result_final;
}

/**
 * @brief Puppet match() function - regex matching
 *
 * Usage: match(string, pattern)
 * Returns array of matches, or empty array if no match
 *
 * Examples:
 *   match('hello world', 'w.*d')     => ['world']
 *   match('foo 123 bar', '[0-9]+')   => ['123']
 *   match('no match', 'xyz')         => []
 */
puppet_value_t *puppet_func_match(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 2) {
        puppet_log(PUPPET_LOG_ERROR, "match() requires 2 arguments: string, pattern");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *pattern_val = puppet_eval_expr(args->exprs[1], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING ||
        !pattern_val || pattern_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "match() arguments must be strings");
        if (str_val) puppet_value_destroy(str_val);
        if (pattern_val) puppet_value_destroy(pattern_val);
        return puppet_value_create_undef();
    }

    /* Compile regex */
    regex_t regex;
    char *pattern_cstr = puppet_malloc(pattern_val->data.string.len + 1);
    memcpy(pattern_cstr, pattern_val->data.string.data, pattern_val->data.string.len);
    pattern_cstr[pattern_val->data.string.len] = '\0';

    int ret = regcomp(&regex, pattern_cstr, REG_EXTENDED);
    puppet_free(pattern_cstr);

    if (ret != 0) {
        puppet_log(PUPPET_LOG_ERROR, "match() invalid regex pattern");
        puppet_value_destroy(str_val);
        puppet_value_destroy(pattern_val);
        return puppet_value_create_undef();
    }

    /* Create null-terminated copy for regex */
    char *src_cstr = puppet_malloc(str_val->data.string.len + 1);
    memcpy(src_cstr, str_val->data.string.data, str_val->data.string.len);
    src_cstr[str_val->data.string.len] = '\0';

    /* Execute match - support up to 10 capture groups */
    #define MAX_MATCHES 10
    regmatch_t matches[MAX_MATCHES];

    puppet_value_t *result = puppet_value_create_array();

    ret = regexec(&regex, src_cstr, MAX_MATCHES, matches, 0);
    if (ret == 0) {
        /* Add matches to result array */
        for (int i = 0; i < MAX_MATCHES && matches[i].rm_so != -1; i++) {
            size_t match_len = matches[i].rm_eo - matches[i].rm_so;
            char *match_str = puppet_malloc(match_len + 1);
            memcpy(match_str, src_cstr + matches[i].rm_so, match_len);
            match_str[match_len] = '\0';

            puppet_array_append(result->data.array,
                puppet_value_create_string(match_str, match_len));
            puppet_free(match_str);
        }
    }

    puppet_free(src_cstr);
    regfree(&regex);

    puppet_value_destroy(str_val);
    puppet_value_destroy(pattern_val);

    return result;
}

/*
 * =============================================================================
 * Crypto Functions
 * =============================================================================
 */

/**
 * @brief Puppet sha1() function - compute SHA1 hash
 *
 * Usage: sha1(string)
 * Returns hex-encoded SHA1 hash of the input string
 *
 * Examples:
 *   sha1('hello')     => '2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c'
 *   sha1('password')  => '5baa61e4c9b93f3f0682250b6cf8331b7ee68fd8'
 */
puppet_value_t *puppet_func_sha1(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log(PUPPET_LOG_ERROR, "sha1() requires 1 argument");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);
    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "sha1() argument must be a string");
        if (str_val) puppet_value_destroy(str_val);
        return puppet_value_create_undef();
    }

    /* Use EVP API (OpenSSL 3.0+ compatible) */
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx) {
        EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
        EVP_DigestUpdate(ctx, str_val->data.string.data, str_val->data.string.len);
        EVP_DigestFinal_ex(ctx, hash, &hash_len);
        EVP_MD_CTX_free(ctx);
    }

    /* Convert to hex string */
    char hex_str[EVP_MAX_MD_SIZE * 2 + 1];
    for (unsigned int i = 0; i < hash_len; i++) {
        snprintf(hex_str + i * 2, 3, "%02x", hash[i]);
    }

    puppet_value_destroy(str_val);
    return puppet_value_create_string(hex_str, hash_len * 2);
}

/**
 * @brief Puppet md5() function - compute MD5 hash
 *
 * Usage: md5(string)
 * Returns hex-encoded MD5 hash of the input string
 *
 * Examples:
 *   md5('hello')     => '5d41402abc4b2a76b9719d911017c592'
 *   md5('password')  => '5f4dcc3b5aa765d61d8327deb882cf99'
 */
puppet_value_t *puppet_func_md5(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log(PUPPET_LOG_ERROR, "md5() requires 1 argument");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);
    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "md5() argument must be a string");
        if (str_val) puppet_value_destroy(str_val);
        return puppet_value_create_undef();
    }

    /* Use EVP API (OpenSSL 3.0+ compatible) */
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx) {
        EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
        EVP_DigestUpdate(ctx, str_val->data.string.data, str_val->data.string.len);
        EVP_DigestFinal_ex(ctx, hash, &hash_len);
        EVP_MD_CTX_free(ctx);
    }

    /* Convert to hex string */
    char hex_str[EVP_MAX_MD_SIZE * 2 + 1];
    for (unsigned int i = 0; i < hash_len; i++) {
        snprintf(hex_str + i * 2, 3, "%02x", hash[i]);
    }

    puppet_value_destroy(str_val);
    return puppet_value_create_string(hex_str, hash_len * 2);
}

/**
 * @brief Puppet base64() function - encode/decode base64
 *
 * Usage: base64(mode, string)
 *   mode: 'encode' or 'decode'
 *
 * Examples:
 *   base64('encode', 'hello')        => 'aGVsbG8='
 *   base64('decode', 'aGVsbG8=')     => 'hello'
 */
puppet_value_t *puppet_func_base64(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 2) {
        puppet_log(PUPPET_LOG_ERROR, "base64() requires 2 arguments: mode, string");
        return puppet_value_create_undef();
    }

    puppet_value_t *mode_val = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *str_val = puppet_eval_expr(args->exprs[1], env);

    if (!mode_val || mode_val->type != PUPPET_VALUE_STRING ||
        !str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "base64() arguments must be strings");
        if (mode_val) puppet_value_destroy(mode_val);
        if (str_val) puppet_value_destroy(str_val);
        return puppet_value_create_undef();
    }

    puppet_value_t *result = NULL;

    /* Check mode */
    if (strncmp(mode_val->data.string.data, "encode", mode_val->data.string.len) == 0) {
        /* Encode to base64 */
        BIO *bio, *b64;
        BUF_MEM *buffer_ptr;

        b64 = BIO_new(BIO_f_base64());
        bio = BIO_new(BIO_s_mem());
        bio = BIO_push(b64, bio);

        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
        BIO_write(bio, str_val->data.string.data, str_val->data.string.len);
        BIO_flush(bio);
        BIO_get_mem_ptr(bio, &buffer_ptr);

        result = puppet_value_create_string(buffer_ptr->data, buffer_ptr->length);
        BIO_free_all(bio);

    } else if (strncmp(mode_val->data.string.data, "decode", mode_val->data.string.len) == 0) {
        /* Decode from base64 */
        BIO *bio, *b64;

        size_t decode_len = str_val->data.string.len;
        char *decode_buf = puppet_malloc(decode_len + 1);

        bio = BIO_new_mem_buf(str_val->data.string.data, str_val->data.string.len);
        b64 = BIO_new(BIO_f_base64());
        bio = BIO_push(b64, bio);

        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
        int decoded_len = BIO_read(bio, decode_buf, decode_len);

        if (decoded_len > 0) {
            result = puppet_value_create_string(decode_buf, decoded_len);
        } else {
            puppet_log(PUPPET_LOG_ERROR, "base64() decode failed");
            result = puppet_value_create_undef();
        }

        puppet_free(decode_buf);
        BIO_free_all(bio);

    } else {
        puppet_log(PUPPET_LOG_ERROR, "base64() mode must be 'encode' or 'decode'");
        result = puppet_value_create_undef();
    }

    puppet_value_destroy(mode_val);
    puppet_value_destroy(str_val);

    return result;
}

/*
 * each($collection) |$item| { ... }
 * each($collection) |$index, $item| { ... }
 *
 * Iterates over an array or hash, executing the lambda for each element.
 * Returns the original collection.
 */
puppet_value_t *puppet_func_each(puppet_expr_t *expr, puppet_env_t *env) {
    if (!expr || expr->type != PUPPET_EXPR_FUNCALL) {
        puppet_log(PUPPET_LOG_ERROR, "each() internal error: invalid expression");
        return puppet_value_create_undef();
    }

    /* Get the lambda */
    puppet_lambda_t *lambda = expr->data.funcall.lambda;
    if (!lambda || (!lambda->body && !lambda->expr_body)) {
        puppet_log(PUPPET_LOG_ERROR, "each() requires a lambda block");
        return puppet_value_create_undef();
    }

    /* Get the collection argument */
    if (expr->data.funcall.args.count < 1) {
        puppet_log(PUPPET_LOG_ERROR, "each() requires a collection argument");
        return puppet_value_create_undef();
    }

    puppet_value_t *collection = puppet_eval_expr(expr->data.funcall.args.exprs[0], env);
    if (!collection) {
        puppet_debug("each(): collection argument evaluated to NULL");
        return puppet_value_create_undef();
    }

    /* Handle undef gracefully - treat as empty collection (zero iterations) */
    if (collection->type == PUPPET_VALUE_UNDEF) {
        puppet_value_destroy(collection);
        return puppet_value_create_undef();
    }

    /* Get lambda parameter names */
    size_t param_count = lambda->params.count;
    const char *param1_name = NULL;
    const char *param2_name = NULL;

    if (param_count >= 1 && lambda->params.params[0].name.data) {
        param1_name = lambda->params.params[0].name.data;
    }
    if (param_count >= 2 && lambda->params.params[1].name.data) {
        param2_name = lambda->params.params[1].name.data;
    }

    /* Iterate based on collection type */
    if (collection->type == PUPPET_VALUE_ARRAY) {
        puppet_array_t *arr = collection->data.array;
        for (size_t i = 0; i < arr->count; i++) {
            /* Set parameter variables */
            if (param_count == 1) {
                /* |$item| - just the value */
                if (param1_name) {
                    puppet_value_t *item_copy = puppet_value_copy(arr->items[i]);
                    puppet_env_set_var(env, param1_name, item_copy);
                }
            } else if (param_count >= 2) {
                /* |$index, $item| - index and value */
                if (param1_name) {
                    puppet_value_t *index_val = puppet_value_create_number((double)i);
                    puppet_env_set_var(env, param1_name, index_val);
                }
                if (param2_name) {
                    puppet_value_t *item_copy = puppet_value_copy(arr->items[i]);
                    puppet_env_set_var(env, param2_name, item_copy);
                }
            }

            /* Execute the lambda body */
            exec_lambda_body(lambda, env);
        }
    } else if (collection->type == PUPPET_VALUE_HASH) {
        puppet_hash_t *hash = collection->data.hash;

        /* Iterate over hash buckets */
        for (size_t b = 0; b < hash->bucket_count; b++) {
            puppet_hash_entry_t *entry = hash->buckets[b];
            while (entry) {
                /* Set parameter variables */
                if (param_count == 1) {
                    /* |$item| - key-value pair as array [key, value] */
                    if (param1_name) {
                        puppet_array_t *pair = puppet_calloc(1, sizeof(puppet_array_t));
                        pair->count = 2;
                        pair->capacity = 2;
                        pair->items = puppet_malloc(2 * sizeof(puppet_value_t*));
                        pair->items[0] = puppet_value_create_string(entry->key.data, entry->key.len);
                        pair->items[1] = puppet_value_copy(entry->value);

                        puppet_value_t *pair_val = puppet_calloc(1, sizeof(puppet_value_t));
                        pair_val->type = PUPPET_VALUE_ARRAY;
                        pair_val->data.array = pair;
                        puppet_env_set_var(env, param1_name, pair_val);
                    }
                } else if (param_count >= 2) {
                    /* |$key, $value| - key and value separately */
                    if (param1_name) {
                        puppet_value_t *key_val = puppet_value_create_string(entry->key.data, entry->key.len);
                        puppet_env_set_var(env, param1_name, key_val);
                    }
                    if (param2_name) {
                        puppet_value_t *val_copy = puppet_value_copy(entry->value);
                        puppet_env_set_var(env, param2_name, val_copy);
                    }
                }

                /* Execute the lambda body */
                exec_lambda_body(lambda, env);

                entry = entry->next;
            }
        }
    } else {
        puppet_log(PUPPET_LOG_ERROR, "each() requires an array or hash");
        puppet_value_destroy(collection);
        return puppet_value_create_undef();
    }

    /* Return the original collection */
    return collection;
}

/*
 * Helper: Execute lambda body (either statement list or expression)
 */
static void exec_lambda_body(puppet_lambda_t *lambda, puppet_env_t *env) {
    if (!lambda) return;

    if (lambda->expr_body) {
        puppet_value_t *result = puppet_eval_expr(lambda->expr_body, env);
        puppet_value_destroy(result);
    } else if (lambda->body) {
        puppet_exec_stmt_list(lambda->body, env);
    }
}

/*
 * Helper: Execute lambda body and return value of last expression
 * In Puppet, the lambda implicitly returns the last expression's value.
 */
static puppet_value_t *exec_lambda_with_result(puppet_lambda_t *lambda, puppet_env_t *env) {
    if (!lambda) {
        return puppet_value_create_undef();
    }

    /* If lambda has an expression body, evaluate and return it */
    if (lambda->expr_body) {
        return puppet_eval_expr(lambda->expr_body, env);
    }

    /* Otherwise, execute statement body */
    puppet_stmt_list_t *body = lambda->body;
    if (!body || body->count == 0) {
        return puppet_value_create_undef();
    }

    /* Execute all statements except the last */
    for (size_t i = 0; i + 1 < body->count; i++) {
        puppet_exec_stmt(body->stmts[i], env);
    }

    /* For the last statement, try to get its value */
    puppet_stmt_t *last_stmt = body->stmts[body->count - 1];
    if (!last_stmt) {
        return puppet_value_create_undef();
    }

    /* If last statement is a function call or expression, evaluate and return it */
    if (last_stmt->type == PUPPET_STMT_FUNCTION_CALL && last_stmt->data.expr) {
        return puppet_eval_expr(last_stmt->data.expr, env);
    }
    if (last_stmt->type == PUPPET_STMT_EXPRESSION && last_stmt->data.expr) {
        return puppet_eval_expr(last_stmt->data.expr, env);
    }

    /* Otherwise just execute and return undef */
    puppet_exec_stmt(last_stmt, env);
    return puppet_value_create_undef();
}

/*
 * map($collection) |$item| { expr }
 * map($collection) |$index, $item| { expr }
 *
 * Transforms each element using the lambda and returns a new array.
 */
puppet_value_t *puppet_func_map(puppet_expr_t *expr, puppet_env_t *env) {
    if (!expr || expr->type != PUPPET_EXPR_FUNCALL) {
        puppet_log(PUPPET_LOG_ERROR, "map() internal error: invalid expression");
        return puppet_value_create_undef();
    }

    puppet_lambda_t *lambda = expr->data.funcall.lambda;
    if (!lambda || (!lambda->body && !lambda->expr_body)) {
        puppet_log(PUPPET_LOG_ERROR, "map() requires a lambda block");
        return puppet_value_create_undef();
    }

    if (expr->data.funcall.args.count < 1) {
        puppet_log(PUPPET_LOG_ERROR, "map() requires a collection argument");
        return puppet_value_create_undef();
    }

    puppet_value_t *collection = puppet_eval_expr(expr->data.funcall.args.exprs[0], env);
    if (!collection) {
        return puppet_value_create_undef();
    }

    /* Handle undef gracefully - return empty array */
    if (collection->type == PUPPET_VALUE_UNDEF) {
        puppet_value_destroy(collection);
        return puppet_value_create_array();
    }

    /* Get lambda parameter names */
    size_t param_count = lambda->params.count;
    const char *param1_name = NULL;
    const char *param2_name = NULL;

    if (param_count >= 1 && lambda->params.params[0].name.data) {
        param1_name = lambda->params.params[0].name.data;
    }
    if (param_count >= 2 && lambda->params.params[1].name.data) {
        param2_name = lambda->params.params[1].name.data;
    }

    /* Create result array */
    puppet_array_t *result = puppet_calloc(1, sizeof(puppet_array_t));
    result->count = 0;
    result->capacity = 8;
    result->items = puppet_malloc(result->capacity * sizeof(puppet_value_t*));

    if (collection->type == PUPPET_VALUE_ARRAY) {
        puppet_array_t *arr = collection->data.array;
        for (size_t i = 0; i < arr->count; i++) {
            /* Set parameter variables */
            if (param_count == 1) {
                if (param1_name) {
                    puppet_value_t *item_copy = puppet_value_copy(arr->items[i]);
                    puppet_env_set_var(env, param1_name, item_copy);
                }
            } else if (param_count >= 2) {
                if (param1_name) {
                    puppet_value_t *index_val = puppet_value_create_number((double)i);
                    puppet_env_set_var(env, param1_name, index_val);
                }
                if (param2_name) {
                    puppet_value_t *item_copy = puppet_value_copy(arr->items[i]);
                    puppet_env_set_var(env, param2_name, item_copy);
                }
            }

            /* Execute lambda and collect result */
            puppet_value_t *mapped = exec_lambda_with_result(lambda, env);

            /* Add to result array */
            if (result->count >= result->capacity) {
                result->capacity *= 2;
                result->items = puppet_realloc(result->items, result->capacity * sizeof(puppet_value_t*));
            }
            result->items[result->count++] = mapped;
        }
    } else if (collection->type == PUPPET_VALUE_HASH) {
        puppet_hash_t *hash = collection->data.hash;

        for (size_t b = 0; b < hash->bucket_count; b++) {
            puppet_hash_entry_t *entry = hash->buckets[b];
            while (entry) {
                if (param_count == 1) {
                    if (param1_name) {
                        /* Create [key, value] pair */
                        puppet_array_t *pair = puppet_calloc(1, sizeof(puppet_array_t));
                        pair->count = 2;
                        pair->capacity = 2;
                        pair->items = puppet_malloc(2 * sizeof(puppet_value_t*));
                        pair->items[0] = puppet_value_create_string(entry->key.data, entry->key.len);
                        pair->items[1] = puppet_value_copy(entry->value);

                        puppet_value_t *pair_val = puppet_calloc(1, sizeof(puppet_value_t));
                        pair_val->type = PUPPET_VALUE_ARRAY;
                        pair_val->data.array = pair;
                        puppet_env_set_var(env, param1_name, pair_val);
                    }
                } else if (param_count >= 2) {
                    if (param1_name) {
                        puppet_value_t *key_val = puppet_value_create_string(entry->key.data, entry->key.len);
                        puppet_env_set_var(env, param1_name, key_val);
                    }
                    if (param2_name) {
                        puppet_value_t *val_copy = puppet_value_copy(entry->value);
                        puppet_env_set_var(env, param2_name, val_copy);
                    }
                }

                puppet_value_t *mapped = exec_lambda_with_result(lambda, env);

                if (result->count >= result->capacity) {
                    result->capacity *= 2;
                    result->items = puppet_realloc(result->items, result->capacity * sizeof(puppet_value_t*));
                }
                result->items[result->count++] = mapped;

                entry = entry->next;
            }
        }
    } else {
        puppet_log(PUPPET_LOG_ERROR, "map() requires an array or hash");
        puppet_value_destroy(collection);
        puppet_free(result->items);
        puppet_free(result);
        return puppet_value_create_undef();
    }

    puppet_value_destroy(collection);

    puppet_value_t *result_val = puppet_calloc(1, sizeof(puppet_value_t));
    result_val->type = PUPPET_VALUE_ARRAY;
    result_val->data.array = result;
    return result_val;
}

/*
 * filter($collection) |$item| { condition }
 * filter($collection) |$index, $item| { condition }
 *
 * Returns elements where the lambda returns a truthy value.
 * Also available as select() for Puppet compatibility.
 */
puppet_value_t *puppet_func_filter(puppet_expr_t *expr, puppet_env_t *env) {
    if (!expr || expr->type != PUPPET_EXPR_FUNCALL) {
        puppet_log(PUPPET_LOG_ERROR, "filter() internal error: invalid expression");
        return puppet_value_create_undef();
    }

    puppet_lambda_t *lambda = expr->data.funcall.lambda;
    if (!lambda || (!lambda->body && !lambda->expr_body)) {
        puppet_log(PUPPET_LOG_ERROR, "filter() requires a lambda block");
        return puppet_value_create_undef();
    }

    if (expr->data.funcall.args.count < 1) {
        puppet_log(PUPPET_LOG_ERROR, "filter() requires a collection argument");
        return puppet_value_create_undef();
    }

    puppet_value_t *collection = puppet_eval_expr(expr->data.funcall.args.exprs[0], env);
    if (!collection) {
        return puppet_value_create_undef();
    }

    /* Handle undef gracefully - return empty array */
    if (collection->type == PUPPET_VALUE_UNDEF) {
        puppet_value_destroy(collection);
        return puppet_value_create_array();
    }

    size_t param_count = lambda->params.count;
    const char *param1_name = NULL;
    const char *param2_name = NULL;

    if (param_count >= 1 && lambda->params.params[0].name.data) {
        param1_name = lambda->params.params[0].name.data;
    }
    if (param_count >= 2 && lambda->params.params[1].name.data) {
        param2_name = lambda->params.params[1].name.data;
    }

    /* Create result array */
    puppet_array_t *result = puppet_calloc(1, sizeof(puppet_array_t));
    result->count = 0;
    result->capacity = 8;
    result->items = puppet_malloc(result->capacity * sizeof(puppet_value_t*));

    if (collection->type == PUPPET_VALUE_ARRAY) {
        puppet_array_t *arr = collection->data.array;
        for (size_t i = 0; i < arr->count; i++) {
            if (param_count == 1) {
                if (param1_name) {
                    puppet_value_t *item_copy = puppet_value_copy(arr->items[i]);
                    puppet_env_set_var(env, param1_name, item_copy);
                }
            } else if (param_count >= 2) {
                if (param1_name) {
                    puppet_value_t *index_val = puppet_value_create_number((double)i);
                    puppet_env_set_var(env, param1_name, index_val);
                }
                if (param2_name) {
                    puppet_value_t *item_copy = puppet_value_copy(arr->items[i]);
                    puppet_env_set_var(env, param2_name, item_copy);
                }
            }

            /* Execute lambda and check if result is truthy */
            puppet_value_t *cond = exec_lambda_with_result(lambda, env);
            bool keep = puppet_value_is_truthy(cond);
            puppet_value_destroy(cond);

            if (keep) {
                if (result->count >= result->capacity) {
                    result->capacity *= 2;
                    result->items = puppet_realloc(result->items, result->capacity * sizeof(puppet_value_t*));
                }
                result->items[result->count++] = puppet_value_copy(arr->items[i]);
            }
        }
    } else if (collection->type == PUPPET_VALUE_HASH) {
        puppet_hash_t *hash = collection->data.hash;

        for (size_t b = 0; b < hash->bucket_count; b++) {
            puppet_hash_entry_t *entry = hash->buckets[b];
            while (entry) {
                if (param_count == 1) {
                    if (param1_name) {
                        puppet_array_t *pair = puppet_calloc(1, sizeof(puppet_array_t));
                        pair->count = 2;
                        pair->capacity = 2;
                        pair->items = puppet_malloc(2 * sizeof(puppet_value_t*));
                        pair->items[0] = puppet_value_create_string(entry->key.data, entry->key.len);
                        pair->items[1] = puppet_value_copy(entry->value);

                        puppet_value_t *pair_val = puppet_calloc(1, sizeof(puppet_value_t));
                        pair_val->type = PUPPET_VALUE_ARRAY;
                        pair_val->data.array = pair;
                        puppet_env_set_var(env, param1_name, pair_val);
                    }
                } else if (param_count >= 2) {
                    if (param1_name) {
                        puppet_value_t *key_val = puppet_value_create_string(entry->key.data, entry->key.len);
                        puppet_env_set_var(env, param1_name, key_val);
                    }
                    if (param2_name) {
                        puppet_value_t *val_copy = puppet_value_copy(entry->value);
                        puppet_env_set_var(env, param2_name, val_copy);
                    }
                }

                puppet_value_t *cond = exec_lambda_with_result(lambda, env);
                bool keep = puppet_value_is_truthy(cond);
                puppet_value_destroy(cond);

                if (keep) {
                    /* For hash, add [key, value] pair to result */
                    puppet_array_t *pair = puppet_calloc(1, sizeof(puppet_array_t));
                    pair->count = 2;
                    pair->capacity = 2;
                    pair->items = puppet_malloc(2 * sizeof(puppet_value_t*));
                    pair->items[0] = puppet_value_create_string(entry->key.data, entry->key.len);
                    pair->items[1] = puppet_value_copy(entry->value);

                    puppet_value_t *pair_val = puppet_calloc(1, sizeof(puppet_value_t));
                    pair_val->type = PUPPET_VALUE_ARRAY;
                    pair_val->data.array = pair;

                    if (result->count >= result->capacity) {
                        result->capacity *= 2;
                        result->items = puppet_realloc(result->items, result->capacity * sizeof(puppet_value_t*));
                    }
                    result->items[result->count++] = pair_val;
                }

                entry = entry->next;
            }
        }
    } else {
        puppet_log(PUPPET_LOG_ERROR, "filter() requires an array or hash");
        puppet_value_destroy(collection);
        puppet_free(result->items);
        puppet_free(result);
        return puppet_value_create_undef();
    }

    puppet_value_destroy(collection);

    puppet_value_t *result_val = puppet_calloc(1, sizeof(puppet_value_t));
    result_val->type = PUPPET_VALUE_ARRAY;
    result_val->data.array = result;
    return result_val;
}

/*
 * reduce(collection, initial) |$memo, $value| { ... }
 * reduce(collection, initial) |$memo, $index, $value| { ... }  (for arrays)
 * reduce(collection, initial) |$memo, $key, $value| { ... }    (for hashes)
 *
 * Reduces a collection to a single value by iteratively applying a lambda.
 * The lambda receives the accumulated value (memo) and each element.
 * Returns the final accumulated value.
 */
puppet_value_t *puppet_func_reduce(puppet_expr_t *expr, puppet_env_t *env) {
    if (!expr || expr->type != PUPPET_EXPR_FUNCALL) {
        puppet_log(PUPPET_LOG_ERROR, "reduce() internal error: invalid expression");
        return puppet_value_create_undef();
    }

    puppet_lambda_t *lambda = expr->data.funcall.lambda;
    if (!lambda || (!lambda->body && !lambda->expr_body)) {
        puppet_log(PUPPET_LOG_ERROR, "reduce() requires a lambda block");
        return puppet_value_create_undef();
    }

    /* reduce requires 2 arguments: collection and initial value */
    if (expr->data.funcall.args.count < 2) {
        puppet_log(PUPPET_LOG_ERROR, "reduce() requires a collection and an initial value");
        return puppet_value_create_undef();
    }

    puppet_value_t *collection = puppet_eval_expr(expr->data.funcall.args.exprs[0], env);
    if (!collection) {
        return puppet_value_create_undef();
    }

    /* Handle undef gracefully - return the initial value (zero iterations) */
    if (collection->type == PUPPET_VALUE_UNDEF) {
        puppet_value_destroy(collection);
        /* Evaluate and return the initial value */
        return puppet_eval_expr(expr->data.funcall.args.exprs[1], env);
    }

    puppet_value_t *memo = puppet_eval_expr(expr->data.funcall.args.exprs[1], env);
    if (!memo) {
        puppet_value_destroy(collection);
        return puppet_value_create_undef();
    }

    /* Get lambda parameter names */
    size_t param_count = lambda->params.count;
    const char *memo_name = NULL;
    const char *param2_name = NULL;
    const char *param3_name = NULL;

    if (param_count >= 1 && lambda->params.params[0].name.data) {
        memo_name = lambda->params.params[0].name.data;
    }
    if (param_count >= 2 && lambda->params.params[1].name.data) {
        param2_name = lambda->params.params[1].name.data;
    }
    if (param_count >= 3 && lambda->params.params[2].name.data) {
        param3_name = lambda->params.params[2].name.data;
    }

    if (collection->type == PUPPET_VALUE_ARRAY) {
        puppet_array_t *arr = collection->data.array;
        for (size_t i = 0; i < arr->count; i++) {
            /* Set memo variable */
            if (memo_name) {
                puppet_env_set_var(env, memo_name, memo);
            }

            if (param_count == 2) {
                /* |$memo, $value| */
                if (param2_name) {
                    puppet_value_t *item_copy = puppet_value_copy(arr->items[i]);
                    puppet_env_set_var(env, param2_name, item_copy);
                }
            } else if (param_count >= 3) {
                /* |$memo, $index, $value| */
                if (param2_name) {
                    puppet_value_t *index_val = puppet_value_create_number((double)i);
                    puppet_env_set_var(env, param2_name, index_val);
                }
                if (param3_name) {
                    puppet_value_t *item_copy = puppet_value_copy(arr->items[i]);
                    puppet_env_set_var(env, param3_name, item_copy);
                }
            }

            /* Execute lambda and get new memo value */
            memo = exec_lambda_with_result(lambda, env);
        }
    } else if (collection->type == PUPPET_VALUE_HASH) {
        puppet_hash_t *hash = collection->data.hash;

        for (size_t b = 0; b < hash->bucket_count; b++) {
            puppet_hash_entry_t *entry = hash->buckets[b];
            while (entry) {
                /* Set memo variable */
                if (memo_name) {
                    puppet_env_set_var(env, memo_name, memo);
                }

                if (param_count == 2) {
                    /* |$memo, $value| - value is [key, value] pair */
                    if (param2_name) {
                        puppet_array_t *pair = puppet_calloc(1, sizeof(puppet_array_t));
                        pair->count = 2;
                        pair->capacity = 2;
                        pair->items = puppet_malloc(2 * sizeof(puppet_value_t*));
                        pair->items[0] = puppet_value_create_string(entry->key.data, entry->key.len);
                        pair->items[1] = puppet_value_copy(entry->value);

                        puppet_value_t *pair_val = puppet_calloc(1, sizeof(puppet_value_t));
                        pair_val->type = PUPPET_VALUE_ARRAY;
                        pair_val->data.array = pair;
                        puppet_env_set_var(env, param2_name, pair_val);
                    }
                } else if (param_count >= 3) {
                    /* |$memo, $key, $value| */
                    if (param2_name) {
                        puppet_value_t *key_val = puppet_value_create_string(entry->key.data, entry->key.len);
                        puppet_env_set_var(env, param2_name, key_val);
                    }
                    if (param3_name) {
                        puppet_value_t *val_copy = puppet_value_copy(entry->value);
                        puppet_env_set_var(env, param3_name, val_copy);
                    }
                }

                /* Execute lambda and get new memo value */
                memo = exec_lambda_with_result(lambda, env);

                entry = entry->next;
            }
        }
    } else {
        puppet_log(PUPPET_LOG_ERROR, "reduce() requires an array or hash");
        puppet_value_destroy(collection);
        puppet_value_destroy(memo);
        return puppet_value_create_undef();
    }

    puppet_value_destroy(collection);
    return memo;
}

/**
 * @brief Puppet validate_re() function - validate string against regex pattern(s)
 *
 * Usage: validate_re(string, regex) or validate_re(string, [regex1, regex2, ...])
 * Raises an error if the string doesn't match any of the patterns.
 * This is a legacy stdlib function - in modern Puppet, use pattern matching instead.
 *
 * Examples:
 *   validate_re('one', '^one$')           => passes (no return)
 *   validate_re('one', ['^one', 'two'])   => passes (matches first)
 *   validate_re('foo', '^bar$')           => raises error
 */

/**
 * Get human-readable name for puppet value type
 */
static const char *puppet_value_type_name(puppet_value_type_t type) {
    switch (type) {
        case PUPPET_VALUE_UNDEF:  return "Undef";
        case PUPPET_VALUE_BOOL:   return "Boolean";
        case PUPPET_VALUE_STRING: return "String";
        case PUPPET_VALUE_NUMBER: return "Number";
        case PUPPET_VALUE_ARRAY:  return "Array";
        case PUPPET_VALUE_HASH:   return "Hash";
        case PUPPET_VALUE_REGEXP: return "Regexp";
        case PUPPET_VALUE_TYPE:   return "Type";
        default:                  return "Unknown";
    }
}

/**
 * Convert Ruby regex anchors to POSIX equivalents
 * \A -> ^ (start of string)
 * \z -> $ (end of string)
 * \Z -> $ (end of string, ignoring trailing newline - approximate)
 */
static char *convert_ruby_regex_to_posix(const char *pattern) {
    size_t len = strlen(pattern);
    char *result = puppet_malloc(len * 2 + 1);  /* Worst case: every char doubles */
    size_t j = 0;

    for (size_t i = 0; i < len; i++) {
        if (pattern[i] == '\\' && i + 1 < len) {
            if (pattern[i + 1] == 'A') {
                result[j++] = '^';
                i++;  /* Skip the 'A' */
            } else if (pattern[i + 1] == 'z' || pattern[i + 1] == 'Z') {
                result[j++] = '$';
                i++;  /* Skip the 'z' or 'Z' */
            } else {
                result[j++] = pattern[i];
            }
        } else {
            result[j++] = pattern[i];
        }
    }
    result[j] = '\0';
    return result;
}

static bool validate_re_match(const char *str, const char *pattern) {
    char *posix_pattern = convert_ruby_regex_to_posix(pattern);
    regex_t regex;
    bool matched = false;

    if (regcomp(&regex, posix_pattern, REG_EXTENDED | REG_NOSUB) == 0) {
        matched = (regexec(&regex, str, 0, NULL, 0) == 0);
        regfree(&regex);
    }
    puppet_free(posix_pattern);
    return matched;
}

puppet_value_t *puppet_func_validate_re(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 2) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "validate_re() requires at least 2 arguments");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);
    /* Handle undef gracefully - skip validation */
    if (!str_val || str_val->type == PUPPET_VALUE_UNDEF) {
        if (str_val) puppet_value_destroy(str_val);
        return puppet_value_create_bool(true);  /* Allow undef to pass validation */
    }
    if (str_val->type != PUPPET_VALUE_STRING) {
        if (str_val) puppet_value_destroy(str_val);
        puppet_log_loc(PUPPET_LOG_ERROR, args->exprs[0]->loc,
            "validate_re() first argument must be a string");
        return puppet_value_create_undef();
    }

    puppet_value_t *pattern_val = puppet_eval_expr(args->exprs[1], env);
    if (!pattern_val) {
        puppet_value_destroy(str_val);
        return puppet_value_create_undef();
    }

    const char *str = str_val->data.string.data;
    bool matched = false;
    char pattern_desc[256] = "";

    if (pattern_val->type == PUPPET_VALUE_STRING) {
        /* Single pattern */
        matched = validate_re_match(str, pattern_val->data.string.data);
        snprintf(pattern_desc, sizeof(pattern_desc), "'%s'", pattern_val->data.string.data);
    } else if (pattern_val->type == PUPPET_VALUE_ARRAY) {
        /* Array of patterns - match any */
        size_t offset = 0;
        offset += snprintf(pattern_desc + offset, sizeof(pattern_desc) - offset, "[");
        for (size_t i = 0; i < pattern_val->data.array->count && !matched; i++) {
            puppet_value_t *pat = pattern_val->data.array->items[i];
            if (pat && pat->type == PUPPET_VALUE_STRING) {
                matched = validate_re_match(str, pat->data.string.data);
                if (i > 0 && offset < sizeof(pattern_desc) - 1) {
                    offset += snprintf(pattern_desc + offset, sizeof(pattern_desc) - offset, ", ");
                }
                if (offset < sizeof(pattern_desc) - 1) {
                    offset += snprintf(pattern_desc + offset, sizeof(pattern_desc) - offset, "'%s'", pat->data.string.data);
                }
            }
        }
        if (offset < sizeof(pattern_desc) - 1) {
            snprintf(pattern_desc + offset, sizeof(pattern_desc) - offset, "]");
        }
    }

    if (!matched) {
        /* Log warning with actual value and pattern for debugging */
        puppet_log_loc(PUPPET_LOG_WARNING, get_args_location(args),
            "validate_re(): '%s' does not match %s", str, pattern_desc);
    }

    puppet_value_destroy(str_val);
    puppet_value_destroy(pattern_val);

    return puppet_value_create_bool(matched);
}

/**
 * @brief Puppet validate_hash() function - validate that values are hashes
 *
 * Usage: validate_hash(value, ...)
 * Validates that all arguments are hashes.
 * This is a legacy stdlib function - in modern Puppet, use type checking instead.
 */
puppet_value_t *puppet_func_validate_hash(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "validate_hash() requires at least 1 argument");
        return puppet_value_create_undef();
    }

    bool all_valid = true;

    /* Validate ALL arguments */
    for (size_t i = 0; i < args->count; i++) {
        puppet_value_t *val = puppet_eval_expr(args->exprs[i], env);
        bool is_hash = val && val->type == PUPPET_VALUE_HASH;

        if (!is_hash) {
            puppet_log_loc(PUPPET_LOG_WARNING, args->exprs[i]->loc,
                "validate_hash(): got %s, expected Hash",
                val ? puppet_value_type_name(val->type) : "null");
            all_valid = false;
        }

        if (val) puppet_value_destroy(val);
    }

    return puppet_value_create_bool(all_valid);
}

/**
 * @brief Puppet validate_string() function - validate that values are strings
 *
 * Usage: validate_string(value, ...)
 * Validates that all arguments are strings or undef.
 * This is a legacy stdlib function - allows undef as valid.
 */
puppet_value_t *puppet_func_validate_string(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "validate_string() requires at least 1 argument");
        return puppet_value_create_undef();
    }

    bool all_valid = true;

    /* Validate ALL arguments */
    for (size_t i = 0; i < args->count; i++) {
        puppet_value_t *val = puppet_eval_expr(args->exprs[i], env);
        /* undef is allowed, only non-string non-undef values fail */
        bool is_valid = !val || val->type == PUPPET_VALUE_UNDEF || val->type == PUPPET_VALUE_STRING;

        if (!is_valid) {
            puppet_log_loc(PUPPET_LOG_WARNING, args->exprs[i]->loc,
                "validate_string(): got %s, expected String",
                val ? puppet_value_type_name(val->type) : "null");
            all_valid = false;
        }

        if (val) puppet_value_destroy(val);
    }

    return puppet_value_create_bool(all_valid);
}

/**
 * @brief Puppet validate_array() function - validate that values are arrays
 *
 * Usage: validate_array(value, ...)
 * Validates that all arguments are arrays.
 * This is a legacy stdlib function.
 */
puppet_value_t *puppet_func_validate_array(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "validate_array() requires at least 1 argument");
        return puppet_value_create_undef();
    }

    bool all_valid = true;

    /* Validate ALL arguments */
    for (size_t i = 0; i < args->count; i++) {
        puppet_value_t *val = puppet_eval_expr(args->exprs[i], env);
        bool is_array = val && val->type == PUPPET_VALUE_ARRAY;

        if (!is_array) {
            puppet_log_loc(PUPPET_LOG_WARNING, args->exprs[i]->loc,
                "validate_array(): got %s, expected Array",
                val ? puppet_value_type_name(val->type) : "null");
            all_valid = false;
        }

        if (val) puppet_value_destroy(val);
    }

    return puppet_value_create_bool(all_valid);
}

/**
 * @brief Puppet validate_bool() function - validate that values are booleans
 *
 * Usage: validate_bool(value, ...)
 * Validates that all arguments are booleans.
 * This is a legacy stdlib function.
 */
puppet_value_t *puppet_func_validate_bool(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "validate_bool() requires at least 1 argument");
        return puppet_value_create_undef();
    }

    bool all_valid = true;

    /* Validate ALL arguments */
    for (size_t i = 0; i < args->count; i++) {
        puppet_value_t *val = puppet_eval_expr(args->exprs[i], env);
        bool is_bool = val && val->type == PUPPET_VALUE_BOOL;

        if (!is_bool) {
            if (val && val->type == PUPPET_VALUE_STRING) {
                puppet_log_loc(PUPPET_LOG_WARNING, args->exprs[i]->loc,
                    "validate_bool(): got String '%s', expected Boolean",
                    val->data.string.data);
            } else {
                puppet_log_loc(PUPPET_LOG_WARNING, args->exprs[i]->loc,
                    "validate_bool(): got %s, expected Boolean",
                    val ? puppet_value_type_name(val->type) : "null");
            }
            all_valid = false;
        }

        if (val) puppet_value_destroy(val);
    }

    return puppet_value_create_bool(all_valid);
}

/**
 * @brief Compare version string components
 *
 * Helper function for versioncmp that compares alphanumeric segments.
 */
static int version_segment_cmp(const char *a, const char *b) {
    while (*a && *b) {
        /* Skip non-alphanumeric characters */
        while (*a && !isalnum((unsigned char)*a)) a++;
        while (*b && !isalnum((unsigned char)*b)) b++;

        if (!*a || !*b) break;

        /* Compare segments */
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            /* Numeric comparison */
            long na = strtol(a, (char**)&a, 10);
            long nb = strtol(b, (char**)&b, 10);
            if (na != nb) return (na > nb) ? 1 : -1;
        } else if (isdigit((unsigned char)*a)) {
            return 1;  /* Numbers come after letters */
        } else if (isdigit((unsigned char)*b)) {
            return -1;
        } else {
            /* Alphabetic comparison */
            while (*a && *b && isalpha((unsigned char)*a) && isalpha((unsigned char)*b)) {
                if (*a != *b) return (*a > *b) ? 1 : -1;
                a++;
                b++;
            }
            if (isalpha((unsigned char)*a)) return 1;
            if (isalpha((unsigned char)*b)) return -1;
        }
    }

    /* Skip trailing non-alphanumeric */
    while (*a && !isalnum((unsigned char)*a)) a++;
    while (*b && !isalnum((unsigned char)*b)) b++;

    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

/**
 * @brief Puppet versioncmp() function - compare version strings
 *
 * Usage: versioncmp(version_a, version_b)
 * Returns:
 *   -1 if version_a < version_b
 *    0 if version_a == version_b
 *    1 if version_a > version_b
 *
 * Examples:
 *   versioncmp('1.0', '2.0')      => -1
 *   versioncmp('2.0', '1.0')      => 1
 *   versioncmp('1.0', '1.0')      => 0
 *   versioncmp('1.0.0', '1.0')    => 1
 *   versioncmp('1.0a', '1.0b')    => -1
 */
puppet_value_t *puppet_func_versioncmp(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 2) {
        puppet_log(PUPPET_LOG_ERROR, "versioncmp() requires 2 arguments");
        return puppet_value_create_undef();
    }

    puppet_value_t *a_val = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *b_val = puppet_eval_expr(args->exprs[1], env);

    /* Handle undef values gracefully - return 0 (equal) if either is undef */
    if (!a_val || a_val->type == PUPPET_VALUE_UNDEF ||
        !b_val || b_val->type == PUPPET_VALUE_UNDEF) {
        if (a_val) puppet_value_destroy(a_val);
        if (b_val) puppet_value_destroy(b_val);
        puppet_log(PUPPET_LOG_WARNING, "versioncmp() received undef argument, returning 0");
        return puppet_value_create_number(0);
    }

    /* Convert numbers to strings for version comparison */
    char a_buf[64] = {0}, b_buf[64] = {0};
    const char *a_str = NULL, *b_str = NULL;

    if (a_val->type == PUPPET_VALUE_STRING) {
        a_str = a_val->data.string.data;
    } else if (a_val->type == PUPPET_VALUE_NUMBER) {
        snprintf(a_buf, sizeof(a_buf), "%g", a_val->data.number);
        a_str = a_buf;
    }

    if (b_val->type == PUPPET_VALUE_STRING) {
        b_str = b_val->data.string.data;
    } else if (b_val->type == PUPPET_VALUE_NUMBER) {
        snprintf(b_buf, sizeof(b_buf), "%g", b_val->data.number);
        b_str = b_buf;
    }

    if (!a_str || !b_str) {
        if (a_val) puppet_value_destroy(a_val);
        if (b_val) puppet_value_destroy(b_val);
        puppet_log(PUPPET_LOG_ERROR, "versioncmp() arguments must be strings");
        return puppet_value_create_undef();
    }

    int result = version_segment_cmp(a_str, b_str);

    puppet_value_destroy(a_val);
    puppet_value_destroy(b_val);

    return puppet_value_create_number((double)result);
}

/**
 * @brief Puppet is_domain_name() function - check if string is a valid domain name
 *
 * Usage: is_domain_name(string)
 * Returns true if the string is a valid domain name.
 *
 * Examples:
 *   is_domain_name('example.com')      => true
 *   is_domain_name('foo.bar.baz')      => true
 *   is_domain_name('not valid!')       => false
 *   is_domain_name('-invalid.com')     => false
 */
puppet_value_t *puppet_func_is_domain_name(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        return puppet_value_create_bool(false);
    }

    puppet_value_t *val = puppet_eval_expr(args->exprs[0], env);
    if (!val || val->type != PUPPET_VALUE_STRING) {
        if (val) puppet_value_destroy(val);
        return puppet_value_create_bool(false);
    }

    const char *domain = val->data.string.data;
    size_t len = strlen(domain);
    bool valid = true;

    /* Basic validation */
    if (len == 0 || len > 253) {
        valid = false;
    } else {
        /* Check each label */
        const char *p = domain;
        while (*p && valid) {
            const char *label_start = p;
            size_t label_len = 0;

            /* Find end of label */
            while (*p && *p != '.') {
                char c = *p;
                /* Valid characters: a-z, A-Z, 0-9, hyphen */
                if (!isalnum((unsigned char)c) && c != '-') {
                    valid = false;
                    break;
                }
                p++;
                label_len++;
            }

            /* Label validation */
            if (valid) {
                if (label_len == 0 || label_len > 63) {
                    valid = false;
                } else if (label_start[0] == '-' || label_start[label_len - 1] == '-') {
                    /* Labels can't start or end with hyphen */
                    valid = false;
                }
            }

            if (*p == '.') p++;  /* Skip dot */
        }
    }

    puppet_value_destroy(val);
    return puppet_value_create_bool(valid);
}

/**
 * @brief Puppet is_ip_address() function - check if string is a valid IP address
 *
 * Usage: is_ip_address(string)
 * Returns true if the string is a valid IPv4 or IPv6 address.
 */
puppet_value_t *puppet_func_is_ip_address(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        return puppet_value_create_bool(false);
    }

    puppet_value_t *val = puppet_eval_expr(args->exprs[0], env);
    if (!val || val->type != PUPPET_VALUE_STRING) {
        if (val) puppet_value_destroy(val);
        return puppet_value_create_bool(false);
    }

    const char *addr = val->data.string.data;
    bool valid = false;

    /* Try IPv4 */
    int octets[4];
    int n = sscanf(addr, "%d.%d.%d.%d", &octets[0], &octets[1], &octets[2], &octets[3]);
    if (n == 4) {
        valid = true;
        for (int i = 0; i < 4; i++) {
            if (octets[i] < 0 || octets[i] > 255) {
                valid = false;
                break;
            }
        }
        /* Check for trailing garbage */
        if (valid) {
            char check[32];
            snprintf(check, sizeof(check), "%d.%d.%d.%d", octets[0], octets[1], octets[2], octets[3]);
            if (strcmp(check, addr) != 0) {
                valid = false;
            }
        }
    }

    /* Try IPv6 (basic check - contains colons and hex digits) */
    if (!valid && strchr(addr, ':')) {
        valid = true;
        for (const char *p = addr; *p && valid; p++) {
            if (!isxdigit((unsigned char)*p) && *p != ':') {
                valid = false;
            }
        }
    }

    puppet_value_destroy(val);
    return puppet_value_create_bool(valid);
}

/**
 * @brief Puppet create_resources() function - create resources from a hash
 *
 * Usage: create_resources(type, hash, [defaults])
 * Creates resources of the given type from a hash of title => parameters.
 *
 * Example:
 *   $users = {
 *     'alice' => { uid => 1001, gid => 1001 },
 *     'bob'   => { uid => 1002, gid => 1002 },
 *   }
 *   create_resources('user', $users)
 */
puppet_value_t *puppet_func_create_resources(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 2) {
        puppet_log(PUPPET_LOG_ERROR, "create_resources() requires at least 2 arguments");
        return puppet_value_create_undef();
    }

    puppet_value_t *type_val = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *hash_val = puppet_eval_expr(args->exprs[1], env);
    puppet_value_t *defaults_val = NULL;

    if (args->count >= 3) {
        defaults_val = puppet_eval_expr(args->exprs[2], env);
    }

    if (!type_val || type_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "create_resources() first argument must be a string (resource type)");
        if (type_val) puppet_value_destroy(type_val);
        if (hash_val) puppet_value_destroy(hash_val);
        if (defaults_val) puppet_value_destroy(defaults_val);
        return puppet_value_create_undef();
    }

    if (!hash_val || hash_val->type != PUPPET_VALUE_HASH) {
        puppet_log(PUPPET_LOG_ERROR, "create_resources() second argument must be a hash");
        puppet_value_destroy(type_val);
        if (hash_val) puppet_value_destroy(hash_val);
        if (defaults_val) puppet_value_destroy(defaults_val);
        return puppet_value_create_undef();
    }

    const char *res_type = type_val->data.string.data;
    puppet_hash_t *hash = hash_val->data.hash;
    puppet_hash_t *defaults_hash = (defaults_val && defaults_val->type == PUPPET_VALUE_HASH)
                                   ? defaults_val->data.hash : NULL;

    /* Look up the defined type */
    puppet_value_t *define_ptr = puppet_hash_get(env->define_types, res_type, strlen(res_type));

    /* Try to autoload the define if not already registered */
    if (!define_ptr && env->loader && strchr(res_type, ':')) {
        puppet_stmt_t *loaded_def = puppet_loader_load_define(env->loader, res_type);
        if (loaded_def) {
            puppet_debug("create_resources: Autoloaded defined type: %s", res_type);
            puppet_value_t *stmt_ptr = puppet_calloc(1, sizeof(puppet_value_t));
            stmt_ptr->type = PUPPET_VALUE_UNDEF;
            stmt_ptr->data.string.data = (char*)loaded_def;
            puppet_hash_set(env->define_types, res_type, strlen(res_type), stmt_ptr);
            define_ptr = stmt_ptr;
        }
    }

    puppet_stmt_t *define_stmt = define_ptr ? (puppet_stmt_t *)define_ptr->data.string.data : NULL;

    /* Iterate over hash entries to create resources */
    for (size_t i = 0; i < hash->bucket_count; i++) {
        puppet_hash_entry_t *entry = hash->buckets[i];
        while (entry) {
            const char *title_str = entry->key.data;
            puppet_value_t *params_val = entry->value;
            puppet_hash_t *params_hash = (params_val && params_val->type == PUPPET_VALUE_HASH)
                                        ? params_val->data.hash : NULL;

            /* Check for duplicate resource */
            size_t res_id_len = strlen(res_type) + strlen(title_str) + 3;
            char *resource_id = puppet_malloc(res_id_len);
            snprintf(resource_id, res_id_len, "%s[%s]", res_type, title_str);

            puppet_value_t *existing = puppet_hash_get(env->resource_catalog, resource_id, strlen(resource_id));
            if (existing) {
                fprintf(stderr, "Error: Duplicate declaration - %s is already declared\n", resource_id);
                puppet_free(resource_id);
                entry = entry->next;
                continue;
            }

            /* Mark as declared */
            puppet_value_t *marker = puppet_value_create_bool(true);
            puppet_hash_set(env->resource_catalog, resource_id, strlen(resource_id), marker);

            if (define_stmt) {
                /* This is a defined type - execute its body */
                puppet_debug("create_resources: Executing defined type %s[%s]", res_type, title_str);

                /* Create new scope for the define execution */
                puppet_scope_t *define_scope = puppet_scope_create(env->current_scope, res_type);
                puppet_scope_push(env, define_scope);

                /* Bind $name and $title to the title */
                puppet_value_t *name_val = puppet_value_create_string(title_str, strlen(title_str));
                puppet_scope_set_var(define_scope, "name", name_val);
                puppet_scope_set_var(define_scope, "title", puppet_value_copy(name_val));

                /* Bind define parameters with values from hash or defaults */
                for (size_t p = 0; p < define_stmt->data.define.params.count; p++) {
                    const char *param_name = define_stmt->data.define.params.params[p].name.data;
                    puppet_value_t *param_value = NULL;

                    /* First, check if parameter is in the entry's params hash */
                    if (params_hash) {
                        param_value = puppet_hash_get(params_hash, param_name, strlen(param_name));
                        if (param_value) {
                            param_value = puppet_value_copy(param_value);
                        }
                    }

                    /* If not found, check defaults hash */
                    if (!param_value && defaults_hash) {
                        param_value = puppet_hash_get(defaults_hash, param_name, strlen(param_name));
                        if (param_value) {
                            param_value = puppet_value_copy(param_value);
                        }
                    }

                    /* If still not found, use define's default value */
                    if (!param_value && define_stmt->data.define.params.params[p].default_value) {
                        param_value = puppet_eval_expr(
                            define_stmt->data.define.params.params[p].default_value, env);
                    }

                    if (param_value) {
                        puppet_scope_set_var(define_scope, param_name, param_value);
                    }
                }

                /* Execute the define body */
                puppet_exec_stmt_list(&define_stmt->data.define.body, env);

                /* Add the define instance to catalog */
                if (env->build_catalog && env->catalog) {
                    /* Collect parameters for catalog */
                    size_t param_count = 0;
                    puppet_catalog_param_t *cat_params = NULL;

                    /* Count params from entry hash */
                    if (params_hash) {
                        for (size_t bi = 0; bi < params_hash->bucket_count; bi++) {
                            puppet_hash_entry_t *pe = params_hash->buckets[bi];
                            while (pe) {
                                param_count++;
                                pe = pe->next;
                            }
                        }
                    }

                    if (param_count > 0) {
                        cat_params = puppet_calloc(param_count, sizeof(puppet_catalog_param_t));
                        size_t param_idx = 0;
                        for (size_t bi = 0; bi < params_hash->bucket_count; bi++) {
                            puppet_hash_entry_t *pe = params_hash->buckets[bi];
                            while (pe) {
                                cat_params[param_idx].name = puppet_strdup(pe->key.data);
                                cat_params[param_idx].value = puppet_value_copy(pe->value);
                                param_idx++;
                                pe = pe->next;
                            }
                        }
                    }

                    puppet_catalog_add_resource(env->catalog, res_type, title_str, cat_params, param_count,
                                                NULL, 0);  /* create_resources: no source location */
                }

                /* Pop the define scope */
                puppet_scope_t *popped = puppet_scope_pop(env);
                puppet_scope_destroy(popped);
            } else {
                /* Built-in resource type - add directly to catalog */
                puppet_debug("create_resources: Creating resource %s[%s]", res_type, title_str);

                if (env->build_catalog && env->catalog) {
                    /* Merge defaults with params */
                    size_t param_count = 0;
                    puppet_catalog_param_t *cat_params = NULL;

                    /* Count parameters */
                    if (defaults_hash) {
                        for (size_t bi = 0; bi < defaults_hash->bucket_count; bi++) {
                            puppet_hash_entry_t *pe = defaults_hash->buckets[bi];
                            while (pe) { param_count++; pe = pe->next; }
                        }
                    }
                    if (params_hash) {
                        for (size_t bi = 0; bi < params_hash->bucket_count; bi++) {
                            puppet_hash_entry_t *pe = params_hash->buckets[bi];
                            while (pe) { param_count++; pe = pe->next; }
                        }
                    }

                    if (param_count > 0) {
                        cat_params = puppet_calloc(param_count, sizeof(puppet_catalog_param_t));
                        size_t param_idx = 0;

                        /* Add defaults first */
                        if (defaults_hash) {
                            for (size_t bi = 0; bi < defaults_hash->bucket_count; bi++) {
                                puppet_hash_entry_t *pe = defaults_hash->buckets[bi];
                                while (pe) {
                                    cat_params[param_idx].name = puppet_strdup(pe->key.data);
                                    cat_params[param_idx].value = puppet_value_copy(pe->value);
                                    param_idx++;
                                    pe = pe->next;
                                }
                            }
                        }

                        /* Override with entry params */
                        if (params_hash) {
                            for (size_t bi = 0; bi < params_hash->bucket_count; bi++) {
                                puppet_hash_entry_t *pe = params_hash->buckets[bi];
                                while (pe) {
                                    /* Check if already in cat_params from defaults - override it */
                                    bool found = false;
                                    for (size_t k = 0; k < param_idx; k++) {
                                        if (strcmp(cat_params[k].name, pe->key.data) == 0) {
                                            puppet_value_destroy(cat_params[k].value);
                                            cat_params[k].value = puppet_value_copy(pe->value);
                                            found = true;
                                            break;
                                        }
                                    }
                                    if (!found) {
                                        cat_params[param_idx].name = puppet_strdup(pe->key.data);
                                        cat_params[param_idx].value = puppet_value_copy(pe->value);
                                        param_idx++;
                                    }
                                    pe = pe->next;
                                }
                            }
                        }

                        puppet_catalog_add_resource(env->catalog, res_type, title_str, cat_params, param_idx,
                                                    NULL, 0);  /* create_resources: no source location */
                    } else {
                        puppet_catalog_add_resource(env->catalog, res_type, title_str, NULL, 0,
                                                    NULL, 0);  /* create_resources: no source location */
                    }
                }
            }

            puppet_free(resource_id);
            entry = entry->next;
        }
    }

    puppet_value_destroy(type_val);
    puppet_value_destroy(hash_val);
    if (defaults_val) puppet_value_destroy(defaults_val);

    return puppet_value_create_undef();
}

/**
 * @brief Puppet ensure_packages() function - ensure packages are installed
 *
 * Usage: ensure_packages(packages, [options])
 * Ensures packages are installed without duplicates.
 *
 * Note: This is a stub - logs what would be ensured.
 */
puppet_value_t *puppet_func_ensure_packages(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log(PUPPET_LOG_ERROR, "ensure_packages() requires at least 1 argument");
        return puppet_value_create_undef();
    }

    puppet_value_t *pkgs = puppet_eval_expr(args->exprs[0], env);
    if (!pkgs) {
        return puppet_value_create_undef();
    }

    /* Just log for now - full implementation would create package resources */
    if (pkgs->type == PUPPET_VALUE_ARRAY) {
        for (size_t i = 0; i < pkgs->data.array->count; i++) {
            puppet_value_t *pkg = pkgs->data.array->items[i];
            if (pkg && pkg->type == PUPPET_VALUE_STRING) {
                puppet_log(PUPPET_LOG_NOTICE, "ensure_packages: Would ensure package is installed");
            }
        }
    } else if (pkgs->type == PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_NOTICE, "ensure_packages: Would ensure package is installed");
    }

    puppet_value_destroy(pkgs);
    return puppet_value_create_undef();
}

/**
 * @brief Puppet any2array() function - convert any value to array
 *
 * Usage: any2array(value)
 * Converts any value to an array. Arrays pass through unchanged,
 * hashes become array of [key, value] pairs, scalars become single-element arrays.
 */
puppet_value_t *puppet_func_any2array(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        /* Return empty array */
        puppet_array_t *arr = puppet_calloc(1, sizeof(puppet_array_t));
        arr->count = 0;
        arr->capacity = 4;
        arr->items = puppet_calloc(arr->capacity, sizeof(puppet_value_t*));
        puppet_value_t *result = puppet_calloc(1, sizeof(puppet_value_t));
        result->type = PUPPET_VALUE_ARRAY;
        result->data.array = arr;
        return result;
    }

    puppet_value_t *val = puppet_eval_expr(args->exprs[0], env);
    if (!val) {
        return puppet_value_create_undef();
    }

    if (val->type == PUPPET_VALUE_ARRAY) {
        /* Already an array - return copy */
        return val;
    }

    if (val->type == PUPPET_VALUE_UNDEF) {
        puppet_value_destroy(val);
        /* Return empty array */
        puppet_array_t *arr = puppet_calloc(1, sizeof(puppet_array_t));
        arr->count = 0;
        arr->capacity = 4;
        arr->items = puppet_calloc(arr->capacity, sizeof(puppet_value_t*));
        puppet_value_t *result = puppet_calloc(1, sizeof(puppet_value_t));
        result->type = PUPPET_VALUE_ARRAY;
        result->data.array = arr;
        return result;
    }

    /* Wrap scalar in array */
    puppet_array_t *arr = puppet_calloc(1, sizeof(puppet_array_t));
    arr->count = 1;
    arr->capacity = 4;
    arr->items = puppet_calloc(arr->capacity, sizeof(puppet_value_t*));
    arr->items[0] = val;

    puppet_value_t *result = puppet_calloc(1, sizeof(puppet_value_t));
    result->type = PUPPET_VALUE_ARRAY;
    result->data.array = arr;
    return result;
}

/**
 * @brief Puppet str2bool() function - convert string to boolean
 *
 * Usage: str2bool(string)
 * Converts 'true', 'yes', '1' to true; 'false', 'no', '0' to false.
 */
puppet_value_t *puppet_func_str2bool(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log(PUPPET_LOG_ERROR, "str2bool() requires 1 argument");
        return puppet_value_create_undef();
    }

    puppet_value_t *val = puppet_eval_expr(args->exprs[0], env);
    if (!val) {
        return puppet_value_create_bool(false);
    }

    if (val->type == PUPPET_VALUE_BOOL) {
        return val;
    }

    if (val->type != PUPPET_VALUE_STRING) {
        puppet_value_destroy(val);
        return puppet_value_create_bool(false);
    }

    const char *str = val->data.string.data;
    bool result = false;

    if (strcasecmp(str, "true") == 0 || strcasecmp(str, "yes") == 0 ||
        strcasecmp(str, "1") == 0 || strcasecmp(str, "y") == 0) {
        result = true;
    }

    puppet_value_destroy(val);
    return puppet_value_create_bool(result);
}

/**
 * @brief Puppet bool2str() function - convert boolean to string
 *
 * Usage: bool2str(boolean) or bool2str(boolean, true_string, false_string)
 */
puppet_value_t *puppet_func_bool2str(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log(PUPPET_LOG_ERROR, "bool2str() requires at least 1 argument");
        return puppet_value_create_undef();
    }

    puppet_value_t *val = puppet_eval_expr(args->exprs[0], env);
    if (!val || val->type != PUPPET_VALUE_BOOL) {
        if (val) puppet_value_destroy(val);
        puppet_log(PUPPET_LOG_ERROR, "bool2str() first argument must be a boolean");
        return puppet_value_create_undef();
    }

    bool b = val->data.boolean;
    puppet_value_destroy(val);

    if (args->count >= 3) {
        /* Custom true/false strings */
        puppet_value_t *true_str = puppet_eval_expr(args->exprs[1], env);
        puppet_value_t *false_str = puppet_eval_expr(args->exprs[2], env);

        puppet_value_t *result;
        if (b && true_str && true_str->type == PUPPET_VALUE_STRING) {
            result = puppet_value_create_string(true_str->data.string.data,
                                                 true_str->data.string.len);
        } else if (!b && false_str && false_str->type == PUPPET_VALUE_STRING) {
            result = puppet_value_create_string(false_str->data.string.data,
                                                 false_str->data.string.len);
        } else {
            result = puppet_value_create_string(b ? "true" : "false", b ? 4 : 5);
        }

        if (true_str) puppet_value_destroy(true_str);
        if (false_str) puppet_value_destroy(false_str);
        return result;
    }

    return puppet_value_create_string(b ? "true" : "false", b ? 4 : 5);
}

/**
 * @brief Puppet type() function - return the type of a value as a string
 *
 * Usage: type(value)
 * Returns 'String', 'Integer', 'Float', 'Boolean', 'Array', 'Hash', or 'Undef'
 */
puppet_value_t *puppet_func_type(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        return puppet_value_create_string("Undef", 5);
    }

    puppet_value_t *val = puppet_eval_expr(args->exprs[0], env);
    const char *type_str = "Undef";

    if (val) {
        switch (val->type) {
            case PUPPET_VALUE_STRING:
                type_str = "String";
                break;
            case PUPPET_VALUE_NUMBER:
                if (val->data.number == (long)val->data.number) {
                    type_str = "Integer";
                } else {
                    type_str = "Float";
                }
                break;
            case PUPPET_VALUE_BOOL:
                type_str = "Boolean";
                break;
            case PUPPET_VALUE_ARRAY:
                type_str = "Array";
                break;
            case PUPPET_VALUE_HASH:
                type_str = "Hash";
                break;
            case PUPPET_VALUE_UNDEF:
            default:
                type_str = "Undef";
                break;
        }
        puppet_value_destroy(val);
    }

    return puppet_value_create_string(type_str, strlen(type_str));
}

/**
 * @brief Simple hash function for strings
 */
static unsigned long string_hash(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

/**
 * @brief Puppet fqdn_rand() function - generate a random number based on FQDN
 *
 * Usage: fqdn_rand(max) or fqdn_rand(max, seed)
 * Returns a random number from 0 to max-1 that is consistent for a given FQDN.
 * The optional seed string is appended to the FQDN for the hash.
 *
 * Examples:
 *   fqdn_rand(30)           => consistent number 0-29 based on $fqdn
 *   fqdn_rand(30, 'myseed') => different consistent number using seed
 */
puppet_value_t *puppet_func_fqdn_rand(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log(PUPPET_LOG_ERROR, "fqdn_rand() requires at least 1 argument");
        return puppet_value_create_undef();
    }

    puppet_value_t *max_val = puppet_eval_expr(args->exprs[0], env);
    if (!max_val || max_val->type != PUPPET_VALUE_NUMBER) {
        if (max_val) puppet_value_destroy(max_val);
        puppet_log(PUPPET_LOG_ERROR, "fqdn_rand() first argument must be a number");
        return puppet_value_create_undef();
    }

    long max = (long)max_val->data.number;
    puppet_value_destroy(max_val);

    if (max <= 0) {
        return puppet_value_create_number(0);
    }

    /* Get FQDN from environment */
    char hash_input[512] = "";
    puppet_value_t *fqdn = puppet_env_get_var(env, "fqdn");
    if (fqdn && fqdn->type == PUPPET_VALUE_STRING) {
        strncpy(hash_input, fqdn->data.string.data, sizeof(hash_input) - 1);
    } else {
        /* Fallback to hostname */
        puppet_value_t *hostname = puppet_env_get_var(env, "hostname");
        if (hostname && hostname->type == PUPPET_VALUE_STRING) {
            strncpy(hash_input, hostname->data.string.data, sizeof(hash_input) - 1);
        } else {
            strcpy(hash_input, "localhost");
        }
    }

    /* Append optional seed */
    if (args->count >= 2) {
        puppet_value_t *seed_val = puppet_eval_expr(args->exprs[1], env);
        if (seed_val && seed_val->type == PUPPET_VALUE_STRING) {
            size_t len = strlen(hash_input);
            if (len < sizeof(hash_input) - 1) {
                strncat(hash_input, seed_val->data.string.data, sizeof(hash_input) - len - 1);
            }
        }
        if (seed_val) puppet_value_destroy(seed_val);
    }

    /* Generate consistent random number */
    unsigned long hash = string_hash(hash_input);
    long result = (long)(hash % (unsigned long)max);

    return puppet_value_create_number((double)result);
}

/**
 * @brief Puppet assert_type() function - assert that a value has a specific type
 *
 * Usage: assert_type(type, value)
 * Returns the value if it matches the type, otherwise raises an error.
 * This is a simplified version that just returns the value.
 */
puppet_value_t *puppet_func_assert_type(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 2) {
        puppet_log(PUPPET_LOG_ERROR, "assert_type() requires 2 arguments");
        return puppet_value_create_undef();
    }

    /* For now, just return the value - type checking is a TODO */
    puppet_value_t *type_arg = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *value = puppet_eval_expr(args->exprs[1], env);

    if (type_arg) puppet_value_destroy(type_arg);

    return value ? value : puppet_value_create_undef();
}

/**
 * @brief Puppet dig() function - safely access nested data structures
 *
 * Usage: dig(data, key1, key2, ...)
 * Returns the value at the nested path, or undef if any key doesn't exist.
 */
puppet_value_t *puppet_func_dig(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 2) {
        puppet_log(PUPPET_LOG_ERROR, "dig() requires at least 2 arguments");
        return puppet_value_create_undef();
    }

    puppet_value_t *current = puppet_eval_expr(args->exprs[0], env);
    if (!current) {
        return puppet_value_create_undef();
    }

    for (size_t i = 1; i < args->count; i++) {
        puppet_value_t *key = puppet_eval_expr(args->exprs[i], env);
        if (!key) {
            puppet_value_destroy(current);
            return puppet_value_create_undef();
        }

        puppet_value_t *next = NULL;

        if (current->type == PUPPET_VALUE_HASH && key->type == PUPPET_VALUE_STRING) {
            next = puppet_hash_get(current->data.hash, key->data.string.data, key->data.string.len);
            if (next) {
                next = puppet_value_copy(next);
            }
        } else if (current->type == PUPPET_VALUE_ARRAY && key->type == PUPPET_VALUE_NUMBER) {
            size_t idx = (size_t)key->data.number;
            if (idx < current->data.array->count) {
                next = puppet_value_copy(current->data.array->items[idx]);
            }
        }

        puppet_value_destroy(key);
        puppet_value_destroy(current);

        if (!next) {
            return puppet_value_create_undef();
        }
        current = next;
    }

    return current;
}

/**
 * @brief Puppet pick() function - return first non-undef value
 *
 * Usage: pick(value1, value2, ...)
 * Returns the first value that is not undef.
 */
puppet_value_t *puppet_func_pick(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log(PUPPET_LOG_ERROR, "pick() requires at least 1 argument");
        return puppet_value_create_undef();
    }

    for (size_t i = 0; i < args->count; i++) {
        puppet_value_t *val = puppet_eval_expr(args->exprs[i], env);
        if (val && val->type != PUPPET_VALUE_UNDEF) {
            return val;
        }
        if (val) puppet_value_destroy(val);
    }

    puppet_log(PUPPET_LOG_ERROR, "pick(): no valid value found");
    return puppet_value_create_undef();
}

/**
 * @brief Puppet pick_default() function - return first non-undef value, or last value
 *
 * Usage: pick_default(value1, value2, ..., default)
 * Returns the first value that is not undef, or the last value if all are undef.
 */
puppet_value_t *puppet_func_pick_default(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        return puppet_value_create_undef();
    }

    puppet_value_t *last = NULL;

    for (size_t i = 0; i < args->count; i++) {
        puppet_value_t *val = puppet_eval_expr(args->exprs[i], env);
        if (val && val->type != PUPPET_VALUE_UNDEF) {
            if (last) puppet_value_destroy(last);
            return val;
        }
        if (last) puppet_value_destroy(last);
        last = val;
    }

    return last ? last : puppet_value_create_undef();
}

/**
 * @brief Puppet hiera() function - lookup value from Hiera data
 *
 * Usage: hiera(key)
 *        hiera(key, default)
 *        hiera(key, default, override)
 * Returns the value from Hiera or the default if not found.
 */
puppet_value_t *puppet_func_hiera(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log(PUPPET_LOG_ERROR, "hiera() requires at least 1 argument (key)");
        return puppet_value_create_undef();
    }

    /* Get the key */
    puppet_value_t *key_val = puppet_eval_expr(args->exprs[0], env);
    if (!key_val || key_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "hiera() first argument must be a string key");
        if (key_val) puppet_value_destroy(key_val);
        return puppet_value_create_undef();
    }
    const char *key = key_val->data.string.data;

    /* Get optional default value */
    puppet_value_t *default_val = NULL;
    if (args->count >= 2) {
        default_val = puppet_eval_expr(args->exprs[1], env);
    }

    /* Look up in Hiera via data providers */
    puppet_value_t *result = NULL;

    /* Check data providers (Hiera should be registered as one) */
    for (size_t i = 0; i < env->data_provider_count; i++) {
        puppet_data_provider_t *provider = env->data_providers[i];
        if (provider && provider->lookup) {
            result = provider->lookup(key, env, provider->data);
            if (result) {
                break;
            }
        }
    }

    puppet_value_destroy(key_val);

    if (result) {
        if (default_val) puppet_value_destroy(default_val);
        return result;
    }

    /* Return default or undef */
    if (default_val) {
        return default_val;
    }

    puppet_log_loc(PUPPET_LOG_WARNING, get_args_location(args),
        "hiera(): key not found and no default provided");
    return puppet_value_create_undef();
}

/**
 * @brief Puppet getvar() function - get variable by name string
 *
 * Usage: getvar('class::variable_name')
 * Returns the value of the variable, or undef if not found.
 */
puppet_value_t *puppet_func_getvar(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args),
            "getvar() requires at least 1 argument (variable name)");
        return puppet_value_create_undef();
    }

    /* Get the variable name */
    puppet_value_t *name_val = puppet_eval_expr(args->exprs[0], env);
    if (!name_val || name_val->type != PUPPET_VALUE_STRING) {
        puppet_log_loc(PUPPET_LOG_ERROR, get_args_location(args), "getvar() argument must be a string");
        if (name_val) puppet_value_destroy(name_val);
        return puppet_value_create_undef();
    }

    const char *var_name = name_val->data.string.data;

    /* Use the existing lookup chain which handles class auto-inclusion */
    puppet_value_t *result = puppet_variable_lookup_chain(env, var_name);
    if (result) {
        result = puppet_value_copy(result);
    }

    puppet_value_destroy(name_val);

    return result ? result : puppet_value_create_undef();
}

/**
 * @brief Puppet file() function - read file content at compile time
 *
 * Usage: file('module/path') or file('/absolute/path')
 * Returns the content of the file as a string.
 */
puppet_value_t *puppet_func_file(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log(PUPPET_LOG_ERROR, "file() requires at least 1 argument (file path)");
        return puppet_value_create_undef();
    }

    /* Try each argument until one succeeds */
    for (size_t i = 0; i < args->count; i++) {
        puppet_value_t *path_val = puppet_eval_expr(args->exprs[i], env);
        if (!path_val || path_val->type != PUPPET_VALUE_STRING) {
            if (path_val) puppet_value_destroy(path_val);
            continue;
        }

        const char *path = path_val->data.string.data;
        char fullpath[4096];
        FILE *fp = NULL;

        /* Check if it's an absolute path */
        if (path[0] == '/') {
            fp = fopen(path, "r");
        } else {
            /* Module-relative path: module_name/file_path */
            /* Try to find in module's files directory */
            const char *slash = strchr(path, '/');
            if (slash && env->loader && env->loader->modules_path) {
                size_t module_len = slash - path;
                char module_name[256];
                if (module_len < sizeof(module_name)) {
                    memcpy(module_name, path, module_len);
                    module_name[module_len] = '\0';

                    /* Look in the modules path */
                    snprintf(fullpath, sizeof(fullpath), "%s/%s/files/%s",
                             env->loader->modules_path, module_name, slash + 1);
                    fp = fopen(fullpath, "r");
                }
            }
        }

        puppet_value_destroy(path_val);

        if (fp) {
            /* Read file content */
            fseek(fp, 0, SEEK_END);
            long size = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            char *content = puppet_malloc(size + 1);
            size_t read_size = fread(content, 1, size, fp);
            content[read_size] = '\0';
            fclose(fp);

            puppet_value_t *result = puppet_value_create_string(content, read_size);
            puppet_free(content);
            return result;
        }
    }

    puppet_log(PUPPET_LOG_ERROR, "file(): could not read any of the specified files");
    return puppet_value_create_undef();
}

/**
 * @brief Puppet inline_template() function - evaluate ERB template inline
 *
 * Usage: inline_template('ERB template string')
 * Returns the result of evaluating the template.
 */
puppet_value_t *puppet_func_inline_template(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        puppet_log(PUPPET_LOG_ERROR, "inline_template() requires at least 1 argument");
        return puppet_value_create_undef();
    }

    /* Skip ERB in parallel mode - return placeholder */
    if (env && env->skip_erb) {
        return puppet_value_create_string("[inline_template skipped in parallel mode]",
                                          strlen("[inline_template skipped in parallel mode]"));
    }

    /* Initialize Ruby if needed (shared context) */
    static puppet_ruby_context_t *ruby_ctx = NULL;
    if (!ruby_ctx) {
        ruby_ctx = puppet_ruby_init();
        if (!ruby_ctx) {
            puppet_log(PUPPET_LOG_ERROR, "inline_template(): failed to initialize Ruby");
            return puppet_value_create_undef();
        }
    }

    /* Concatenate all template outputs */
    char *full_output = NULL;
    size_t full_len = 0;

    for (size_t i = 0; i < args->count; i++) {
        /* Evaluate the template string argument */
        puppet_value_t *template_val = puppet_eval_expr(args->exprs[i], env);
        if (!template_val || template_val->type != PUPPET_VALUE_STRING) {
            puppet_log(PUPPET_LOG_ERROR, "inline_template() argument must be a string");
            if (template_val) puppet_value_destroy(template_val);
            if (full_output) puppet_free(full_output);
            return puppet_value_create_undef();
        }

        const char *template_str = template_val->data.string.data;

        /* Use the ERB processing function */
        char *result = puppet_erb_render(template_str, env, ruby_ctx, "inline_template");
        puppet_value_destroy(template_val);

        if (result) {
            size_t result_len = strlen(result);
            char *new_output = puppet_malloc(full_len + result_len + 1);
            if (full_output) {
                memcpy(new_output, full_output, full_len);
                puppet_free(full_output);
            }
            memcpy(new_output + full_len, result, result_len + 1);
            full_output = new_output;
            full_len += result_len;
            puppet_free(result);
        }
    }

    if (full_output) {
        puppet_value_t *ret = puppet_value_create_string(full_output, full_len);
        puppet_free(full_output);
        return ret;
    }

    return puppet_value_create_undef();
}

