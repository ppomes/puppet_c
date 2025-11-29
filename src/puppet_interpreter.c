#include "puppet_interpreter.h"
#include "puppet_erb.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

puppet_env_t *puppet_env_create(void) {
    puppet_env_t *env = calloc(1, sizeof(puppet_env_t));
    env->global_scope = puppet_scope_create(NULL, "global");
    env->current_scope = env->global_scope;
    env->stack_capacity = 16;
    env->scope_stack = calloc(env->stack_capacity, sizeof(puppet_scope_t*));
    env->stack_depth = 0;
    return env;
}

void puppet_env_destroy(puppet_env_t *env) {
    if (!env) return;
    
    // Clean up scope stack
    while (env->stack_depth > 0) {
        puppet_scope_pop(env);
    }
    
    puppet_scope_destroy(env->global_scope);
    free(env->scope_stack);
    free(env);
}

puppet_scope_t *puppet_scope_create(puppet_scope_t *parent, const char *name) {
    puppet_scope_t *scope = calloc(1, sizeof(puppet_scope_t));
    scope->parent = parent;
    scope->name = puppet_string_create(name ? name : "");
    scope->variables = calloc(1, sizeof(puppet_hash_t));
    scope->variables->bucket_count = 16;
    scope->variables->buckets = calloc(scope->variables->bucket_count, sizeof(puppet_hash_entry_t*));
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
            free(entry);
            entry = next;
        }
    }
    free(scope->variables->buckets);
    free(scope->variables);
    puppet_string_free(scope->name);
    free(scope);
}

void puppet_scope_push(puppet_env_t *env, puppet_scope_t *scope) {
    if (env->stack_depth >= env->stack_capacity) {
        env->stack_capacity *= 2;
        env->scope_stack = realloc(env->scope_stack, env->stack_capacity * sizeof(puppet_scope_t*));
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
            
        default:
            printf("Unimplemented expression type: %d\n", expr->type);
            return puppet_value_create_undef();
    }
}

puppet_value_t *puppet_eval_variable(const char *name, puppet_env_t *env) {
    puppet_value_t *value = puppet_env_get_var(env, name);
    
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
            
        case PUPPET_STMT_RESOURCE:
            printf("Executing resource: %s\n", stmt->data.resource.type.data);
            // TODO: Implement resource execution
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
    puppet_env_set_var(env, var, val);
    
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
    
    // Create a new scope for the class
    puppet_scope_t *class_scope = puppet_scope_create(env->current_scope, class_name);
    puppet_scope_push(env, class_scope);
    
    // Execute class body
    puppet_exec_stmt_list(&class_stmt->data.class_def.body, env);
    
    // Pop the class scope
    puppet_scope_t *old_scope = puppet_scope_pop(env);
    puppet_scope_destroy(old_scope);
}

void puppet_exec_program(puppet_program_t *program, puppet_env_t *env) {
    puppet_exec_stmt_list(&program->statements, env);
}