/**
 * @file puppet_ast.c
 * @brief Implementation of AST data structures and operations
 *
 * This file implements the core data structures for the Puppet AST,
 * including creation, manipulation, and destruction functions for:
 * - Values (strings, numbers, arrays, hashes)
 * - Expressions (operators, function calls, conditionals)  
 * - Statements (resources, classes, conditionals)
 * - Support structures (parameter lists, etc.)
 *
 * Memory Management Strategy:
 * - All structures use explicit allocation/deallocation
 * - Values own their contained data (strings, arrays, etc.)
 * - Expressions and statements form ownership trees
 * - Destroy functions recursively free all owned data
 */

#include "puppet_ast.h"
#include "puppet_memory.h"
#include <stdlib.h>
#include <string.h>

/*
 * ===========================================================================
 * STRING OPERATIONS
 * ===========================================================================
 */

puppet_string_t puppet_string_create(const char *str) {
    puppet_string_t s;
    s.len = strlen(str);
    s.data = puppet_malloc(s.len + 1);
    strcpy(s.data, str);
    return s;
}

void puppet_string_free(puppet_string_t str) {
    puppet_free(str.data);
}


puppet_value_t *puppet_value_create_undef(void) {
    puppet_value_t *value = puppet_calloc(1, sizeof(puppet_value_t));
    if (!value) {
        return NULL;  /* Let caller handle failure */
    }
    value->type = PUPPET_VALUE_UNDEF;
    return value;
}

puppet_value_t *puppet_value_create_bool(bool val) {
    puppet_value_t *value = puppet_calloc(1, sizeof(puppet_value_t));
    if (!value) {
        return NULL;  /* Let caller handle failure */
    }
    value->type = PUPPET_VALUE_BOOL;
    value->data.boolean = val;
    return value;
}

puppet_value_t *puppet_value_create_string(const char *str, size_t len) {
    puppet_value_t *value = puppet_calloc(1, sizeof(puppet_value_t));
    value->type = PUPPET_VALUE_STRING;
    value->data.string.data = puppet_malloc(len + 1);
    memcpy(value->data.string.data, str, len);
    value->data.string.data[len] = '\0';
    value->data.string.len = len;
    return value;
}

puppet_value_t *puppet_value_create_number(double val) {
    puppet_value_t *value = puppet_calloc(1, sizeof(puppet_value_t));
    value->type = PUPPET_VALUE_NUMBER;
    value->data.number = val;
    return value;
}

puppet_value_t *puppet_value_create_array(void) {
    puppet_value_t *value = puppet_calloc(1, sizeof(puppet_value_t));
    value->type = PUPPET_VALUE_ARRAY;
    value->data.array = puppet_calloc(1, sizeof(puppet_array_t));
    value->data.array->capacity = 8;
    value->data.array->items = puppet_malloc(value->data.array->capacity * sizeof(puppet_value_t*));
    return value;
}

puppet_value_t *puppet_value_create_hash(void) {
    puppet_value_t *value = puppet_calloc(1, sizeof(puppet_value_t));
    value->type = PUPPET_VALUE_HASH;
    value->data.hash = puppet_calloc(1, sizeof(puppet_hash_t));
    value->data.hash->bucket_count = 16;
    value->data.hash->buckets = puppet_calloc(value->data.hash->bucket_count, sizeof(puppet_hash_entry_t*));
    return value;
}

void puppet_array_append(puppet_array_t *array, puppet_value_t *value) {
    if (array->count >= array->capacity) {
        array->capacity *= 2;
        array->items = puppet_realloc(array->items, array->capacity * sizeof(puppet_value_t*));
    }
    array->items[array->count++] = value;
}

static uint32_t hash_string(const char *str, size_t len) {
    uint32_t hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + str[i];
    }
    return hash;
}

void puppet_hash_set(puppet_hash_t *hash, const char *key, size_t key_len, puppet_value_t *value) {
    uint32_t hash_val = hash_string(key, key_len);
    size_t bucket = hash_val % hash->bucket_count;
    
    puppet_hash_entry_t *entry = hash->buckets[bucket];
    while (entry) {
        if (entry->key.len == key_len && memcmp(entry->key.data, key, key_len) == 0) {
            puppet_value_destroy(entry->value);
            entry->value = value;
            return;
        }
        entry = entry->next;
    }
    
    entry = puppet_malloc(sizeof(puppet_hash_entry_t));
    entry->key.data = puppet_malloc(key_len + 1);
    memcpy(entry->key.data, key, key_len);
    entry->key.data[key_len] = '\0';
    entry->key.len = key_len;
    entry->value = value;
    entry->next = hash->buckets[bucket];
    hash->buckets[bucket] = entry;
    hash->entry_count++;
}

puppet_value_t *puppet_hash_get(puppet_hash_t *hash, const char *key, size_t key_len) {
    uint32_t hash_val = hash_string(key, key_len);
    size_t bucket = hash_val % hash->bucket_count;
    
    puppet_hash_entry_t *entry = hash->buckets[bucket];
    while (entry) {
        if (entry->key.len == key_len && memcmp(entry->key.data, key, key_len) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    return NULL;
}

void puppet_value_destroy(puppet_value_t *value) {
    if (!value) return;
    
    switch (value->type) {
        case PUPPET_VALUE_STRING:
            puppet_string_free(value->data.string);
            break;
            
        case PUPPET_VALUE_ARRAY:
            if (value->data.array) {
                for (size_t i = 0; i < value->data.array->count; i++) {
                    puppet_value_destroy(value->data.array->items[i]);
                }
                puppet_free(value->data.array->items);
                puppet_free(value->data.array);
            }
            break;
            
        case PUPPET_VALUE_HASH:
            if (value->data.hash) {
                for (size_t i = 0; i < value->data.hash->bucket_count; i++) {
                    puppet_hash_entry_t *entry = value->data.hash->buckets[i];
                    while (entry) {
                        puppet_hash_entry_t *next = entry->next;
                        puppet_string_free(entry->key);
                        puppet_value_destroy(entry->value);
                        puppet_free(entry);
                        entry = next;
                    }
                }
                puppet_free(value->data.hash->buckets);
                puppet_free(value->data.hash);
            }
            break;
            
        case PUPPET_VALUE_REGEXP:
            puppet_string_free(value->data.regexp);
            break;
            
        case PUPPET_VALUE_TYPE:
            break;
            
        default:
            break;
    }
    
    puppet_free(value);
}

puppet_value_t *puppet_value_copy(puppet_value_t *value) {
    if (!value) return NULL;
    
    switch (value->type) {
        case PUPPET_VALUE_UNDEF:
            return puppet_value_create_undef();
            
        case PUPPET_VALUE_BOOL:
            return puppet_value_create_bool(value->data.boolean);
            
        case PUPPET_VALUE_STRING:
            return puppet_value_create_string(value->data.string.data, value->data.string.len);
            
        case PUPPET_VALUE_NUMBER:
            return puppet_value_create_number(value->data.number);
            
        case PUPPET_VALUE_ARRAY: {
            puppet_value_t *new_array = puppet_value_create_array();
            for (size_t i = 0; i < value->data.array->count; i++) {
                puppet_array_append(new_array->data.array, puppet_value_copy(value->data.array->items[i]));
            }
            return new_array;
        }
            
        case PUPPET_VALUE_HASH: {
            puppet_value_t *new_hash = puppet_value_create_hash();
            if (value->data.hash) {
                for (size_t i = 0; i < value->data.hash->bucket_count; i++) {
                    puppet_hash_entry_t *entry = value->data.hash->buckets[i];
                    while (entry) {
                        puppet_hash_set(new_hash->data.hash,
                                       entry->key.data, entry->key.len,
                                       puppet_value_copy(entry->value));
                        entry = entry->next;
                    }
                }
            }
            return new_hash;
        }
            
        default:
            return NULL;
    }
}

puppet_expr_t *puppet_expr_create_value(puppet_value_t *value) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_VALUE;
    expr->data.value = value;
    return expr;
}

puppet_expr_t *puppet_expr_create_variable(const char *name) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_VARIABLE;
    expr->data.variable.data = puppet_strdup(name);
    expr->data.variable.len = strlen(name);
    return expr;
}

puppet_expr_t *puppet_expr_create_binop(puppet_binop_t op, puppet_expr_t *left, puppet_expr_t *right) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_BINOP;
    expr->data.binop.op = op;
    expr->data.binop.left = left;
    expr->data.binop.right = right;
    return expr;
}

puppet_expr_t *puppet_expr_create_unop(puppet_unop_t op, puppet_expr_t *operand) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_UNOP;
    expr->data.unop.op = op;
    expr->data.unop.expr = operand;
    return expr;
}

void puppet_expr_destroy(puppet_expr_t *expr) {
    if (!expr) return;
    
    switch (expr->type) {
        case PUPPET_EXPR_VALUE:
            puppet_value_destroy(expr->data.value);
            break;
            
        case PUPPET_EXPR_VARIABLE:
            puppet_string_free(expr->data.variable);
            break;
            
        case PUPPET_EXPR_BINOP:
            puppet_expr_destroy(expr->data.binop.left);
            puppet_expr_destroy(expr->data.binop.right);
            break;
            
        case PUPPET_EXPR_UNOP:
            puppet_expr_destroy(expr->data.unop.expr);
            break;
            
        case PUPPET_EXPR_FUNCALL:
            puppet_string_free(expr->data.funcall.name);
            for (size_t i = 0; i < expr->data.funcall.args.count; i++) {
                puppet_expr_destroy(expr->data.funcall.args.exprs[i]);
            }
            puppet_free(expr->data.funcall.args.exprs);
            if (expr->data.funcall.lambda) {
                puppet_free(expr->data.funcall.lambda);
            }
            break;
            
        case PUPPET_EXPR_INDEX:
            puppet_expr_destroy(expr->data.index.object);
            puppet_expr_destroy(expr->data.index.index);
            break;
            
        case PUPPET_EXPR_DOT:
            puppet_expr_destroy(expr->data.dot.object);
            puppet_string_free(expr->data.dot.field);
            break;
            
        case PUPPET_EXPR_CONDITIONAL:
            puppet_expr_destroy(expr->data.conditional.condition);
            puppet_expr_destroy(expr->data.conditional.then_expr);
            puppet_expr_destroy(expr->data.conditional.else_expr);
            break;
            
        case PUPPET_EXPR_LAMBDA:
            if (expr->data.lambda) {
                for (size_t i = 0; i < expr->data.lambda->params.count; i++) {
                    puppet_string_free(expr->data.lambda->params.params[i].name);
                    puppet_value_destroy(expr->data.lambda->params.params[i].type_constraint);
                    puppet_expr_destroy(expr->data.lambda->params.params[i].default_value);
                }
                puppet_free(expr->data.lambda->params.params);
                for (size_t i = 0; i < expr->data.lambda->body.count; i++) {
                    puppet_expr_destroy(expr->data.lambda->body.exprs[i]);
                }
                puppet_free(expr->data.lambda->body.exprs);
                puppet_free(expr->data.lambda);
            }
            break;
            
        case PUPPET_EXPR_RESOURCE_REF:
            puppet_string_free(expr->data.resource_ref.type);
            puppet_expr_destroy(expr->data.resource_ref.title);
            break;
            
        case PUPPET_EXPR_INTERPOLATED_STRING:
            for (size_t i = 0; i < expr->data.interpolated.count; i++) {
                if (expr->data.interpolated.parts)
                    puppet_string_free(expr->data.interpolated.parts[i]);
                if (expr->data.interpolated.exprs)
                    puppet_expr_destroy(expr->data.interpolated.exprs[i]);
            }
            puppet_free(expr->data.interpolated.parts);
            puppet_free(expr->data.interpolated.exprs);
            break;
    }
    
    puppet_free(expr);
}

puppet_stmt_t *puppet_stmt_create_resource(puppet_resource_decl_t decl) {
    puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
    stmt->type = PUPPET_STMT_RESOURCE;
    stmt->data.resource = decl;
    return stmt;
}

puppet_stmt_t *puppet_stmt_create_assignment(const char *var, puppet_expr_t *value) {
    puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
    stmt->type = PUPPET_STMT_ASSIGNMENT;
    stmt->data.assignment.variable.data = puppet_strdup(var);
    stmt->data.assignment.variable.len = strlen(var);
    stmt->data.assignment.value = value;
    return stmt;
}

static void puppet_resource_decl_destroy(puppet_resource_decl_t *decl) {
    puppet_string_free(decl->type);
    
    for (size_t i = 0; i < decl->instance_count; i++) {
        puppet_expr_destroy(decl->instances[i].title);
        for (size_t j = 0; j < decl->instances[i].attr_count; j++) {
            puppet_string_free(decl->instances[i].attributes[j].name);
            puppet_expr_destroy(decl->instances[i].attributes[j].value);
        }
        puppet_free(decl->instances[i].attributes);
    }
    puppet_free(decl->instances);
}

static void puppet_stmt_list_destroy(puppet_stmt_list_t *list) {
    for (size_t i = 0; i < list->count; i++) {
        puppet_stmt_destroy(list->stmts[i]);
    }
    puppet_free(list->stmts);
}

void puppet_stmt_destroy(puppet_stmt_t *stmt) {
    if (!stmt) return;
    
    switch (stmt->type) {
        case PUPPET_STMT_RESOURCE:
            puppet_resource_decl_destroy(&stmt->data.resource);
            break;
            
        case PUPPET_STMT_RESOURCE_DEFAULT:
            puppet_string_free(stmt->data.resource_default.type);
            for (size_t i = 0; i < stmt->data.resource_default.attr_count; i++) {
                puppet_string_free(stmt->data.resource_default.attributes[i].name);
                puppet_expr_destroy(stmt->data.resource_default.attributes[i].value);
            }
            puppet_free(stmt->data.resource_default.attributes);
            break;
            
        case PUPPET_STMT_RESOURCE_OVERRIDE:
            puppet_expr_destroy(stmt->data.resource_override.reference);
            for (size_t i = 0; i < stmt->data.resource_override.attr_count; i++) {
                puppet_string_free(stmt->data.resource_override.attributes[i].name);
                puppet_expr_destroy(stmt->data.resource_override.attributes[i].value);
            }
            puppet_free(stmt->data.resource_override.attributes);
            break;
            
        case PUPPET_STMT_RESOURCE_COLLECTOR:
            puppet_string_free(stmt->data.collector.type);
            puppet_expr_destroy(stmt->data.collector.search_expr);
            for (size_t i = 0; i < stmt->data.collector.attr_count; i++) {
                puppet_string_free(stmt->data.collector.attributes[i].name);
                puppet_expr_destroy(stmt->data.collector.attributes[i].value);
            }
            puppet_free(stmt->data.collector.attributes);
            break;
            
        case PUPPET_STMT_CLASS_DEF:
            puppet_string_free(stmt->data.class_def.name);
            for (size_t i = 0; i < stmt->data.class_def.params.count; i++) {
                puppet_string_free(stmt->data.class_def.params.params[i].name);
                puppet_value_destroy(stmt->data.class_def.params.params[i].type_constraint);
                puppet_expr_destroy(stmt->data.class_def.params.params[i].default_value);
            }
            puppet_free(stmt->data.class_def.params.params);
            if (stmt->data.class_def.inherits) {
                puppet_string_free(*stmt->data.class_def.inherits);
                puppet_free(stmt->data.class_def.inherits);
            }
            puppet_stmt_list_destroy(&stmt->data.class_def.body);
            break;
            
        case PUPPET_STMT_CLASS_INSTANCE:
            puppet_string_free(stmt->data.class_instance.class_name);
            for (size_t i = 0; i < stmt->data.class_instance.arg_count; i++) {
                puppet_string_free(stmt->data.class_instance.arguments[i].name);
                puppet_expr_destroy(stmt->data.class_instance.arguments[i].value);
            }
            puppet_free(stmt->data.class_instance.arguments);
            break;
            
        case PUPPET_STMT_DEFINE:
            puppet_string_free(stmt->data.define.name);
            for (size_t i = 0; i < stmt->data.define.params.count; i++) {
                puppet_string_free(stmt->data.define.params.params[i].name);
                puppet_value_destroy(stmt->data.define.params.params[i].type_constraint);
                puppet_expr_destroy(stmt->data.define.params.params[i].default_value);
            }
            puppet_free(stmt->data.define.params.params);
            puppet_stmt_list_destroy(&stmt->data.define.body);
            break;
            
        case PUPPET_STMT_NODE:
            puppet_string_free(stmt->data.node.name);
            puppet_stmt_list_destroy(&stmt->data.node.body);
            break;
            
        case PUPPET_STMT_IF:
            {
                puppet_if_branch_t *branch = stmt->data.if_stmt.branches;
                while (branch) {
                    puppet_if_branch_t *next = branch->next;
                    puppet_expr_destroy(branch->condition);
                    puppet_stmt_list_destroy(&branch->body);
                    puppet_free(branch);
                    branch = next;
                }
                if (stmt->data.if_stmt.else_body) {
                    puppet_stmt_list_destroy(stmt->data.if_stmt.else_body);
                    puppet_free(stmt->data.if_stmt.else_body);
                }
            }
            break;
            
        case PUPPET_STMT_UNLESS:
            puppet_expr_destroy(stmt->data.unless_stmt.condition);
            puppet_stmt_list_destroy(&stmt->data.unless_stmt.body);
            break;
            
        case PUPPET_STMT_CASE:
            puppet_expr_destroy(stmt->data.case_stmt.expr);
            for (size_t i = 0; i < stmt->data.case_stmt.when_count; i++) {
                puppet_expr_destroy(stmt->data.case_stmt.whens[i].test);
                puppet_stmt_list_destroy(&stmt->data.case_stmt.whens[i].body);
            }
            puppet_free(stmt->data.case_stmt.whens);
            if (stmt->data.case_stmt.default_body) {
                puppet_stmt_list_destroy(stmt->data.case_stmt.default_body);
                puppet_free(stmt->data.case_stmt.default_body);
            }
            break;
            
        case PUPPET_STMT_ASSIGNMENT:
            puppet_string_free(stmt->data.assignment.variable);
            puppet_expr_destroy(stmt->data.assignment.value);
            break;
            
        case PUPPET_STMT_APPEND:
            puppet_string_free(stmt->data.append.variable);
            puppet_expr_destroy(stmt->data.append.value);
            break;
            
        case PUPPET_STMT_FUNCTION_CALL:
            puppet_expr_destroy(stmt->data.expr);
            break;
            
        case PUPPET_STMT_RESOURCE_CHAIN:
            puppet_stmt_destroy(stmt->data.chain.left);
            puppet_stmt_destroy(stmt->data.chain.right);
            break;
            
        case PUPPET_STMT_INCLUDE:
        case PUPPET_STMT_REQUIRE:
        case PUPPET_STMT_CONTAIN:
        case PUPPET_STMT_TAG:
            for (size_t i = 0; i < stmt->data.names.count; i++) {
                puppet_expr_destroy(stmt->data.names.exprs[i]);
            }
            puppet_free(stmt->data.names.exprs);
            break;
            
        case PUPPET_STMT_IMPORT:
            puppet_string_free(stmt->data.import_pattern);
            break;
    }
    
    puppet_free(stmt);
}

void puppet_program_destroy(puppet_program_t *program) {
    if (!program) return;
    puppet_stmt_list_destroy(&program->statements);
    puppet_free(program);
}