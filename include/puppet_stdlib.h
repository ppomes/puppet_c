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

// Utility function to convert values to strings for logging
char *puppet_value_to_display_string(puppet_value_t *value);

#endif