#ifndef PUPPET_STDLIB_H
#define PUPPET_STDLIB_H

#include "puppet_ast.h"
#include "puppet_interpreter.h"

/*
 * ===========================================================================
 * LOGGING WITH SOURCE LOCATION
 * ===========================================================================
 */

/**
 * @brief Log levels for puppet_log functions
 */
typedef enum {
    PUPPET_LOG_DEBUG,
    PUPPET_LOG_INFO,
    PUPPET_LOG_NOTICE,
    PUPPET_LOG_WARNING,
    PUPPET_LOG_ERROR,
    PUPPET_LOG_CRITICAL
} puppet_log_level_t;

/**
 * @brief Log a message with source location information
 * @param level Log level
 * @param loc Source location (may have NULL filename)
 * @param format Printf-style format string
 * @param ... Format arguments
 */
void puppet_log_loc(puppet_log_level_t level, puppet_location_t loc, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

/**
 * @brief Log an error with source location
 * @param loc Source location
 * @param format Printf-style format string
 * @param ... Format arguments
 */
void puppet_error_at(puppet_location_t loc, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

/**
 * @brief Log a warning with source location
 * @param loc Source location
 * @param format Printf-style format string
 * @param ... Format arguments
 */
void puppet_warning_at(puppet_location_t loc, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

/**
 * @brief Set thread-local environment for context-aware logging
 *
 * When set, logging functions will automatically increment error/warning
 * counters in the environment. Thread-local for parallel safety.
 *
 * @param env Environment to use for logging counters (NULL to disable)
 */
void puppet_set_log_env(puppet_env_t *env);

/**
 * @brief Get the current thread-local logging environment
 * @return Current logging environment or NULL if not set
 */
puppet_env_t *puppet_get_log_env(void);

// Core functions
puppet_value_t *puppet_func_fail(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_notice(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_info(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_warning(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_err(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_crit(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_debug(puppet_expr_list_t *args, puppet_env_t *env);

// Resource functions
puppet_value_t *puppet_func_defined(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_realize(puppet_expr_list_t *args, puppet_env_t *env);
void realize_single_resource(puppet_stmt_t *stmt, size_t instance_idx, puppet_env_t *env);
puppet_value_t *puppet_func_tag(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_tagged(puppet_expr_list_t *args, puppet_env_t *env);

// Data lookup functions (Hiera)
puppet_value_t *puppet_func_hiera(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_lookup(puppet_expr_list_t *args, puppet_env_t *env);

// String manipulation functions
puppet_value_t *puppet_func_split(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_join(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_downcase(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_upcase(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_strip(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_lstrip(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_rstrip(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_chomp(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_chop(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_capitalize(puppet_expr_list_t *args, puppet_env_t *env);

// Inspection functions
puppet_value_t *puppet_func_size(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_empty(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_keys(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_values(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_has_key(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_member(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_reverse(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_unique(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_sort(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_flatten(puppet_expr_list_t *args, puppet_env_t *env);

// Array functions
puppet_value_t *puppet_func_concat(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_delete(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_delete_at(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_first(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_last(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_range(puppet_expr_list_t *args, puppet_env_t *env);

// Hash functions
puppet_value_t *puppet_func_merge(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_mysql_normalise_and_deepmerge(puppet_expr_list_t *args, puppet_env_t *env);

// Shell/string escaping functions
puppet_value_t *puppet_func_shell_escape(puppet_expr_list_t *args, puppet_env_t *env);

// Type checking functions
puppet_value_t *puppet_func_is_string(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_is_array(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_is_hash(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_is_numeric(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_is_bool(puppet_expr_list_t *args, puppet_env_t *env);

// Math functions
puppet_value_t *puppet_func_abs(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_min(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_max(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_floor(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_ceil(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_round(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_sqrt(puppet_expr_list_t *args, puppet_env_t *env);

// Path functions
puppet_value_t *puppet_func_basename(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_dirname(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_extname(puppet_expr_list_t *args, puppet_env_t *env);

// Regex functions
puppet_value_t *puppet_func_regsubst(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_match(puppet_expr_list_t *args, puppet_env_t *env);

// Crypto functions
puppet_value_t *puppet_func_sha1(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_md5(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_base64(puppet_expr_list_t *args, puppet_env_t *env);

// Iterator functions (take full expression for lambda access)
puppet_value_t *puppet_func_each(puppet_expr_t *expr, puppet_env_t *env);
puppet_value_t *puppet_func_map(puppet_expr_t *expr, puppet_env_t *env);
puppet_value_t *puppet_func_filter(puppet_expr_t *expr, puppet_env_t *env);
puppet_value_t *puppet_func_reduce(puppet_expr_t *expr, puppet_env_t *env);

// Validation functions (legacy stdlib)
puppet_value_t *puppet_func_validate_re(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_validate_hash(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_validate_string(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_validate_array(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_validate_bool(puppet_expr_list_t *args, puppet_env_t *env);

// Version comparison
puppet_value_t *puppet_func_versioncmp(puppet_expr_list_t *args, puppet_env_t *env);

// Domain/IP validation
puppet_value_t *puppet_func_is_domain_name(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_is_ip_address(puppet_expr_list_t *args, puppet_env_t *env);

// Resource creation
puppet_value_t *puppet_func_create_resources(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_ensure_packages(puppet_expr_list_t *args, puppet_env_t *env);

// Conversion functions
puppet_value_t *puppet_func_any2array(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_str2bool(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_bool2str(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_type(puppet_expr_list_t *args, puppet_env_t *env);

// Random functions
puppet_value_t *puppet_func_fqdn_rand(puppet_expr_list_t *args, puppet_env_t *env);

// Type assertion
puppet_value_t *puppet_func_assert_type(puppet_expr_list_t *args, puppet_env_t *env);

// Data access
puppet_value_t *puppet_func_dig(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_pick(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_pick_default(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_getvar(puppet_expr_list_t *args, puppet_env_t *env);

// File functions
puppet_value_t *puppet_func_file(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_inline_template(puppet_expr_list_t *args, puppet_env_t *env);

// Utility function to convert values to strings for logging
char *puppet_value_to_display_string(puppet_value_t *value);

#endif