#ifndef PUPPET_STDLIB_H
#define PUPPET_STDLIB_H

#include "puppet_ast.h"
#include "puppet_interpreter.h"

// Core functions
puppet_value_t *puppet_func_fail(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_notice(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_info(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_warning(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_err(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_debug(puppet_expr_list_t *args, puppet_env_t *env);

// Resource functions
puppet_value_t *puppet_func_defined(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_realize(puppet_expr_list_t *args, puppet_env_t *env);
void realize_single_resource(puppet_stmt_t *stmt, size_t instance_idx, puppet_env_t *env);
puppet_value_t *puppet_func_tag(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_tagged(puppet_expr_list_t *args, puppet_env_t *env);

// Data lookup functions
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

// Utility function to convert values to strings for logging
char *puppet_value_to_display_string(puppet_value_t *value);

#endif