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
#include <ctype.h>
#include <math.h>
#include <regex.h>
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

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
 * realize() - Realize virtual resources
 *
 * Usage: realize(User['john'], User['jane'])
 *
 * Note: Virtual resources (@resource syntax) are not yet fully supported.
 * This function logs the resources that would be realized but does not
 * actually move them from virtual to realized state.
 */
puppet_value_t *puppet_func_realize(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count == 0) {
        puppet_log(PUPPET_LOG_ERROR, "realize() requires at least one argument");
        return puppet_value_create_undef();
    }

    /* Log each resource that would be realized */
    for (size_t i = 0; i < args->count; i++) {
        puppet_value_t *val = puppet_eval_expr(args->exprs[i], env);
        char message[512];

        if (val->type == PUPPET_VALUE_STRING) {
            /* Resource reference passed as string */
            snprintf(message, sizeof(message), "realize: %s (virtual resources not yet implemented)",
                    val->data.string.data);
        } else {
            /* Other expression type */
            char *str = puppet_value_to_display_string(val);
            snprintf(message, sizeof(message), "realize: %s (virtual resources not yet implemented)", str);
            puppet_free(str);
        }
        puppet_log(PUPPET_LOG_INFO, message);

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
        puppet_log(PUPPET_LOG_ERROR, "split() requires 2 arguments: string and pattern");
        return puppet_value_create_undef();
    }

    /* Evaluate arguments */
    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *pattern_val = puppet_eval_expr(args->exprs[1], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "split() first argument must be a string");
        if (str_val) puppet_value_destroy(str_val);
        if (pattern_val) puppet_value_destroy(pattern_val);
        return puppet_value_create_undef();
    }

    if (!pattern_val || pattern_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "split() second argument must be a string pattern");
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
        puppet_log(PUPPET_LOG_ERROR, "join() requires 2 arguments: array and separator");
        return puppet_value_create_undef();
    }

    /* Evaluate arguments */
    puppet_value_t *array_val = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *sep_val = puppet_eval_expr(args->exprs[1], env);

    if (!array_val || array_val->type != PUPPET_VALUE_ARRAY) {
        puppet_log(PUPPET_LOG_ERROR, "join() first argument must be an array");
        if (array_val) puppet_value_destroy(array_val);
        if (sep_val) puppet_value_destroy(sep_val);
        return puppet_value_create_undef();
    }

    if (!sep_val || sep_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "join() second argument must be a string separator");
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
        puppet_log(PUPPET_LOG_ERROR, "downcase() requires 1 argument: string");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "downcase() argument must be a string");
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
        puppet_log(PUPPET_LOG_ERROR, "upcase() requires 1 argument: string");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "upcase() argument must be a string");
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
        puppet_log(PUPPET_LOG_ERROR, "strip() requires 1 argument: string");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "strip() argument must be a string");
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
        puppet_log(PUPPET_LOG_ERROR, "lstrip() requires 1 argument: string");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "lstrip() argument must be a string");
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
        puppet_log(PUPPET_LOG_ERROR, "rstrip() requires 1 argument: string");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "rstrip() argument must be a string");
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
        puppet_log(PUPPET_LOG_ERROR, "chomp() requires 1 argument: string");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "chomp() argument must be a string");
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
        puppet_log(PUPPET_LOG_ERROR, "chop() requires 1 argument: string");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "chop() argument must be a string");
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
        puppet_log(PUPPET_LOG_ERROR, "capitalize() requires 1 argument: string");
        return puppet_value_create_undef();
    }

    puppet_value_t *str_val = puppet_eval_expr(args->exprs[0], env);

    if (!str_val || str_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "capitalize() argument must be a string");
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
        puppet_log(PUPPET_LOG_ERROR, "size() requires 1 argument");
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
            puppet_log(PUPPET_LOG_ERROR, "size() argument must be a string, array, or hash");
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
        puppet_log(PUPPET_LOG_ERROR, "empty() requires 1 argument");
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
        puppet_log(PUPPET_LOG_ERROR, "keys() requires 1 argument: hash");
        return puppet_value_create_undef();
    }

    puppet_value_t *hash_val = puppet_eval_expr(args->exprs[0], env);

    if (!hash_val || hash_val->type != PUPPET_VALUE_HASH) {
        puppet_log(PUPPET_LOG_ERROR, "keys() argument must be a hash");
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
        puppet_log(PUPPET_LOG_ERROR, "values() requires 1 argument: hash");
        return puppet_value_create_undef();
    }

    puppet_value_t *hash_val = puppet_eval_expr(args->exprs[0], env);

    if (!hash_val || hash_val->type != PUPPET_VALUE_HASH) {
        puppet_log(PUPPET_LOG_ERROR, "values() argument must be a hash");
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
        puppet_log(PUPPET_LOG_ERROR, "has_key() requires 2 arguments: hash and key");
        return puppet_value_create_bool(false);
    }

    puppet_value_t *hash_val = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *key_val = puppet_eval_expr(args->exprs[1], env);

    if (!hash_val || hash_val->type != PUPPET_VALUE_HASH) {
        puppet_log(PUPPET_LOG_ERROR, "has_key() first argument must be a hash");
        if (hash_val) puppet_value_destroy(hash_val);
        if (key_val) puppet_value_destroy(key_val);
        return puppet_value_create_bool(false);
    }

    if (!key_val || key_val->type != PUPPET_VALUE_STRING) {
        puppet_log(PUPPET_LOG_ERROR, "has_key() second argument must be a string");
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
        puppet_log(PUPPET_LOG_ERROR, "member() requires 2 arguments: array and value");
        return puppet_value_create_bool(false);
    }

    puppet_value_t *array_val = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *search_val = puppet_eval_expr(args->exprs[1], env);

    if (!array_val || array_val->type != PUPPET_VALUE_ARRAY) {
        puppet_log(PUPPET_LOG_ERROR, "member() first argument must be an array");
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
        puppet_log(PUPPET_LOG_ERROR, "reverse() requires 1 argument: array");
        return puppet_value_create_undef();
    }

    puppet_value_t *array_val = puppet_eval_expr(args->exprs[0], env);

    if (!array_val || array_val->type != PUPPET_VALUE_ARRAY) {
        puppet_log(PUPPET_LOG_ERROR, "reverse() argument must be an array");
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
        puppet_log(PUPPET_LOG_ERROR, "unique() requires 1 argument: array");
        return puppet_value_create_undef();
    }

    puppet_value_t *array_val = puppet_eval_expr(args->exprs[0], env);

    if (!array_val || array_val->type != PUPPET_VALUE_ARRAY) {
        puppet_log(PUPPET_LOG_ERROR, "unique() argument must be an array");
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
        puppet_log(PUPPET_LOG_ERROR, "sort() requires 1 argument: array");
        return puppet_value_create_undef();
    }

    puppet_value_t *array_val = puppet_eval_expr(args->exprs[0], env);

    if (!array_val || array_val->type != PUPPET_VALUE_ARRAY) {
        puppet_log(PUPPET_LOG_ERROR, "sort() argument must be an array");
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
        puppet_log(PUPPET_LOG_ERROR, "flatten() requires 1 argument: array");
        return puppet_value_create_undef();
    }

    puppet_value_t *array_val = puppet_eval_expr(args->exprs[0], env);

    if (!array_val || array_val->type != PUPPET_VALUE_ARRAY) {
        puppet_log(PUPPET_LOG_ERROR, "flatten() argument must be an array");
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
        puppet_log(PUPPET_LOG_ERROR, "concat() requires at least 1 argument");
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
        puppet_log(PUPPET_LOG_ERROR, "delete() requires 2 arguments: collection, value");
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

    puppet_log(PUPPET_LOG_ERROR, "delete() first argument must be an array or hash");
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
        puppet_log(PUPPET_LOG_ERROR, "delete_at() requires 2 arguments: array, index");
        return puppet_value_create_undef();
    }

    puppet_value_t *arr = puppet_eval_expr(args->exprs[0], env);
    puppet_value_t *idx_val = puppet_eval_expr(args->exprs[1], env);

    if (!arr || arr->type != PUPPET_VALUE_ARRAY) {
        puppet_log(PUPPET_LOG_ERROR, "delete_at() first argument must be an array");
        if (arr) puppet_value_destroy(arr);
        if (idx_val) puppet_value_destroy(idx_val);
        return puppet_value_create_undef();
    }

    if (!idx_val || idx_val->type != PUPPET_VALUE_NUMBER) {
        puppet_log(PUPPET_LOG_ERROR, "delete_at() second argument must be a number");
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
        puppet_log(PUPPET_LOG_ERROR, "first() requires 1 argument: array");
        return puppet_value_create_undef();
    }

    puppet_value_t *arr = puppet_eval_expr(args->exprs[0], env);

    if (!arr || arr->type != PUPPET_VALUE_ARRAY) {
        puppet_log(PUPPET_LOG_ERROR, "first() argument must be an array");
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
        puppet_log(PUPPET_LOG_ERROR, "last() requires 1 argument: array");
        return puppet_value_create_undef();
    }

    puppet_value_t *arr = puppet_eval_expr(args->exprs[0], env);

    if (!arr || arr->type != PUPPET_VALUE_ARRAY) {
        puppet_log(PUPPET_LOG_ERROR, "last() argument must be an array");
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
        puppet_log(PUPPET_LOG_ERROR, "range() requires at least 2 arguments: start, end");
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
        puppet_log(PUPPET_LOG_ERROR, "range() requires numeric or single-character string arguments");
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
        puppet_log(PUPPET_LOG_ERROR, "merge() requires at least 1 argument");
        return puppet_value_create_undef();
    }

    puppet_value_t *result = puppet_value_create_hash();

    for (size_t i = 0; i < args->count; i++) {
        puppet_value_t *hash_val = puppet_eval_expr(args->exprs[i], env);

        if (!hash_val || hash_val->type != PUPPET_VALUE_HASH) {
            puppet_log(PUPPET_LOG_ERROR, "merge() arguments must be hashes");
            if (hash_val) puppet_value_destroy(hash_val);
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
        puppet_log(PUPPET_LOG_ERROR, "abs() requires 1 argument: number");
        return puppet_value_create_undef();
    }

    puppet_value_t *num_val = puppet_eval_expr(args->exprs[0], env);

    if (!num_val || num_val->type != PUPPET_VALUE_NUMBER) {
        puppet_log(PUPPET_LOG_ERROR, "abs() argument must be a number");
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
        puppet_log(PUPPET_LOG_ERROR, "min() requires at least 1 argument");
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
        puppet_log(PUPPET_LOG_ERROR, "min() arguments must be numbers or an array");
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
        puppet_log(PUPPET_LOG_ERROR, "max() requires at least 1 argument");
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
        puppet_log(PUPPET_LOG_ERROR, "max() arguments must be numbers or an array");
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
        puppet_log(PUPPET_LOG_ERROR, "floor() requires 1 argument: number");
        return puppet_value_create_undef();
    }

    puppet_value_t *num_val = puppet_eval_expr(args->exprs[0], env);

    if (!num_val || num_val->type != PUPPET_VALUE_NUMBER) {
        puppet_log(PUPPET_LOG_ERROR, "floor() argument must be a number");
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
        puppet_log(PUPPET_LOG_ERROR, "ceil() requires 1 argument: number");
        return puppet_value_create_undef();
    }

    puppet_value_t *num_val = puppet_eval_expr(args->exprs[0], env);

    if (!num_val || num_val->type != PUPPET_VALUE_NUMBER) {
        puppet_log(PUPPET_LOG_ERROR, "ceil() argument must be a number");
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
        puppet_log(PUPPET_LOG_ERROR, "round() requires 1 argument: number");
        return puppet_value_create_undef();
    }

    puppet_value_t *num_val = puppet_eval_expr(args->exprs[0], env);

    if (!num_val || num_val->type != PUPPET_VALUE_NUMBER) {
        puppet_log(PUPPET_LOG_ERROR, "round() argument must be a number");
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
        puppet_log(PUPPET_LOG_ERROR, "sqrt() requires 1 argument: number");
        return puppet_value_create_undef();
    }

    puppet_value_t *num_val = puppet_eval_expr(args->exprs[0], env);

    if (!num_val || num_val->type != PUPPET_VALUE_NUMBER) {
        puppet_log(PUPPET_LOG_ERROR, "sqrt() argument must be a number");
        if (num_val) puppet_value_destroy(num_val);
        return puppet_value_create_undef();
    }

    if (num_val->data.number < 0) {
        puppet_log(PUPPET_LOG_ERROR, "sqrt() argument must be non-negative");
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
        puppet_log(PUPPET_LOG_ERROR, "basename() requires 1 argument: path");
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

    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((const unsigned char *)str_val->data.string.data,
         str_val->data.string.len, hash);

    /* Convert to hex string */
    char hex_str[SHA_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        snprintf(hex_str + i * 2, 3, "%02x", hash[i]);
    }

    puppet_value_destroy(str_val);
    return puppet_value_create_string(hex_str, SHA_DIGEST_LENGTH * 2);
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

    unsigned char hash[MD5_DIGEST_LENGTH];
    MD5((const unsigned char *)str_val->data.string.data,
        str_val->data.string.len, hash);

    /* Convert to hex string */
    char hex_str[MD5_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        snprintf(hex_str + i * 2, 3, "%02x", hash[i]);
    }

    puppet_value_destroy(str_val);
    return puppet_value_create_string(hex_str, MD5_DIGEST_LENGTH * 2);
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