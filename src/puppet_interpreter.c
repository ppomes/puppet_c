#include "puppet_interpreter.h"
#include "puppet_erb.h"
#include "puppet_loader.h"
#include "puppet_memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
    
    return env;
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
    
    puppet_free(env->scope_stack);
    puppet_free(env->node_name);
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
            
            if (strcmp(func_name, "template") == 0) {
                return puppet_func_template(&expr->data.funcall.args, env);
            } else {
                printf("Error: Unknown function: %s\n", func_name);
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
            printf("Unimplemented expression type: %d\n", expr->type);
            return puppet_value_create_undef();
    }
}

puppet_value_t *puppet_eval_variable(const char *name, puppet_env_t *env) {
    // Use enhanced lookup chain instead of simple scope lookup
    puppet_value_t *value = puppet_variable_lookup_chain(env, name);
    
    if (!value) {
        printf("Warning: Undefined variable: %s\n", name);
        return puppet_value_create_undef();
    }
    
    // Return a copy to avoid double-free
    switch (value->type) {
        case PUPPET_VALUE_UNDEF:
            return puppet_value_create_undef();
        case PUPPET_VALUE_BOOL:
            return puppet_value_create_bool(value->data.boolean);
        case PUPPET_VALUE_NUMBER:
            return puppet_value_create_number(value->data.number);
        case PUPPET_VALUE_STRING:
            return puppet_value_create_string(value->data.string.data, value->data.string.len);
        default:
            return puppet_value_create_undef();
    }
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
    
    printf("Warning: Unsupported binary operation\n");
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
    
    printf("Warning: Unsupported unary operation\n");
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
            
        case PUPPET_STMT_RESOURCE:
            printf("Executing resource: %s\n", stmt->data.resource.type.data);
            
            // Evaluate resource titles
            for (size_t i = 0; i < stmt->data.resource.instance_count; i++) {
                puppet_resource_instance_t *instance = &stmt->data.resource.instances[i];
                if (instance->title) {
                    puppet_value_t *title_val = puppet_eval_expr(instance->title, env);
                    const char *title_str = puppet_value_to_string(title_val);
                    printf("  Title: %s\n", title_str);
                    // Don't free title_str - it's internal to title_val
                    puppet_value_destroy(title_val);
                    
                    // Show attributes for this instance
                    for (size_t j = 0; j < instance->attr_count; j++) {
                        printf("    %s => ", instance->attributes[j].name.data);
                        puppet_value_t *attr_val = puppet_eval_expr(instance->attributes[j].value, env);
                        const char *attr_str = puppet_value_to_string(attr_val);
                        printf("%s\n", attr_str);
                        // Don't free attr_str - it's internal to attr_val
                        puppet_value_destroy(attr_val);
                    }
                }
            }
            break;
            
        default:
            printf("Unimplemented statement type: %d\n", stmt->type);
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
    printf("Set $%s = ", var);
    switch (val->type) {
        case PUPPET_VALUE_UNDEF:
            printf("undef");
            break;
        case PUPPET_VALUE_BOOL:
            printf("%s", val->data.boolean ? "true" : "false");
            break;
        case PUPPET_VALUE_STRING:
            printf("\"%s\"", val->data.string.data);
            break;
        case PUPPET_VALUE_NUMBER:
            printf("%.6g", val->data.number);
            break;
        default:
            printf("(complex value)");
            break;
    }
    printf("\n");
}

void puppet_exec_class_def(puppet_stmt_t *class_stmt, puppet_env_t *env) {
    const char *class_name = class_stmt->data.class_def.name.data;
    printf("Defining class: %s\n", class_name);
    
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
            printf("Set class parameter $%s = ", param_name);
            switch (default_val->type) {
                case PUPPET_VALUE_BOOL:
                    printf("%s", default_val->data.boolean ? "true" : "false");
                    break;
                case PUPPET_VALUE_NUMBER:
                    printf("%.6g", default_val->data.number);
                    break;
                case PUPPET_VALUE_STRING:
                    printf("\"%s\"", default_val->data.string.data);
                    break;
                default:
                    printf("(complex value)");
                    break;
            }
            printf(" (default)\n");
        } else {
            // Set parameter to undef if no default provided
            puppet_value_t *undef_val = puppet_value_create_undef();
            puppet_scope_set_var(class_scope, param_name, undef_val);
            printf("Set class parameter $%s = undef (no default)\n", param_name);
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
        printf("Warning: Include statements require a module loader to be configured\n");
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
                printf("Warning: Failed to include class '%s'\n", class_name);
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
        printf("Executing node: %s\n", node_name);
        
        /* Switch to node-specific facts if available */
        if (env->facts_db) {
            if (puppet_facts_db_set_current_node(env->facts_db, node_name) == 0) {
                printf("Using facts for node: %s\n", node_name);
            } else {
                printf("No facts found for node %s, using default facts\n", node_name);
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
            printf("Skipping node: %s (looking for %s)\n", node_name, env->node_name);
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

void puppet_exec_class_instance(puppet_stmt_t *class_instance_stmt, puppet_env_t *env) {
    if (!class_instance_stmt || class_instance_stmt->type != PUPPET_STMT_CLASS_INSTANCE) return;
    
    const char *class_name = class_instance_stmt->data.class_instance.class_name.data;
    printf("Instantiating class: %s\n", class_name);
    
    // Find the class definition
    puppet_stmt_t *class_def = puppet_find_class_def(env, class_name);
    if (!class_def) {
        printf("Error: Class '%s' not found\n", class_name);
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
                printf("Set class parameter $%s = ", param_name);
                break;
            }
        }
        
        // If not provided, use default value
        if (!found_arg && param->default_value) {
            param_value = puppet_eval_expr(param->default_value, env);
            printf("Set class parameter $%s = ", param_name);
        } else if (!found_arg) {
            param_value = puppet_value_create_undef();
            printf("Set class parameter $%s = undef (no default)\n", param_name);
        }
        
        // Set the parameter value in class scope
        if (param_value) {
            puppet_scope_set_var(class_scope, param_name, param_value);
            
            if (found_arg || param->default_value) {
                switch (param_value->type) {
                    case PUPPET_VALUE_BOOL:
                        printf("%s", param_value->data.boolean ? "true" : "false");
                        break;
                    case PUPPET_VALUE_NUMBER:
                        printf("%.6g", param_value->data.number);
                        break;
                    case PUPPET_VALUE_STRING:
                        printf("\"%s\"", param_value->data.string.data);
                        break;
                    default:
                        printf("(complex value)");
                        break;
                }
                printf("%s\n", found_arg ? " (provided)" : " (default)");
            }
        }
    }
    
    // Execute the class body
    printf("Executing class body for: %s\n", class_name);
    puppet_exec_stmt_list(&class_def->data.class_def.body, env);
    
    printf("Class %s instantiation complete\n", class_name);
    
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
        printf("Error: Failed to parse facts file: %s\n", filepath);
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
        printf("Error: Unsupported facts file format\n");
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
        printf("Warning: Node '%s' not found in facts database\n", certname);
        return -1;
    }
    
    size_t index = (size_t)index_value->data.number;
    if (index >= facts_db->node_count) {
        printf("Warning: Invalid node index for '%s'\n", certname);
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