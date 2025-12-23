#include "puppet_interpreter.h"
#include "puppet_erb.h"
#include "puppet_stdlib.h"
#include "puppet_loader.h"
#include "puppet_memory.h"
#include "puppet_hiera.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Global verbose flag */
bool puppet_verbose = false;

// Helper function to convert value to string
static const char *puppet_value_to_string(puppet_value_t *value) {
    if (!value) return "";
    
    static char buffer[1024];  // Static buffer for conversions
    
    switch (value->type) {
        case PUPPET_VALUE_STRING:
            return value->data.string.data;
        case PUPPET_VALUE_NUMBER:
            snprintf(buffer, sizeof(buffer), "%g", value->data.number);
            return buffer;
        case PUPPET_VALUE_BOOL:
            return value->data.boolean ? "true" : "false";
        case PUPPET_VALUE_UNDEF:
            return "";
        default:
            return "";
    }
}

puppet_env_t *puppet_env_create(void) {
    puppet_env_t *env = puppet_calloc(1, sizeof(puppet_env_t));
    env->global_scope = puppet_scope_create(NULL, "global");
    env->current_scope = env->global_scope;
    env->stack_capacity = 16;
    env->scope_stack = puppet_calloc(env->stack_capacity, sizeof(puppet_scope_t*));
    env->stack_depth = 0;
    env->loader = NULL;  /* Loader is optional, set separately */
    env->node_name = NULL;  /* No node filtering by default */
    env->execute_all_nodes = false;
    
    /* Initialize enhanced variable system */
    env->data_provider_capacity = 4;
    env->data_providers = puppet_calloc(env->data_provider_capacity, sizeof(puppet_data_provider_t*));
    env->data_provider_count = 0;
    env->node_scope = puppet_scope_create(env->global_scope, "node");
    env->class_scope = NULL;  /* Set when entering class context */
    
    /* Initialize class definition registry */
    env->class_def_capacity = 4;
    env->class_definitions = puppet_calloc(env->class_def_capacity, sizeof(puppet_stmt_t*));
    env->class_def_count = 0;
    
    /* Initialize facts database */
    env->facts_db = NULL;
    
    /* Initialize resource catalog for duplicate detection */
    env->resource_catalog = puppet_calloc(1, sizeof(puppet_hash_t));
    env->resource_catalog->bucket_count = 64;  /* Start with reasonable size */
    env->resource_catalog->buckets = puppet_calloc(env->resource_catalog->bucket_count, sizeof(puppet_hash_entry_t*));
    
    /* Initialize template output */
    env->template_output_target = NULL;
    env->template_output_found = false;
    
    /* Initialize core function support */
    env->defined_resources = puppet_calloc(1, sizeof(puppet_hash_t));
    env->defined_resources->bucket_count = 64;
    env->defined_resources->buckets = puppet_calloc(env->defined_resources->bucket_count, sizeof(puppet_hash_entry_t*));
    env->current_tags = NULL;  /* Initialized when first tag is added */
    env->compilation_failed = false;
    env->failure_message = NULL;

    /* Initialize output control */
    env->verbose = puppet_verbose;  /* Inherit from global flag */

    /* Register Hiera data provider */
    puppet_hiera_register_provider(env, "data");

    return env;
}

void puppet_env_set_verbose(puppet_env_t *env, bool verbose) {
    if (env) {
        env->verbose = verbose;
    }
    puppet_verbose = verbose;  /* Also set global flag */
}

void puppet_env_destroy(puppet_env_t *env) {
    if (!env) return;
    
    // Clean up scope stack
    while (env->stack_depth > 0) {
        puppet_scope_pop(env);
    }
    
    // Clean up scopes
    puppet_scope_destroy(env->global_scope);
    if (env->node_scope) puppet_scope_destroy(env->node_scope);
    if (env->class_scope) puppet_scope_destroy(env->class_scope);
    
    // Clean up data providers
    for (size_t i = 0; i < env->data_provider_count; i++) {
        puppet_data_provider_t *provider = env->data_providers[i];
        if (provider && provider->cleanup) {
            provider->cleanup(provider->data);
        }
        puppet_free(provider->name);
        puppet_free(provider);
    }
    puppet_free(env->data_providers);
    
    // Clean up class definition registry (don't destroy statements, they're owned by AST)
    puppet_free(env->class_definitions);
    
    // Clean up facts database
    if (env->facts_db) {
        puppet_facts_db_destroy(env->facts_db);
    }
    
    // Clean up resource catalog
    if (env->resource_catalog) {
        /* Clean up resource catalog */
        for (size_t i = 0; i < env->resource_catalog->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->resource_catalog->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->resource_catalog->buckets);
        puppet_free(env->resource_catalog);
    }
    
    puppet_free(env->scope_stack);
    puppet_free(env->node_name);
    puppet_free(env->template_output_target);
    
    /* Clean up defined resources hash */
    if (env->defined_resources) {
        for (size_t i = 0; i < env->defined_resources->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->defined_resources->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->defined_resources->buckets);
        puppet_free(env->defined_resources);
    }
    
    /* Clean up tags */
    if (env->current_tags) {
        puppet_value_destroy(env->current_tags);
    }
    
    /* Clean up failure message */
    puppet_free(env->failure_message);
    
    puppet_free(env);
}

puppet_scope_t *puppet_scope_create(puppet_scope_t *parent, const char *name) {
    puppet_scope_t *scope = puppet_calloc(1, sizeof(puppet_scope_t));
    scope->parent = parent;
    scope->name = puppet_string_create(name ? name : "");
    scope->variables = puppet_calloc(1, sizeof(puppet_hash_t));
    scope->variables->bucket_count = 16;
    scope->variables->buckets = puppet_calloc(scope->variables->bucket_count, sizeof(puppet_hash_entry_t*));
    return scope;
}

void puppet_scope_destroy(puppet_scope_t *scope) {
    if (!scope) return;
    
    // Clean up variables hash table
    for (size_t i = 0; i < scope->variables->bucket_count; i++) {
        puppet_hash_entry_t *entry = scope->variables->buckets[i];
        while (entry) {
            puppet_hash_entry_t *next = entry->next;
            puppet_string_free(entry->key);
            puppet_value_destroy(entry->value);
            puppet_free(entry);
            entry = next;
        }
    }
    puppet_free(scope->variables->buckets);
    puppet_free(scope->variables);
    puppet_string_free(scope->name);
    puppet_free(scope);
}

void puppet_scope_push(puppet_env_t *env, puppet_scope_t *scope) {
    if (env->stack_depth >= env->stack_capacity) {
        env->stack_capacity *= 2;
        env->scope_stack = puppet_realloc(env->scope_stack, env->stack_capacity * sizeof(puppet_scope_t*));
    }
    
    env->scope_stack[env->stack_depth++] = env->current_scope;
    env->current_scope = scope;
}

puppet_scope_t *puppet_scope_pop(puppet_env_t *env) {
    if (env->stack_depth == 0) {
        return env->current_scope;
    }
    
    puppet_scope_t *old_scope = env->current_scope;
    env->current_scope = env->scope_stack[--env->stack_depth];
    return old_scope;
}

void puppet_env_set_var(puppet_env_t *env, const char *name, puppet_value_t *value) {
    puppet_scope_set_var(env->current_scope, name, value);
}

puppet_value_t *puppet_env_get_var(puppet_env_t *env, const char *name) {
    return puppet_scope_get_var(env->current_scope, name, true);
}

void puppet_scope_set_var(puppet_scope_t *scope, const char *name, puppet_value_t *value) {
    puppet_hash_set(scope->variables, name, strlen(name), value);
}

puppet_value_t *puppet_scope_get_var(puppet_scope_t *scope, const char *name, bool recursive) {
    puppet_value_t *value = puppet_hash_get(scope->variables, name, strlen(name));
    
    if (!value && recursive && scope->parent) {
        return puppet_scope_get_var(scope->parent, name, true);
    }
    
    return value;
}

puppet_value_t *puppet_eval_expr(puppet_expr_t *expr, puppet_env_t *env) {
    if (!expr) {
        return puppet_value_create_undef();
    }
    
    switch (expr->type) {
        case PUPPET_EXPR_VALUE:
            // Return a copy of the value to avoid double-free issues
            switch (expr->data.value->type) {
                case PUPPET_VALUE_UNDEF:
                    return puppet_value_create_undef();
                case PUPPET_VALUE_BOOL:
                    return puppet_value_create_bool(expr->data.value->data.boolean);
                case PUPPET_VALUE_NUMBER:
                    return puppet_value_create_number(expr->data.value->data.number);
                case PUPPET_VALUE_STRING:
                    return puppet_value_create_string(expr->data.value->data.string.data,
                                                    expr->data.value->data.string.len);
                case PUPPET_VALUE_ARRAY:
                case PUPPET_VALUE_HASH:
                    return puppet_value_copy(expr->data.value);
                default:
                    return puppet_value_create_undef();
            }
            
        case PUPPET_EXPR_VARIABLE:
            return puppet_eval_variable(expr->data.variable.data, env);
            
        case PUPPET_EXPR_BINOP: {
            puppet_value_t *left = puppet_eval_expr(expr->data.binop.left, env);
            puppet_value_t *right = puppet_eval_expr(expr->data.binop.right, env);
            puppet_value_t *result = puppet_eval_binop(expr->data.binop.op, left, right);
            puppet_value_destroy(left);
            puppet_value_destroy(right);
            return result;
        }
            
        case PUPPET_EXPR_UNOP: {
            puppet_value_t *operand = puppet_eval_expr(expr->data.unop.expr, env);
            puppet_value_t *result = puppet_eval_unop(expr->data.unop.op, operand);
            puppet_value_destroy(operand);
            return result;
        }
            
        case PUPPET_EXPR_FUNCALL: {
            // Handle built-in functions
            const char *func_name = expr->data.funcall.name.data;
            
            // Template function
            if (strcmp(func_name, "template") == 0) {
                return puppet_func_template(&expr->data.funcall.args, env);
            }
            // Core logging functions
            else if (strcmp(func_name, "fail") == 0) {
                return puppet_func_fail(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "notice") == 0) {
                return puppet_func_notice(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "info") == 0) {
                return puppet_func_info(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "warning") == 0) {
                return puppet_func_warning(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "err") == 0) {
                return puppet_func_err(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "debug") == 0) {
                return puppet_func_debug(&expr->data.funcall.args, env);
            }
            // Resource functions
            else if (strcmp(func_name, "defined") == 0) {
                return puppet_func_defined(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "realize") == 0) {
                return puppet_func_realize(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "tag") == 0) {
                return puppet_func_tag(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "tagged") == 0) {
                return puppet_func_tagged(&expr->data.funcall.args, env);
            }
            // Data lookup functions
            else if (strcmp(func_name, "lookup") == 0) {
                return puppet_func_lookup(&expr->data.funcall.args, env);
            }
            // String manipulation functions
            else if (strcmp(func_name, "split") == 0) {
                return puppet_func_split(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "join") == 0) {
                return puppet_func_join(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "downcase") == 0) {
                return puppet_func_downcase(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "upcase") == 0) {
                return puppet_func_upcase(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "strip") == 0) {
                return puppet_func_strip(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "lstrip") == 0) {
                return puppet_func_lstrip(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "rstrip") == 0) {
                return puppet_func_rstrip(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "chomp") == 0) {
                return puppet_func_chomp(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "chop") == 0) {
                return puppet_func_chop(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "capitalize") == 0) {
                return puppet_func_capitalize(&expr->data.funcall.args, env);
            }
            // Inspection functions
            else if (strcmp(func_name, "size") == 0 || strcmp(func_name, "length") == 0) {
                return puppet_func_size(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "empty") == 0) {
                return puppet_func_empty(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "keys") == 0) {
                return puppet_func_keys(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "values") == 0) {
                return puppet_func_values(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "has_key") == 0) {
                return puppet_func_has_key(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "member") == 0) {
                return puppet_func_member(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "reverse") == 0) {
                return puppet_func_reverse(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "unique") == 0) {
                return puppet_func_unique(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "sort") == 0) {
                return puppet_func_sort(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "flatten") == 0) {
                return puppet_func_flatten(&expr->data.funcall.args, env);
            }
            // Array functions
            else if (strcmp(func_name, "concat") == 0) {
                return puppet_func_concat(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "delete") == 0) {
                return puppet_func_delete(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "delete_at") == 0) {
                return puppet_func_delete_at(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "first") == 0) {
                return puppet_func_first(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "last") == 0) {
                return puppet_func_last(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "range") == 0) {
                return puppet_func_range(&expr->data.funcall.args, env);
            }
            // Hash functions
            else if (strcmp(func_name, "merge") == 0) {
                return puppet_func_merge(&expr->data.funcall.args, env);
            }
            // Type checking functions
            else if (strcmp(func_name, "is_string") == 0) {
                return puppet_func_is_string(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "is_array") == 0) {
                return puppet_func_is_array(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "is_hash") == 0) {
                return puppet_func_is_hash(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "is_numeric") == 0 || strcmp(func_name, "is_integer") == 0 || strcmp(func_name, "is_float") == 0) {
                return puppet_func_is_numeric(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "is_bool") == 0 || strcmp(func_name, "is_boolean") == 0) {
                return puppet_func_is_bool(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "abs") == 0) {
                return puppet_func_abs(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "min") == 0) {
                return puppet_func_min(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "max") == 0) {
                return puppet_func_max(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "floor") == 0) {
                return puppet_func_floor(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "ceil") == 0 || strcmp(func_name, "ceiling") == 0) {
                return puppet_func_ceil(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "round") == 0) {
                return puppet_func_round(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "sqrt") == 0) {
                return puppet_func_sqrt(&expr->data.funcall.args, env);
            }
            // Path functions
            else if (strcmp(func_name, "basename") == 0) {
                return puppet_func_basename(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "dirname") == 0) {
                return puppet_func_dirname(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "extname") == 0) {
                return puppet_func_extname(&expr->data.funcall.args, env);
            }
            else {
                puppet_error("Unknown function: %s", func_name);
                return puppet_value_create_undef();
            }
        }
            
        case PUPPET_EXPR_INTERPOLATED_STRING: {
            // Build interpolated string
            size_t total_len = 0;
            char **parts = puppet_calloc(expr->data.interpolated.count * 2 + 1, sizeof(char*));
            size_t part_count = 0;
            
            // Evaluate all parts
            for (size_t i = 0; i < expr->data.interpolated.count; i++) {
                // Add literal part if present
                if (expr->data.interpolated.parts && expr->data.interpolated.parts[i].data) {
                    parts[part_count] = puppet_strdup(expr->data.interpolated.parts[i].data);
                    total_len += expr->data.interpolated.parts[i].len;
                    part_count++;
                }
                
                // Evaluate expression if present
                if (expr->data.interpolated.exprs && expr->data.interpolated.exprs[i]) {
                    puppet_value_t *val = puppet_eval_expr(expr->data.interpolated.exprs[i], env);
                    const char *str = puppet_value_to_string(val);
                    if (str && *str) {
                        // Make a copy immediately since puppet_value_to_string may use static buffer
                        size_t str_len = strlen(str);
                        parts[part_count] = puppet_malloc(str_len + 1);
                        memcpy(parts[part_count], str, str_len + 1);
                        total_len += str_len;
                        part_count++;
                    }
                    puppet_value_destroy(val);
                }
            }
            
            // Build final string
            char *result = puppet_malloc(total_len + 1);
            size_t pos = 0;
            for (size_t i = 0; i < part_count; i++) {
                if (parts[i]) {
                    size_t len = strlen(parts[i]);
                    memcpy(result + pos, parts[i], len);
                    pos += len;
                    puppet_free(parts[i]);
                }
            }
            result[total_len] = '\0';
            puppet_free(parts);
            
            puppet_value_t *ret = puppet_value_create_string(result, total_len);
            puppet_free(result);
            return ret;
        }
            
        default:
            puppet_warn("Unimplemented expression type: %d", expr->type);
            return puppet_value_create_undef();
    }
}

puppet_value_t *puppet_eval_variable(const char *name, puppet_env_t *env) {
    // Use enhanced lookup chain instead of simple scope lookup
    puppet_value_t *value = puppet_variable_lookup_chain(env, name);

    if (!value) {
        puppet_warn("Undefined variable: %s", name);
        return puppet_value_create_undef();
    }

    // Return a copy to avoid double-free (handles all types including arrays/hashes)
    return puppet_value_copy(value);
}

puppet_value_t *puppet_eval_binop(puppet_binop_t op, puppet_value_t *left, puppet_value_t *right) {
    switch (op) {
        case PUPPET_OP_ADD:
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_number(left->data.number + right->data.number);
            }
            break;
            
        case PUPPET_OP_SUB:
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_number(left->data.number - right->data.number);
            }
            break;
            
        case PUPPET_OP_MUL:
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_number(left->data.number * right->data.number);
            }
            break;
            
        case PUPPET_OP_DIV:
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                if (right->data.number != 0) {
                    return puppet_value_create_number(left->data.number / right->data.number);
                }
            }
            break;
            
        case PUPPET_OP_EQ:
            if (left->type == right->type) {
                switch (left->type) {
                    case PUPPET_VALUE_BOOL:
                        return puppet_value_create_bool(left->data.boolean == right->data.boolean);
                    case PUPPET_VALUE_NUMBER:
                        return puppet_value_create_bool(left->data.number == right->data.number);
                    case PUPPET_VALUE_STRING:
                        return puppet_value_create_bool(
                            left->data.string.len == right->data.string.len &&
                            strcmp(left->data.string.data, right->data.string.data) == 0
                        );
                    default:
                        break;
                }
            }
            return puppet_value_create_bool(false);
            
        case PUPPET_OP_NE:
            if (left->type == right->type) {
                switch (left->type) {
                    case PUPPET_VALUE_BOOL:
                        return puppet_value_create_bool(left->data.boolean != right->data.boolean);
                    case PUPPET_VALUE_NUMBER:
                        return puppet_value_create_bool(left->data.number != right->data.number);
                    case PUPPET_VALUE_STRING:
                        return puppet_value_create_bool(
                            left->data.string.len != right->data.string.len ||
                            strcmp(left->data.string.data, right->data.string.data) != 0
                        );
                    default:
                        break;
                }
            }
            return puppet_value_create_bool(true);
            
        case PUPPET_OP_LT:
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_bool(left->data.number < right->data.number);
            }
            break;
            
        case PUPPET_OP_GT:
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_bool(left->data.number > right->data.number);
            }
            break;
            
        default:
            break;
    }
    
    puppet_warn("Unsupported binary operation");
    return puppet_value_create_undef();
}

puppet_value_t *puppet_eval_unop(puppet_unop_t op, puppet_value_t *operand) {
    switch (op) {
        case PUPPET_UNOP_NOT:
            // Convert operand to boolean and negate
            if (operand->type == PUPPET_VALUE_BOOL) {
                return puppet_value_create_bool(!operand->data.boolean);
            } else if (operand->type == PUPPET_VALUE_UNDEF) {
                return puppet_value_create_bool(true);
            }
            return puppet_value_create_bool(false);
            
        case PUPPET_UNOP_MINUS:
            if (operand->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_number(-operand->data.number);
            }
            break;
            
        case PUPPET_UNOP_PLUS:
            if (operand->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_number(operand->data.number);
            }
            break;
            
        default:
            break;
    }

    puppet_warn("Unsupported unary operation");
    return puppet_value_create_undef();
}

void puppet_exec_stmt(puppet_stmt_t *stmt, puppet_env_t *env) {
    if (!stmt) return;
    
    switch (stmt->type) {
        case PUPPET_STMT_ASSIGNMENT:
            puppet_exec_assignment(stmt->data.assignment.variable.data, 
                                  stmt->data.assignment.value, env);
            break;
            
        case PUPPET_STMT_CLASS_DEF:
            puppet_exec_class_def(stmt, env);
            break;
            
        case PUPPET_STMT_CLASS_INSTANCE:
            puppet_exec_class_instance(stmt, env);
            break;
            
        case PUPPET_STMT_NODE:
            puppet_exec_node(stmt, env);
            break;
            
        case PUPPET_STMT_INCLUDE:
            puppet_exec_include(stmt, env);
            break;
            
        case PUPPET_STMT_FUNCTION_CALL:
            // Execute function call statement (stored as expression)
            if (stmt->data.expr) {
                puppet_value_t *result = puppet_eval_expr(stmt->data.expr, env);
                puppet_value_destroy(result);
            }
            break;
            
        case PUPPET_STMT_RESOURCE:
            puppet_debug("Executing resource: %s", stmt->data.resource.type.data);
            
            // Evaluate resource titles and check for duplicates
            for (size_t i = 0; i < stmt->data.resource.instance_count; i++) {
                puppet_resource_instance_t *instance = &stmt->data.resource.instances[i];
                if (instance->title) {
                    puppet_value_t *title_val = puppet_eval_expr(instance->title, env);
                    const char *title_str = puppet_value_to_string(title_val);
                    
                    // Build resource identifier (type::title)
                    size_t res_id_len = strlen(stmt->data.resource.type.data) + strlen(title_str) + 3;
                    char *resource_id = puppet_malloc(res_id_len);
                    snprintf(resource_id, res_id_len, "%s[%s]", stmt->data.resource.type.data, title_str);
                    
                    // Check for duplicate resource
                    puppet_value_t *existing = puppet_hash_get(env->resource_catalog, 
                                                               resource_id, strlen(resource_id));
                    if (existing) {
                        fprintf(stderr, "Error: Duplicate declaration - %s is already declared\n", resource_id);
                        fprintf(stderr, "       Resource titles must be unique within their type\n");
                        puppet_free(resource_id);
                        puppet_value_destroy(title_val);
                        continue;  // Skip this duplicate resource
                    }
                    
                    // Add to catalog
                    puppet_value_t *marker = puppet_value_create_bool(true);
                    puppet_hash_set(env->resource_catalog, resource_id, strlen(resource_id), marker);

                    puppet_debug("  Title: %s", title_str);
                    
                    // Check if this is the template target for output
                    bool is_template_target = (env->template_output_target && 
                                               strcmp(title_str, env->template_output_target) == 0 &&
                                               strcmp(stmt->data.resource.type.data, "file") == 0);
                    
                    // Show attributes for this instance
                    for (size_t j = 0; j < instance->attr_count; j++) {
                        puppet_value_t *attr_val = puppet_eval_expr(instance->attributes[j].value, env);
                        const char *attr_str = puppet_value_to_string(attr_val);
                        puppet_debug("    %s => %s", instance->attributes[j].name.data, attr_str);
                        
                        // If this is template output mode and we found the content attribute
                        // Output goes to stdout (clean, for piping) - no markers
                        if (is_template_target && strcmp(instance->attributes[j].name.data, "content") == 0) {
                            if (attr_val->type == PUPPET_VALUE_STRING) {
                                printf("%s", attr_val->data.string.data);
                                env->template_output_found = true;
                            }
                        }
                        
                        // Don't free attr_str - it's internal to attr_val
                        puppet_value_destroy(attr_val);
                    }
                    
                    puppet_free(resource_id);
                    puppet_value_destroy(title_val);
                }
            }
            break;

        default:
            puppet_warn("Unimplemented statement type: %d", stmt->type);
            break;
    }
}

void puppet_exec_stmt_list(puppet_stmt_list_t *stmts, puppet_env_t *env) {
    for (size_t i = 0; i < stmts->count; i++) {
        puppet_exec_stmt(stmts->stmts[i], env);
    }
}

void puppet_exec_assignment(const char *var, puppet_expr_t *value, puppet_env_t *env) {
    puppet_value_t *val = puppet_eval_expr(value, env);

    // Use scoped variable assignment (defaults to local scope)
    puppet_env_set_scoped_var(env, var, val, PUPPET_VAR_LOCAL);

    // Debug output
    if (puppet_verbose) {
        const char *val_str;
        char num_buf[64];
        switch (val->type) {
            case PUPPET_VALUE_UNDEF:
                val_str = "undef";
                break;
            case PUPPET_VALUE_BOOL:
                val_str = val->data.boolean ? "true" : "false";
                break;
            case PUPPET_VALUE_STRING:
                val_str = val->data.string.data;
                break;
            case PUPPET_VALUE_NUMBER:
                snprintf(num_buf, sizeof(num_buf), "%.6g", val->data.number);
                val_str = num_buf;
                break;
            default:
                val_str = "(complex value)";
                break;
        }
        puppet_debug("Set $%s = %s", var, val_str);
    }
}

void puppet_exec_class_def(puppet_stmt_t *class_stmt, puppet_env_t *env) {
    const char *class_name = class_stmt->data.class_def.name.data;
    puppet_debug("Defining class: %s", class_name);
    
    // Register this class definition for later instantiation
    puppet_register_class_def(env, class_stmt);
    
    // Create a new scope for the class
    puppet_scope_t *class_scope = puppet_scope_create(env->current_scope, class_name);
    puppet_scope_push(env, class_scope);
    
    // Set class scope in environment for enhanced variable lookup
    puppet_scope_t *old_class_scope = env->class_scope;
    env->class_scope = class_scope;
    
    // Process parameters and set default values
    for (size_t i = 0; i < class_stmt->data.class_def.params.count; i++) {
        puppet_param_t *param = &class_stmt->data.class_def.params.params[i];
        const char *param_name = param->name.data;

        if (param->default_value) {
            // Evaluate default value and set in class scope
            puppet_value_t *default_val = puppet_eval_expr(param->default_value, env);
            puppet_scope_set_var(class_scope, param_name, default_val);
            if (puppet_verbose) {
                const char *val_str;
                char num_buf[64];
                switch (default_val->type) {
                    case PUPPET_VALUE_BOOL:
                        val_str = default_val->data.boolean ? "true" : "false";
                        break;
                    case PUPPET_VALUE_NUMBER:
                        snprintf(num_buf, sizeof(num_buf), "%.6g", default_val->data.number);
                        val_str = num_buf;
                        break;
                    case PUPPET_VALUE_STRING:
                        val_str = default_val->data.string.data;
                        break;
                    default:
                        val_str = "(complex value)";
                        break;
                }
                puppet_debug("Set class parameter $%s = %s (default)", param_name, val_str);
            }
        } else {
            // Set parameter to undef if no default provided
            puppet_value_t *undef_val = puppet_value_create_undef();
            puppet_scope_set_var(class_scope, param_name, undef_val);
            puppet_debug("Set class parameter $%s = undef (no default)", param_name);
        }
    }
    
    // Execute class body
    puppet_exec_stmt_list(&class_stmt->data.class_def.body, env);
    
    // Restore old class scope
    env->class_scope = old_class_scope;
    
    // Pop the class scope
    puppet_scope_t *old_scope = puppet_scope_pop(env);
    puppet_scope_destroy(old_scope);
}

void puppet_exec_program(puppet_program_t *program, puppet_env_t *env) {
    puppet_exec_stmt_list(&program->statements, env);
}

void puppet_exec_include(puppet_stmt_t *include_stmt, puppet_env_t *env) {
    if (!include_stmt || include_stmt->type != PUPPET_STMT_INCLUDE) return;
    
    /* Check if loader is available */
    if (!env->loader) {
        puppet_warn("Include statements require a module loader to be configured");
        return;
    }
    
    /* Process each included class */
    for (size_t i = 0; i < include_stmt->data.names.count; i++) {
        puppet_expr_t *name_expr = include_stmt->data.names.exprs[i];
        
        /* Extract class name from expression */
        if (name_expr && name_expr->type == PUPPET_EXPR_VALUE &&
            name_expr->data.value->type == PUPPET_VALUE_STRING) {
            
            const char *class_name = name_expr->data.value->data.string.data;
            
            /* Include the class using the loader */
            if (!puppet_loader_include_class(env->loader, class_name, env)) {
                puppet_warn("Failed to include class '%s'", class_name);
            }
        }
    }
}

void puppet_env_set_loader(puppet_env_t *env, puppet_loader_t *loader) {
    if (!env) return;
    env->loader = loader;
}

void puppet_exec_node(puppet_stmt_t *node_stmt, puppet_env_t *env) {
    if (!node_stmt || node_stmt->type != PUPPET_STMT_NODE) return;
    
    const char *node_name = node_stmt->data.node.name.data;
    
    /* Check if we should execute this node */
    bool should_execute = false;
    
    if (env->execute_all_nodes) {
        /* Execute all nodes when --all-nodes is specified */
        should_execute = true;
    } else if (!env->node_name) {
        /* No node specified - only execute 'default' node */
        should_execute = (strcmp(node_name, "default") == 0);
    } else {
        /* Specific node requested - check for match */
        should_execute = (strcmp(node_name, env->node_name) == 0);
    }
    
    if (should_execute) {
        puppet_debug("Executing node: %s", node_name);

        /* Clear resource catalog for this node (each node has its own catalog) */
        if (env->resource_catalog) {
            /* Clear existing entries but keep the hash table structure */
            for (size_t i = 0; i < env->resource_catalog->bucket_count; i++) {
                puppet_hash_entry_t *entry = env->resource_catalog->buckets[i];
                while (entry) {
                    puppet_hash_entry_t *next = entry->next;
                    puppet_free(entry->key.data);
                    puppet_value_destroy(entry->value);
                    puppet_free(entry);
                    entry = next;
                }
                env->resource_catalog->buckets[i] = NULL;
            }
            /* Size tracking is handled internally */
        }
        
        /* Switch to node-specific facts if available */
        if (env->facts_db) {
            if (puppet_facts_db_set_current_node(env->facts_db, node_name) == 0) {
                puppet_debug("Using facts for node: %s", node_name);
            } else {
                puppet_debug("No facts found for node %s, using default facts", node_name);
            }
        }
        
        /* Create a new scope for the node */
        puppet_scope_t *node_scope = puppet_scope_create(env->current_scope, node_name);
        puppet_scope_push(env, node_scope);
        
        /* Set automatic variables like $hostname */
        puppet_value_t *hostname_value = puppet_value_create_string(node_name, strlen(node_name));
        puppet_scope_set_var(node_scope, "hostname", hostname_value);
        
        /* Execute node body */
        puppet_exec_stmt_list(&node_stmt->data.node.body, env);
        
        /* Pop the node scope */
        puppet_scope_t *old_scope = puppet_scope_pop(env);
        puppet_scope_destroy(old_scope);
    } else {
        /* Skip this node */
        if (!env->execute_all_nodes && env->node_name) {
            /* Only report skipping when a specific node was requested */
            puppet_debug("Skipping node: %s (looking for %s)", node_name, env->node_name);
        }
    }
}

void puppet_env_set_node(puppet_env_t *env, const char *node_name) {
    if (!env) return;
    
    puppet_free(env->node_name);
    env->node_name = node_name ? puppet_strdup(node_name) : NULL;
    env->execute_all_nodes = false;  /* Specific node mode */
}

void puppet_env_set_execute_all_nodes(puppet_env_t *env, bool execute_all) {
    if (!env) return;
    
    env->execute_all_nodes = execute_all;
    if (execute_all) {
        puppet_free(env->node_name);
        env->node_name = NULL;  /* Clear specific node when in all-nodes mode */
    }
}

void puppet_env_set_template_output(puppet_env_t *env, const char *template_target) {
    if (!env) return;
    
    puppet_free(env->template_output_target);
    env->template_output_target = template_target ? puppet_strdup(template_target) : NULL;
}

void puppet_exec_class_instance(puppet_stmt_t *class_instance_stmt, puppet_env_t *env) {
    if (!class_instance_stmt || class_instance_stmt->type != PUPPET_STMT_CLASS_INSTANCE) return;

    const char *class_name = class_instance_stmt->data.class_instance.class_name.data;
    puppet_debug("Instantiating class: %s", class_name);

    // Find the class definition
    puppet_stmt_t *class_def = puppet_find_class_def(env, class_name);
    if (!class_def) {
        puppet_error("Class '%s' not found", class_name);
        return;
    }
    
    // Create a new scope for the class instance
    puppet_scope_t *class_scope = puppet_scope_create(env->current_scope, class_name);
    puppet_scope_push(env, class_scope);
    
    // Set class scope in environment for enhanced variable lookup
    puppet_scope_t *old_class_scope = env->class_scope;
    env->class_scope = class_scope;
    
    // Process class parameters and apply defaults first
    for (size_t i = 0; i < class_def->data.class_def.params.count; i++) {
        puppet_param_t *param = &class_def->data.class_def.params.params[i];
        const char *param_name = param->name.data;

        // Look for this parameter in provided arguments
        puppet_value_t *param_value = NULL;
        bool found_arg = false;

        for (size_t j = 0; j < class_instance_stmt->data.class_instance.arg_count; j++) {
            puppet_attribute_t *arg = &class_instance_stmt->data.class_instance.arguments[j];
            if (strcmp(arg->name.data, param_name) == 0) {
                param_value = puppet_eval_expr(arg->value, env);
                found_arg = true;
                break;
            }
        }

        // If not provided, use default value
        if (!found_arg && param->default_value) {
            param_value = puppet_eval_expr(param->default_value, env);
        } else if (!found_arg) {
            param_value = puppet_value_create_undef();
        }

        // Set the parameter value in class scope
        if (param_value) {
            puppet_scope_set_var(class_scope, param_name, param_value);

            // Debug output
            if (puppet_verbose) {
                const char *val_str;
                char num_buf[64];
                const char *source;
                if (!found_arg && !param->default_value) {
                    val_str = "undef";
                    source = " (no default)";
                } else {
                    source = found_arg ? " (provided)" : " (default)";
                    switch (param_value->type) {
                        case PUPPET_VALUE_BOOL:
                            val_str = param_value->data.boolean ? "true" : "false";
                            break;
                        case PUPPET_VALUE_NUMBER:
                            snprintf(num_buf, sizeof(num_buf), "%.6g", param_value->data.number);
                            val_str = num_buf;
                            break;
                        case PUPPET_VALUE_STRING:
                            val_str = param_value->data.string.data;
                            break;
                        default:
                            val_str = "(complex value)";
                            break;
                    }
                }
                puppet_debug("Set class parameter $%s = %s%s", param_name, val_str, source);
            }
        }
    }

    // Execute the class body
    puppet_debug("Executing class body for: %s", class_name);
    puppet_exec_stmt_list(&class_def->data.class_def.body, env);

    puppet_debug("Class %s instantiation complete", class_name);
    
    // Restore old class scope
    env->class_scope = old_class_scope;
    
    // Pop the class scope
    puppet_scope_t *old_scope = puppet_scope_pop(env);
    puppet_scope_destroy(old_scope);
}

/*
 * ===========================================================================
 * CLASS DEFINITION MANAGEMENT
 * ===========================================================================
 */

/**
 * @brief Register a class definition for later instantiation
 *
 * @param env Execution environment
 * @param class_def Class definition statement
 * @return 0 on success, -1 on error
 */
int puppet_register_class_def(puppet_env_t *env, puppet_stmt_t *class_def) {
    if (!env || !class_def || class_def->type != PUPPET_STMT_CLASS_DEF) return -1;
    
    // Expand class definition array if needed
    if (env->class_def_count >= env->class_def_capacity) {
        env->class_def_capacity *= 2;
        env->class_definitions = puppet_realloc(env->class_definitions, 
            env->class_def_capacity * sizeof(puppet_stmt_t*));
        if (!env->class_definitions) {
            return -1;
        }
    }
    
    // Add class definition to registry
    env->class_definitions[env->class_def_count] = class_def;
    env->class_def_count++;
    
    return 0;
}

/**
 * @brief Find a class definition by name
 *
 * @param env Execution environment
 * @param class_name Class name to find
 * @return Class definition statement or NULL if not found
 */
puppet_stmt_t *puppet_find_class_def(puppet_env_t *env, const char *class_name) {
    if (!env || !class_name) return NULL;
    
    for (size_t i = 0; i < env->class_def_count; i++) {
        puppet_stmt_t *class_def = env->class_definitions[i];
        if (class_def && class_def->type == PUPPET_STMT_CLASS_DEF) {
            const char *def_name = class_def->data.class_def.name.data;
            if (strcmp(def_name, class_name) == 0) {
                return class_def;
            }
        }
    }
    
    return NULL;
}

/*
 * ===========================================================================
 * ENHANCED VARIABLE SYSTEM IMPLEMENTATION
 * ===========================================================================
 */

/**
 * @brief Enhanced variable lookup with full chain traversal
 *
 * Implements the complete Puppet variable lookup chain:
 * 1. Local scope (current function/class)
 * 2. Class scope (if inside class)
 * 3. Node scope (node-specific variables)  
 * 4. Global scope (top-level variables)
 * 5. Data providers (Hiera, external data sources)
 *
 * @param env Execution environment
 * @param name Variable name to look up
 * @return Variable value or NULL if not found
 */
puppet_value_t *puppet_variable_lookup_chain(puppet_env_t *env, const char *name) {
    if (!env || !name) return NULL;
    
    puppet_value_t *value = NULL;
    
    // 1. Local scope (current scope, non-recursive)
    value = puppet_scope_get_var(env->current_scope, name, false);
    if (value) return value;
    
    // 2. Class scope (if we're inside a class)
    if (env->class_scope && env->class_scope != env->current_scope) {
        value = puppet_scope_get_var(env->class_scope, name, false);
        if (value) return value;
    }
    
    // 3. Node scope (node-specific variables)
    if (env->node_scope && env->node_scope != env->current_scope) {
        value = puppet_scope_get_var(env->node_scope, name, false);
        if (value) return value;
    }
    
    // 4. Global scope (top-level variables)
    if (env->global_scope != env->current_scope) {
        value = puppet_scope_get_var(env->global_scope, name, false);
        if (value) return value;
    }
    
    // 5. Facts lookup (check for direct fact access)
    if (env->facts_db) {
        value = puppet_facts_get(env, name);
        if (value) return value;
    }
    
    // 6. Data providers (Hiera, external data sources)
    for (size_t i = 0; i < env->data_provider_count; i++) {
        puppet_data_provider_t *provider = env->data_providers[i];
        if (provider && provider->lookup) {
            value = provider->lookup(name, env, provider->data);
            if (value) return value;
        }
    }
    
    // 7. Not found
    return NULL;
}

/**
 * @brief Look up variable in specific scope type
 *
 * @param env Execution environment
 * @param name Variable name
 * @param scope Scope type to search
 * @return Variable value or NULL if not found
 */
puppet_value_t *puppet_variable_lookup_scoped(puppet_env_t *env, const char *name, puppet_var_scope_t scope) {
    if (!env || !name) return NULL;
    
    switch (scope) {
        case PUPPET_VAR_LOCAL:
            return puppet_scope_get_var(env->current_scope, name, false);
            
        case PUPPET_VAR_CLASS:
            return env->class_scope ? 
                puppet_scope_get_var(env->class_scope, name, false) : NULL;
                
        case PUPPET_VAR_NODE:
            return env->node_scope ? 
                puppet_scope_get_var(env->node_scope, name, false) : NULL;
                
        case PUPPET_VAR_GLOBAL:
            return puppet_scope_get_var(env->global_scope, name, false);
            
        case PUPPET_VAR_FACT:
            // Facts would be handled by a fact provider
            // For now, fall through to data providers
            for (size_t i = 0; i < env->data_provider_count; i++) {
                puppet_data_provider_t *provider = env->data_providers[i];
                if (provider && provider->lookup) {
                    puppet_value_t *value = provider->lookup(name, env, provider->data);
                    if (value) return value;
                }
            }
            return NULL;
            
        default:
            return NULL;
    }
}

/**
 * @brief Set variable in specific scope
 *
 * @param env Execution environment
 * @param name Variable name
 * @param value Variable value
 * @param scope Target scope type
 */
void puppet_env_set_scoped_var(puppet_env_t *env, const char *name, puppet_value_t *value, puppet_var_scope_t scope) {
    if (!env || !name) return;
    
    switch (scope) {
        case PUPPET_VAR_LOCAL:
            puppet_scope_set_var(env->current_scope, name, value);
            break;
            
        case PUPPET_VAR_CLASS:
            if (env->class_scope) {
                puppet_scope_set_var(env->class_scope, name, value);
            } else {
                // Create class scope if it doesn't exist
                env->class_scope = puppet_scope_create(env->global_scope, "class");
                puppet_scope_set_var(env->class_scope, name, value);
            }
            break;
            
        case PUPPET_VAR_NODE:
            puppet_scope_set_var(env->node_scope, name, value);
            break;
            
        case PUPPET_VAR_GLOBAL:
            puppet_scope_set_var(env->global_scope, name, value);
            break;
            
        case PUPPET_VAR_FACT:
            // Facts are typically read-only, but we could support setting
            // them in node scope for now
            puppet_scope_set_var(env->node_scope, name, value);
            break;
    }
}

/*
 * ===========================================================================
 * DATA PROVIDER MANAGEMENT
 * ===========================================================================
 */

/**
 * @brief Register a data provider (Hiera, etc.)
 *
 * @param env Execution environment
 * @param provider Data provider to register
 * @return 0 on success, -1 on error
 */
int puppet_register_data_provider(puppet_env_t *env, puppet_data_provider_t *provider) {
    if (!env || !provider) return -1;
    
    // Expand provider array if needed
    if (env->data_provider_count >= env->data_provider_capacity) {
        env->data_provider_capacity *= 2;
        env->data_providers = puppet_realloc(env->data_providers, 
            env->data_provider_capacity * sizeof(puppet_data_provider_t*));
        if (!env->data_providers) {
            return -1;
        }
    }
    
    // Add provider to array
    env->data_providers[env->data_provider_count] = provider;
    env->data_provider_count++;
    
    return 0;
}

/**
 * @brief Unregister data provider by name
 *
 * @param env Execution environment
 * @param name Provider name to remove
 */
void puppet_unregister_data_provider(puppet_env_t *env, const char *name) {
    if (!env || !name) return;
    
    for (size_t i = 0; i < env->data_provider_count; i++) {
        puppet_data_provider_t *provider = env->data_providers[i];
        if (provider && provider->name && strcmp(provider->name, name) == 0) {
            // Clean up provider
            if (provider->cleanup) {
                provider->cleanup(provider->data);
            }
            puppet_free(provider->name);
            puppet_free(provider);
            
            // Shift remaining providers down
            for (size_t j = i; j < env->data_provider_count - 1; j++) {
                env->data_providers[j] = env->data_providers[j + 1];
            }
            env->data_provider_count--;
            break;
        }
    }
}

/**
 * @brief Get data provider by name
 *
 * @param env Execution environment
 * @param name Provider name to find
 * @return Provider pointer or NULL if not found
 */
puppet_data_provider_t *puppet_get_data_provider(puppet_env_t *env, const char *name) {
    if (!env || !name) return NULL;
    
    for (size_t i = 0; i < env->data_provider_count; i++) {
        puppet_data_provider_t *provider = env->data_providers[i];
        if (provider && provider->name && strcmp(provider->name, name) == 0) {
            return provider;
        }
    }
    
    return NULL;
}

/*
 * ===========================================================================
 * FACTS DATABASE IMPLEMENTATION
 * ===========================================================================
 */

#include "puppet_json_parser.h"

puppet_facts_db_t *puppet_facts_db_create(void) {
    puppet_facts_db_t *facts_db = puppet_calloc(1, sizeof(puppet_facts_db_t));
    facts_db->node_count = 0;
    facts_db->node_capacity = 4;
    facts_db->nodes = puppet_calloc(facts_db->node_capacity, sizeof(puppet_node_facts_t));
    facts_db->node_index = puppet_calloc(1, sizeof(puppet_hash_t));
    facts_db->node_index->bucket_count = 16;
    facts_db->node_index->buckets = puppet_calloc(facts_db->node_index->bucket_count, sizeof(puppet_hash_entry_t*));
    facts_db->current_node = NULL;
    
    return facts_db;
}

void puppet_facts_db_destroy(puppet_facts_db_t *facts_db) {
    if (!facts_db) return;
    
    // Clean up nodes
    for (size_t i = 0; i < facts_db->node_count; i++) {
        puppet_node_facts_t *node = &facts_db->nodes[i];
        puppet_free(node->certname);
        puppet_free(node->environment);
        
        // Clean up facts hash table
        if (node->facts) {
            for (size_t j = 0; j < node->facts->bucket_count; j++) {
                puppet_hash_entry_t *entry = node->facts->buckets[j];
                while (entry) {
                    puppet_hash_entry_t *next = entry->next;
                    puppet_string_free(entry->key);
                    puppet_value_destroy(entry->value);
                    puppet_free(entry);
                    entry = next;
                }
            }
            puppet_free(node->facts->buckets);
            puppet_free(node->facts);
        }
    }
    puppet_free(facts_db->nodes);
    
    // Clean up node index
    for (size_t i = 0; i < facts_db->node_index->bucket_count; i++) {
        puppet_hash_entry_t *entry = facts_db->node_index->buckets[i];
        while (entry) {
            puppet_hash_entry_t *next = entry->next;
            puppet_string_free(entry->key);
            puppet_value_destroy(entry->value); // Safe to destroy index values
            puppet_free(entry);
            entry = next;
        }
    }
    puppet_free(facts_db->node_index->buckets);
    puppet_free(facts_db->node_index);
    
    puppet_free(facts_db->current_node);
    puppet_free(facts_db);
}

static int puppet_facts_db_add_node(puppet_facts_db_t *facts_db, const char *certname, const char *environment) {
    if (!facts_db || !certname) return -1;
    
    // Expand array if needed
    if (facts_db->node_count >= facts_db->node_capacity) {
        facts_db->node_capacity *= 2;
        facts_db->nodes = puppet_realloc(facts_db->nodes, facts_db->node_capacity * sizeof(puppet_node_facts_t));
    }
    
    // Initialize new node
    puppet_node_facts_t *node = &facts_db->nodes[facts_db->node_count];
    node->certname = puppet_strdup(certname);
    node->environment = environment ? puppet_strdup(environment) : NULL;
    node->facts = puppet_calloc(1, sizeof(puppet_hash_t));
    node->facts->bucket_count = 16;
    node->facts->buckets = puppet_calloc(node->facts->bucket_count, sizeof(puppet_hash_entry_t*));
    
    // Add to index (store array index as number)
    puppet_value_t *index_value = puppet_value_create_number((double)facts_db->node_count);
    puppet_hash_set(facts_db->node_index, certname, strlen(certname), index_value);
    
    facts_db->node_count++;
    return 0;
}

static void puppet_facts_add_fact(puppet_node_facts_t *node, const char *fact_name, json_value_t *json_val) {
    if (!node || !fact_name || !json_val) return;
    
    puppet_value_t *puppet_val = json_value_to_puppet_value(json_val);
    puppet_hash_set(node->facts, fact_name, strlen(fact_name), puppet_val);
}

static void puppet_facts_process_object(puppet_node_facts_t *node, const char *prefix, json_value_t *obj) {
    if (!node || !obj || obj->type != JSON_VALUE_OBJECT) return;
    
    for (size_t i = 0; i < obj->data.object.count; i++) {
        const char *key = obj->data.object.keys[i];
        json_value_t *value = obj->data.object.values[i];
        
        // Create fully qualified fact name
        char *fact_name;
        if (prefix && strlen(prefix) > 0) {
            size_t len = strlen(prefix) + strlen(key) + 2; // +2 for '.' and '\0'
            fact_name = puppet_malloc(len);
            snprintf(fact_name, len, "%s.%s", prefix, key);
        } else {
            fact_name = puppet_strdup(key);
        }
        
        if (value->type == JSON_VALUE_OBJECT) {
            // Recursively process nested objects
            puppet_facts_process_object(node, fact_name, value);
        } else {
            // Add leaf fact
            puppet_facts_add_fact(node, fact_name, value);
        }
        
        // Also add top-level key for direct access (e.g., $os instead of just $os.name)
        if (!prefix || strlen(prefix) == 0) {
            puppet_facts_add_fact(node, key, value);
        }
        
        puppet_free(fact_name);
    }
}

static int puppet_facts_load_facter_format(puppet_facts_db_t *facts_db, json_value_t *root) {
    if (!facts_db || !root || root->type != JSON_VALUE_OBJECT) return -1;
    
    // Facter format: single object with facts
    // Determine node name from hostname or use "localhost"
    json_value_t *hostname_val = json_object_get(root, "hostname");
    json_value_t *networking = json_object_get(root, "networking");
    json_value_t *fqdn_val = networking ? json_object_get(networking, "fqdn") : NULL;
    
    const char *node_name = "localhost";
    if (fqdn_val && fqdn_val->type == JSON_VALUE_STRING) {
        node_name = fqdn_val->data.string_value;
    } else if (hostname_val && hostname_val->type == JSON_VALUE_STRING) {
        node_name = hostname_val->data.string_value;
    }
    
    // Add node
    if (puppet_facts_db_add_node(facts_db, node_name, NULL) < 0) {
        return -1;
    }
    
    puppet_node_facts_t *node = &facts_db->nodes[facts_db->node_count - 1];
    
    // Process all facts
    puppet_facts_process_object(node, NULL, root);
    
    // Set as current node
    puppet_facts_db_set_current_node(facts_db, node_name);
    
    return 0;
}

static int puppet_facts_load_puppetdb_format(puppet_facts_db_t *facts_db, json_value_t *root) {
    if (!facts_db || !root || root->type != JSON_VALUE_ARRAY) return -1;
    
    // PuppetDB format: array of node objects
    for (size_t i = 0; i < root->data.array.count; i++) {
        json_value_t *node_obj = root->data.array.elements[i];
        if (node_obj->type != JSON_VALUE_OBJECT) continue;
        
        json_value_t *certname = json_object_get(node_obj, "certname");
        json_value_t *environment = json_object_get(node_obj, "environment");
        json_value_t *facts = json_object_get(node_obj, "facts");
        
        if (!certname || certname->type != JSON_VALUE_STRING || !facts) continue;
        
        const char *node_name = certname->data.string_value;
        const char *env_name = (environment && environment->type == JSON_VALUE_STRING) ? 
                               environment->data.string_value : NULL;
        
        // Add node
        if (puppet_facts_db_add_node(facts_db, node_name, env_name) < 0) {
            continue;
        }
        
        puppet_node_facts_t *node = &facts_db->nodes[facts_db->node_count - 1];
        
        // Process facts object
        puppet_facts_process_object(node, NULL, facts);
    }
    
    return 0;
}

int puppet_facts_db_load_file(puppet_facts_db_t *facts_db, const char *filepath) {
    if (!facts_db || !filepath) return -1;
    
    json_value_t *root = json_parse_file(filepath);
    if (!root) {
        puppet_error("Failed to parse facts file: %s", filepath);
        return -1;
    }
    
    int result;
    if (root->type == JSON_VALUE_ARRAY) {
        // PuppetDB format
        result = puppet_facts_load_puppetdb_format(facts_db, root);
    } else if (root->type == JSON_VALUE_OBJECT) {
        // Facter format
        result = puppet_facts_load_facter_format(facts_db, root);
    } else {
        puppet_error("Unsupported facts file format");
        result = -1;
    }
    
    json_value_destroy(root);
    return result;
}

int puppet_facts_db_set_current_node(puppet_facts_db_t *facts_db, const char *certname) {
    if (!facts_db || !certname) return -1;
    
    // Find node index
    puppet_value_t *index_value = puppet_hash_get(facts_db->node_index, certname, strlen(certname));
    if (!index_value || index_value->type != PUPPET_VALUE_NUMBER) {
        puppet_debug("Node '%s' not found in facts database", certname);
        return -1;
    }

    size_t index = (size_t)index_value->data.number;
    if (index >= facts_db->node_count) {
        puppet_warn("Invalid node index for '%s'", certname);
        return -1;
    }
    
    // Set current node
    puppet_free(facts_db->current_node);
    facts_db->current_node = puppet_strdup(certname);
    
    return 0;
}

puppet_value_t *puppet_facts_get(puppet_env_t *env, const char *fact_name) {
    if (!env || !env->facts_db || !fact_name) {
        return NULL;
    }
    
    puppet_facts_db_t *facts_db = env->facts_db;
    if (!facts_db->current_node) {
        return NULL;
    }
    
    // Find current node by index
    puppet_value_t *index_value = puppet_hash_get(facts_db->node_index, facts_db->current_node, strlen(facts_db->current_node));
    if (!index_value || index_value->type != PUPPET_VALUE_NUMBER) {
        return NULL;
    }
    
    size_t index = (size_t)index_value->data.number;
    if (index >= facts_db->node_count) {
        return NULL;
    }
    
    puppet_node_facts_t *node = &facts_db->nodes[index];
    
    // Look up fact
    puppet_value_t *fact_value = puppet_hash_get(node->facts, fact_name, strlen(fact_name));
    if (!fact_value) {
        return NULL;
    }
    
    // Return copy to avoid double-free
    switch (fact_value->type) {
        case PUPPET_VALUE_UNDEF:
            return puppet_value_create_undef();
        case PUPPET_VALUE_BOOL:
            return puppet_value_create_bool(fact_value->data.boolean);
        case PUPPET_VALUE_NUMBER:
            return puppet_value_create_number(fact_value->data.number);
        case PUPPET_VALUE_STRING:
            return puppet_value_create_string(fact_value->data.string.data, fact_value->data.string.len);
        default:
            return puppet_value_create_undef();
    }
}

int puppet_env_set_facts_db(puppet_env_t *env, puppet_facts_db_t *facts_db) {
    if (!env) return -1;
    
    if (env->facts_db) {
        puppet_facts_db_destroy(env->facts_db);
    }
    
    env->facts_db = facts_db;
    return 0;
}