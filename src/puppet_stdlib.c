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