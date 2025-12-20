/*
 * Puppet Standard Library Functions
 * 
 * This module implements the core Puppet functions that are essential
 * for catalog compilation and resource management.
 */

#include "puppet_stdlib.h"
#include "puppet_memory.h"
#include "puppet_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Logging levels
typedef enum {
    PUPPET_LOG_DEBUG,
    PUPPET_LOG_INFO,
    PUPPET_LOG_NOTICE,
    PUPPET_LOG_WARNING,
    PUPPET_LOG_ERROR,
    PUPPET_LOG_CRITICAL
} puppet_log_level_t;

// Format log message with timestamp and level
static void puppet_log(puppet_log_level_t level, const char *message) {
    const char *level_str;
    switch (level) {
        case PUPPET_LOG_DEBUG:    level_str = "Debug"; break;
        case PUPPET_LOG_INFO:     level_str = "Info"; break;
        case PUPPET_LOG_NOTICE:   level_str = "Notice"; break;
        case PUPPET_LOG_WARNING:  level_str = "Warning"; break;
        case PUPPET_LOG_ERROR:    level_str = "Error"; break;
        case PUPPET_LOG_CRITICAL: level_str = "Critical"; break;
        default:                  level_str = "Unknown"; break;
    }
    
    time_t now;
    time(&now);
    struct tm *tm_info = localtime(&now);
    char time_buffer[26];
    strftime(time_buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    
    fprintf(stderr, "[%s] %s: %s\n", time_buffer, level_str, message);
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
    puppet_log(PUPPET_LOG_CRITICAL, message);
    
    // Set compilation failure flag in environment
    if (env) {
        env->compilation_failed = 1;
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
    puppet_log(PUPPET_LOG_NOTICE, message);
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
    puppet_log(PUPPET_LOG_INFO, message);
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
    puppet_log(PUPPET_LOG_WARNING, message);
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
    puppet_log(PUPPET_LOG_ERROR, message);
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
    puppet_log(PUPPET_LOG_DEBUG, message);
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
        puppet_log(PUPPET_LOG_ERROR, "defined() requires exactly one argument");
        return puppet_value_create_bool(false);
    }
    
    puppet_value_t *arg = puppet_eval_expr(args->exprs[0], env);
    bool is_defined = false;
    
    if (arg->type == PUPPET_VALUE_STRING) {
        const char *name = arg->data.string.data;
        
        // Check if it's a class name (simplified - just check if loader exists)
        // TODO: Add proper class checking when loader API is exposed
        if (env->loader) {
            // For now, we can't check loaded classes without loader API
            // This would need puppet_loader_is_class_loaded() function
        }
        
        // Check defined resources
        if (!is_defined && env->defined_resources) {
            puppet_value_t *resource_val = puppet_hash_get(env->defined_resources, name, strlen(name));
            if (resource_val) {
                is_defined = true;
            }
        }
    }
    // TODO: Handle resource reference format like File['/etc/motd']
    
    puppet_value_destroy(arg);
    return puppet_value_create_bool(is_defined);
}

/**
 * realize() - Realize virtual resources
 * 
 * Usage: realize(User['john'], User['jane'])
 */
puppet_value_t *puppet_func_realize(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count == 0) {
        puppet_log(PUPPET_LOG_ERROR, "realize() requires at least one argument");
        return puppet_value_create_undef();
    }
    
    // TODO: Implement virtual resource realization
    // For now, just log that we would realize resources
    for (size_t i = 0; i < args->count; i++) {
        puppet_value_t *val = puppet_eval_expr(args->exprs[i], env);
        char *str = puppet_value_to_display_string(val);
        char message[256];
        snprintf(message, sizeof(message), "Would realize virtual resource: %s", str);
        puppet_log(PUPPET_LOG_DEBUG, message);
        puppet_free(str);
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
        puppet_log(PUPPET_LOG_ERROR, "tag() requires at least one argument");
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
            
            char message[256];
            snprintf(message, sizeof(message), "Added tag: %s", tag_val->data.string.data);
            puppet_log(PUPPET_LOG_DEBUG, message);
        } else {
            puppet_log(PUPPET_LOG_WARNING, "tag() argument must be a string");
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
        puppet_log(PUPPET_LOG_ERROR, "tagged() requires exactly one argument");
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
        puppet_log(PUPPET_LOG_ERROR, "lookup() requires at least one argument");
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
            puppet_log(PUPPET_LOG_ERROR, "lookup() options hash must contain 'key' string");
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
        puppet_log(PUPPET_LOG_ERROR, "lookup() key must be a string");
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