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

// Math functions
puppet_value_t *puppet_func_abs(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_min(puppet_expr_list_t *args, puppet_env_t *env);
puppet_value_t *puppet_func_max(puppet_expr_list_t *args, puppet_env_t *env);

// Utility function to convert values to strings for logging
char *puppet_value_to_display_string(puppet_value_t *value);

#endif