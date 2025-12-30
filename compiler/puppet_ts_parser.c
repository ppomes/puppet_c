/**
 * @file puppet_ts_parser.c
 * @brief Tree-sitter based parser for Puppet language
 *
 * Converts tree-sitter parse tree to puppet_ast_t structures.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>
#include "puppet_ast.h"
#include "puppet_memory.h"
#include "puppet_ts_parser.h"

/* External: tree-sitter puppet language */
const TSLanguage *tree_sitter_puppet(void);

/* Forward declarations */
static puppet_stmt_t *convert_statement(TSNode node, const char *source);
static puppet_expr_t *convert_expression(TSNode node, const char *source);
static puppet_stmt_list_t convert_block(TSNode node, const char *source);
static puppet_lambda_t *convert_lambda(TSNode node, const char *source);

/*
 * ===========================================================================
 * HELPER FUNCTIONS
 * ===========================================================================
 */

/* Extract text from a node */
static char *node_text(TSNode node, const char *source) {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    size_t len = end - start;
    char *text = puppet_malloc(len + 1);
    memcpy(text, source + start, len);
    text[len] = '\0';
    return text;
}

/* Check if node matches a type name */
static int node_is(TSNode node, const char *type) {
    return strcmp(ts_node_type(node), type) == 0;
}

/* Find first named child of given type */
static TSNode find_child(TSNode node, const char *type) {
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        if (node_is(child, type)) {
            return child;
        }
    }
    return (TSNode){0};  /* Null node */
}

/* Get source location from node */
static puppet_location_t node_location(TSNode node) {
    TSPoint start = ts_node_start_point(node);
    puppet_location_t loc = {
        .filename = NULL,
        .line = start.row + 1,
        .column = start.column + 1
    };
    return loc;
}

/*
 * ===========================================================================
 * EXPRESSION CONVERSION
 * ===========================================================================
 */

/* Convert variable node: variable -> name */
static puppet_expr_t *convert_variable(TSNode node, const char *source) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_VARIABLE;
    expr->loc = node_location(node);

    TSNode name_node = find_child(node, "name");
    if (!ts_node_is_null(name_node)) {
        char *name = node_text(name_node, source);
        expr->data.variable = puppet_string_create(name);
        puppet_free(name);
    } else {
        /* Fallback: extract name from $varname */
        char *text = node_text(node, source);
        char *name = (text[0] == '$') ? text + 1 : text;
        expr->data.variable = puppet_string_create(name);
        puppet_free(text);
    }

    return expr;
}

/* Convert number literal */
static puppet_expr_t *convert_number(TSNode node, const char *source) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_VALUE;
    expr->loc = node_location(node);

    char *text = node_text(node, source);
    expr->data.value = puppet_value_create_number(atof(text));
    puppet_free(text);

    return expr;
}

/* Process escape sequences in a string */
static char *process_escape_sequences(const char *input, size_t *out_len) {
    size_t len = strlen(input);
    char *output = puppet_malloc(len + 1);
    size_t j = 0;

    for (size_t i = 0; i < len; i++) {
        if (input[i] == '\\' && i + 1 < len) {
            switch (input[i + 1]) {
                case 'n':  output[j++] = '\n'; i++; break;
                case 't':  output[j++] = '\t'; i++; break;
                case 'r':  output[j++] = '\r'; i++; break;
                case '\\': output[j++] = '\\'; i++; break;
                case '"':  output[j++] = '"';  i++; break;
                case '\'': output[j++] = '\''; i++; break;
                case '$':  output[j++] = '$';  i++; break;
                default:   output[j++] = input[i]; break;
            }
        } else {
            output[j++] = input[i];
        }
    }
    output[j] = '\0';
    if (out_len) *out_len = j;
    return output;
}

/* Convert string literal (single or double quoted) */
static puppet_expr_t *convert_string_literal(TSNode node, const char *source) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->loc = node_location(node);

    /* Check for interpolation in double-quoted strings */
    uint32_t child_count = ts_node_named_child_count(node);

    /* First, count actual interpolation nodes (not escape_sequence) */
    size_t interp_count = 0;
    if (child_count > 0 && node_is(node, "double_quoted_string")) {
        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_named_child(node, i);
            if (node_is(child, "interpolation")) {
                interp_count++;
            }
        }
    }

    /* Only use interpolated string path if there are actual interpolations */
    if (interp_count > 0) {
        /* Has interpolations - build interpolated string */
        expr->type = PUPPET_EXPR_INTERPOLATED_STRING;

        /* Allocate parts and expressions */
        expr->data.interpolated.parts = puppet_calloc(interp_count + 1, sizeof(puppet_string_t));
        expr->data.interpolated.exprs = puppet_calloc(interp_count, sizeof(puppet_expr_t*));
        expr->data.interpolated.count = interp_count;

        /* Build parts and expressions */
        uint32_t string_start = ts_node_start_byte(node) + 1; /* Skip opening quote */
        uint32_t string_end = ts_node_end_byte(node) - 1;     /* Skip closing quote */
        size_t part_idx = 0;

        for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_named_child(node, i);
            if (node_is(child, "interpolation")) {
                /* Extract text before this interpolation */
                uint32_t interp_start = ts_node_start_byte(child);
                if (interp_start > string_start) {
                    size_t part_len = interp_start - string_start;
                    char *part = puppet_malloc(part_len + 1);
                    memcpy(part, source + string_start, part_len);
                    part[part_len] = '\0';
                    expr->data.interpolated.parts[part_idx] = puppet_string_create(part);
                    puppet_free(part);
                } else {
                    expr->data.interpolated.parts[part_idx] = puppet_string_create("");
                }

                /* Convert the interpolated expression */
                TSNode interp_expr = ts_node_named_child_count(child) > 0 ?
                                     ts_node_named_child(child, 0) : child;
                expr->data.interpolated.exprs[part_idx] = convert_expression(interp_expr, source);

                string_start = ts_node_end_byte(child);
                part_idx++;
            }
        }

        /* Extract trailing text after last interpolation */
        if (string_start < string_end) {
            size_t part_len = string_end - string_start;
            char *part = puppet_malloc(part_len + 1);
            memcpy(part, source + string_start, part_len);
            part[part_len] = '\0';
            expr->data.interpolated.parts[part_idx] = puppet_string_create(part);
            puppet_free(part);
        } else {
            expr->data.interpolated.parts[part_idx] = puppet_string_create("");
        }

        return expr;
    }

    /* Simple string without interpolation */
    expr->type = PUPPET_EXPR_VALUE;

    char *raw = node_text(node, source);
    size_t len = strlen(raw);

    /* Remove surrounding quotes */
    if (len >= 2 && (raw[0] == '\'' || raw[0] == '"')) {
        char quote_char = raw[0];
        char *content = puppet_malloc(len - 1);
        memcpy(content, raw + 1, len - 2);
        content[len - 2] = '\0';

        /* Process escape sequences for double-quoted strings */
        if (quote_char == '"') {
            size_t processed_len;
            char *processed = process_escape_sequences(content, &processed_len);
            expr->data.value = puppet_value_create_string(processed, processed_len);
            puppet_free(processed);
        } else {
            /* Single-quoted strings: no escape processing */
            expr->data.value = puppet_value_create_string(content, len - 2);
        }
        puppet_free(content);
    } else {
        expr->data.value = puppet_value_create_string(raw, len);
    }
    puppet_free(raw);

    return expr;
}

/* Convert boolean literal */
static puppet_expr_t *convert_boolean(TSNode node, const char *source) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_VALUE;
    expr->loc = node_location(node);

    char *text = node_text(node, source);
    expr->data.value = puppet_value_create_bool(strcmp(text, "true") == 0);
    puppet_free(text);

    return expr;
}

/* Convert undef literal */
static puppet_expr_t *convert_undef(TSNode node, const char *source) {
    (void)source;
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_VALUE;
    expr->loc = node_location(node);
    expr->data.value = puppet_value_create_undef();
    return expr;
}

/* Convert binary expression */
static puppet_expr_t *convert_binary(TSNode node, const char *source) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_BINOP;
    expr->loc = node_location(node);

    /* Parse children to find operands and operator */
    uint32_t count = ts_node_child_count(node);
    puppet_expr_t *left = NULL, *right = NULL;
    puppet_binop_t op = PUPPET_OP_ADD;

    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *type = ts_node_type(child);

        if (ts_node_is_named(child)) {
            if (!left) {
                left = convert_expression(child, source);
            } else {
                right = convert_expression(child, source);
            }
        } else {
            /* Operator token */
            if (strcmp(type, "+") == 0) op = PUPPET_OP_ADD;
            else if (strcmp(type, "-") == 0) op = PUPPET_OP_SUB;
            else if (strcmp(type, "*") == 0) op = PUPPET_OP_MUL;
            else if (strcmp(type, "/") == 0) op = PUPPET_OP_DIV;
            else if (strcmp(type, "%") == 0) op = PUPPET_OP_MOD;
            else if (strcmp(type, "==") == 0) op = PUPPET_OP_EQ;
            else if (strcmp(type, "!=") == 0) op = PUPPET_OP_NE;
            else if (strcmp(type, "<") == 0) op = PUPPET_OP_LT;
            else if (strcmp(type, "<=") == 0) op = PUPPET_OP_LE;
            else if (strcmp(type, ">") == 0) op = PUPPET_OP_GT;
            else if (strcmp(type, ">=") == 0) op = PUPPET_OP_GE;
            else if (strcmp(type, "and") == 0) op = PUPPET_OP_AND;
            else if (strcmp(type, "or") == 0) op = PUPPET_OP_OR;
            else if (strcmp(type, "=~") == 0) op = PUPPET_OP_MATCH;
            else if (strcmp(type, "!~") == 0) op = PUPPET_OP_NOT_MATCH;
            else if (strcmp(type, "in") == 0) op = PUPPET_OP_IN;
            else if (strcmp(type, "<<") == 0) op = PUPPET_OP_LSHIFT;
            else if (strcmp(type, ">>") == 0) op = PUPPET_OP_RSHIFT;
        }
    }

    expr->data.binop.op = op;
    expr->data.binop.left = left;
    expr->data.binop.right = right;

    return expr;
}

/* Convert unary expression */
static puppet_expr_t *convert_unary(TSNode node, const char *source) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_UNOP;
    expr->loc = node_location(node);

    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *type = ts_node_type(child);

        if (ts_node_is_named(child)) {
            expr->data.unop.expr = convert_expression(child, source);
        } else {
            if (strcmp(type, "!") == 0 || strcmp(type, "not") == 0)
                expr->data.unop.op = PUPPET_UNOP_NOT;
            else if (strcmp(type, "-") == 0)
                expr->data.unop.op = PUPPET_UNOP_MINUS;
            else if (strcmp(type, "+") == 0)
                expr->data.unop.op = PUPPET_UNOP_PLUS;
            else if (strcmp(type, "*") == 0)
                expr->data.unop.op = PUPPET_UNOP_SPLAT;
        }
    }

    return expr;
}

/* Convert array literal */
static puppet_expr_t *convert_array(TSNode node, const char *source) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_VALUE;
    expr->loc = node_location(node);
    expr->data.value = puppet_value_create_array();

    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        puppet_expr_t *elem = convert_expression(child, source);
        if (elem && elem->type == PUPPET_EXPR_VALUE) {
            puppet_array_append(expr->data.value->data.array,
                              puppet_value_copy(elem->data.value));
            puppet_expr_destroy(elem);
        }
        /* TODO: handle non-literal elements */
    }

    return expr;
}

/* Convert hash literal */
static puppet_expr_t *convert_hash(TSNode node, const char *source) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_VALUE;
    expr->loc = node_location(node);
    expr->data.value = puppet_value_create_hash();

    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        if (node_is(child, "hash_entry") || node_is(child, "hashpair")) {
            uint32_t entry_count = ts_node_named_child_count(child);
            if (entry_count >= 2) {
                TSNode key_node = ts_node_named_child(child, 0);
                /* hashpair has: key, arrow, value - skip the arrow */
                TSNode val_node = ts_node_named_child(child, entry_count - 1);

                puppet_expr_t *key = convert_expression(key_node, source);
                puppet_expr_t *val = convert_expression(val_node, source);

                if (key && key->type == PUPPET_EXPR_VALUE &&
                    key->data.value->type == PUPPET_VALUE_STRING &&
                    val && val->type == PUPPET_EXPR_VALUE) {
                    puppet_hash_set(expr->data.value->data.hash,
                                   key->data.value->data.string.data,
                                   key->data.value->data.string.len,
                                   puppet_value_copy(val->data.value));
                }
                if (key) puppet_expr_destroy(key);
                if (val) puppet_expr_destroy(val);
            }
        }
    }

    return expr;
}

/* Convert function call expression */
static puppet_expr_t *convert_funcall(TSNode node, const char *source) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_FUNCALL;
    expr->loc = node_location(node);

    /* Find function name and arguments */
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "name") == 0 && expr->data.funcall.name.data == NULL) {
            char *name = node_text(child, source);
            expr->data.funcall.name = puppet_string_create(name);
            puppet_free(name);
        } else if (strcmp(type, "argument_list") == 0) {
            uint32_t arg_count = ts_node_named_child_count(child);
            expr->data.funcall.args.exprs = puppet_calloc(arg_count, sizeof(puppet_expr_t*));
            expr->data.funcall.args.count = 0;

            for (uint32_t j = 0; j < arg_count; j++) {
                TSNode arg = ts_node_named_child(child, j);
                puppet_expr_t *arg_expr = convert_expression(arg, source);
                if (arg_expr) {
                    expr->data.funcall.args.exprs[expr->data.funcall.args.count++] = arg_expr;
                }
            }
        } else if (strcmp(type, "lambda") == 0) {
            expr->data.funcall.lambda = convert_lambda(child, source);
        }
    }

    return expr;
}

/* Convert name (bareword) to string value */
static puppet_expr_t *convert_name_to_value(TSNode node, const char *source) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_VALUE;
    expr->loc = node_location(node);

    char *text = node_text(node, source);
    expr->data.value = puppet_value_create_string(text, strlen(text));
    puppet_free(text);

    return expr;
}

/* Convert lambda: |$params| { body } or |$params| => expr */
static puppet_lambda_t *convert_lambda(TSNode node, const char *source) {
    puppet_lambda_t *lambda = puppet_calloc(1, sizeof(puppet_lambda_t));

    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "parameter_list") == 0) {
            /* Parse parameters */
            uint32_t param_count = ts_node_named_child_count(child);
            lambda->params.params = puppet_calloc(param_count, sizeof(puppet_param_t));

            for (uint32_t j = 0; j < param_count; j++) {
                TSNode param = ts_node_named_child(child, j);
                /* parameter -> regular_parameter -> variable -> name */
                TSNode var = find_child(param, "regular_parameter");
                if (ts_node_is_null(var)) var = param;
                TSNode var_node = find_child(var, "variable");
                if (ts_node_is_null(var_node)) var_node = find_child(param, "variable");
                if (!ts_node_is_null(var_node)) {
                    TSNode name = find_child(var_node, "name");
                    if (!ts_node_is_null(name)) {
                        char *name_str = node_text(name, source);
                        lambda->params.params[lambda->params.count].name = puppet_string_create(name_str);
                        puppet_free(name_str);
                        lambda->params.count++;
                    }
                }
            }
        } else if (strcmp(type, "block") == 0) {
            /* Check if block contains just a single expression */
            uint32_t block_children = ts_node_named_child_count(child);
            if (block_children == 1) {
                TSNode stmt_node = ts_node_named_child(child, 0);
                /* Check if the statement is just an expression (binary, variable, etc.) */
                const char *stmt_type = ts_node_type(stmt_node);
                if (strcmp(stmt_type, "statement") == 0) {
                    uint32_t stmt_children = ts_node_named_child_count(stmt_node);
                    if (stmt_children == 1) {
                        TSNode inner = ts_node_named_child(stmt_node, 0);
                        const char *inner_type = ts_node_type(inner);
                        /* Expression types that should be treated as expression body */
                        if (strcmp(inner_type, "binary") == 0 ||
                            strcmp(inner_type, "variable") == 0 ||
                            strcmp(inner_type, "number") == 0 ||
                            strcmp(inner_type, "function_call") == 0 ||
                            strcmp(inner_type, "unary") == 0) {
                            lambda->expr_body = convert_expression(inner, source);
                            continue;
                        }
                    }
                }
            }
            /* Regular statement block body */
            lambda->body = puppet_calloc(1, sizeof(puppet_stmt_list_t));
            *lambda->body = convert_block(child, source);
        } else {
            /* Expression body (for |$x| => expr) */
            lambda->expr_body = convert_expression(child, source);
        }
    }

    return lambda;
}

/* Convert method call with lambda: obj.method |$x| { body } */
static puppet_expr_t *convert_method_with_lambda(TSNode node, const char *source) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_FUNCALL;
    expr->loc = node_location(node);

    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "call_method") == 0) {
            /* Parse the method call (named_access) */
            TSNode access = find_child(child, "named_access");
            if (!ts_node_is_null(access)) {
                uint32_t access_count = ts_node_named_child_count(access);
                if (access_count >= 2) {
                    /* First child is the object, last is the method name */
                    TSNode obj = ts_node_named_child(access, 0);
                    TSNode method = ts_node_named_child(access, access_count - 1);

                    /* Convert object to first argument */
                    puppet_expr_t *obj_expr = convert_expression(obj, source);
                    if (obj_expr) {
                        expr->data.funcall.args.exprs = puppet_calloc(1, sizeof(puppet_expr_t*));
                        expr->data.funcall.args.exprs[0] = obj_expr;
                        expr->data.funcall.args.count = 1;
                    }

                    /* Get method name */
                    if (node_is(method, "name")) {
                        char *method_name = node_text(method, source);
                        expr->data.funcall.name = puppet_string_create(method_name);
                        puppet_free(method_name);
                    }
                }
            }
        } else if (strcmp(type, "lambda") == 0) {
            expr->data.funcall.lambda = convert_lambda(child, source);
        }
    }

    return expr;
}

/* Convert named_access: obj.method */
static puppet_expr_t *convert_named_access(TSNode node, const char *source) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_DOT;
    expr->loc = node_location(node);

    uint32_t count = ts_node_named_child_count(node);
    if (count >= 2) {
        TSNode obj = ts_node_named_child(node, 0);
        TSNode field = ts_node_named_child(node, count - 1);

        expr->data.dot.object = convert_expression(obj, source);
        if (node_is(field, "name")) {
            char *field_name = node_text(field, source);
            expr->data.dot.field = puppet_string_create(field_name);
            puppet_free(field_name);
        }
    }

    return expr;
}

/* Main expression conversion dispatcher */
static puppet_expr_t *convert_expression(TSNode node, const char *source) {
    if (ts_node_is_null(node)) return NULL;

    const char *type = ts_node_type(node);

    if (strcmp(type, "variable") == 0)
        return convert_variable(node, source);
    if (strcmp(type, "number") == 0)
        return convert_number(node, source);
    if (strcmp(type, "single_quoted_string") == 0 ||
        strcmp(type, "double_quoted_string") == 0)
        return convert_string_literal(node, source);
    if (strcmp(type, "boolean") == 0)
        return convert_boolean(node, source);
    if (strcmp(type, "undef") == 0)
        return convert_undef(node, source);
    if (strcmp(type, "binary") == 0)
        return convert_binary(node, source);
    if (strcmp(type, "unary") == 0)
        return convert_unary(node, source);
    if (strcmp(type, "array") == 0)
        return convert_array(node, source);
    if (strcmp(type, "hash") == 0)
        return convert_hash(node, source);
    if (strcmp(type, "function_call") == 0 || strcmp(type, "statement_function") == 0)
        return convert_funcall(node, source);
    if (strcmp(type, "call_method_with_lambda") == 0)
        return convert_method_with_lambda(node, source);
    if (strcmp(type, "named_access") == 0)
        return convert_named_access(node, source);
    if (strcmp(type, "name") == 0)
        return convert_name_to_value(node, source);

    /* For wrapper nodes, recurse into first child */
    uint32_t count = ts_node_named_child_count(node);
    if (count > 0) {
        return convert_expression(ts_node_named_child(node, 0), source);
    }

    /* Fallback: create string from node text */
    return convert_name_to_value(node, source);
}

/*
 * ===========================================================================
 * STATEMENT CONVERSION
 * ===========================================================================
 */

/* Convert attribute node */
static puppet_attribute_t convert_attribute(TSNode node, const char *source) {
    puppet_attribute_t attr = {0};

    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "name") == 0 && attr.name.data == NULL) {
            char *name = node_text(child, source);
            attr.name = puppet_string_create(name);
            puppet_free(name);
        } else if (strcmp(type, "arrow") != 0) {
            attr.value = convert_expression(child, source);
        }
    }

    return attr;
}

/* Build index expression from object + access node */
static puppet_expr_t *build_index_expr(puppet_expr_t *object, TSNode access_node, const char *source) {
    /* Access node contains access_element children with the index expressions */
    TSNode element = find_child(access_node, "access_element");
    if (ts_node_is_null(element)) {
        /* Try direct child */
        element = ts_node_named_child(access_node, 0);
    }

    puppet_expr_t *index_expr = NULL;
    if (!ts_node_is_null(element)) {
        index_expr = convert_expression(element, source);
    }

    if (!index_expr) {
        return object;  /* No valid index, return object unchanged */
    }

    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_INDEX;
    expr->loc = node_location(access_node);
    expr->data.index.object = object;
    expr->data.index.index = index_expr;
    return expr;
}

/* Convert assignment: variable = expression */
static puppet_stmt_t *convert_assignment(TSNode node, const char *source) {
    puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
    stmt->type = PUPPET_STMT_ASSIGNMENT;
    stmt->loc = node_location(node);

    /*
     * Assignment can have two forms:
     * 1. Simple: $var = expr
     *    Children: variable, expr
     * 2. With bracket access on RHS: $var = $obj['key1']['key2']
     *    Children: variable, variable, access, access
     *    First variable is LHS, second + access nodes are the indexed expression
     */
    bool lhs_set = false;
    puppet_expr_t *rhs_expr = NULL;

    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "variable") == 0) {
            if (!lhs_set) {
                /* First variable is the assignment target */
                TSNode name_node = find_child(child, "name");
                if (!ts_node_is_null(name_node)) {
                    char *name = node_text(name_node, source);
                    stmt->data.assignment.variable = puppet_string_create(name);
                    puppet_free(name);
                }
                lhs_set = true;
            } else {
                /* Subsequent variable is part of the RHS expression */
                rhs_expr = convert_expression(child, source);
            }
        } else if (strcmp(type, "access") == 0) {
            /* Build index expression: rhs_expr[access] */
            if (rhs_expr) {
                rhs_expr = build_index_expr(rhs_expr, child, source);
            }
        } else {
            /* Other expression types */
            rhs_expr = convert_expression(child, source);
        }
    }

    stmt->data.assignment.value = rhs_expr;
    return stmt;
}

/* Convert resource declaration: type { title: attrs } */
static puppet_stmt_t *convert_resource(TSNode node, const char *source) {
    puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
    stmt->type = PUPPET_STMT_RESOURCE;
    stmt->loc = node_location(node);
    stmt->data.resource.style = PUPPET_RES_NORMAL;

    /* Check for virtual or exported markers */
    TSNode virtual_node = find_child(node, "virtual");
    TSNode exported_node = find_child(node, "exported");
    if (!ts_node_is_null(exported_node)) {
        stmt->data.resource.style = PUPPET_RES_EXPORTED;
    } else if (!ts_node_is_null(virtual_node)) {
        stmt->data.resource.style = PUPPET_RES_VIRTUAL;
    }

    /* Find resource type name */
    TSNode name_node = find_child(node, "name");
    if (!ts_node_is_null(name_node)) {
        char *type_name = node_text(name_node, source);
        stmt->data.resource.type = puppet_string_create(type_name);
        puppet_free(type_name);
    }

    /* Find resource body */
    TSNode body = find_child(node, "resource_body");
    if (!ts_node_is_null(body)) {
        /* Create single instance */
        stmt->data.resource.instances = puppet_calloc(1, sizeof(puppet_resource_instance_t));
        stmt->data.resource.instance_count = 1;
        puppet_resource_instance_t *inst = &stmt->data.resource.instances[0];

        /* Get title */
        TSNode title = find_child(body, "resource_title");
        if (!ts_node_is_null(title)) {
            uint32_t title_count = ts_node_named_child_count(title);
            if (title_count > 0) {
                inst->title = convert_expression(ts_node_named_child(title, 0), source);
            }
        }

        /* Get attributes */
        TSNode attr_list = find_child(body, "attribute_list");
        if (!ts_node_is_null(attr_list)) {
            uint32_t attr_count = ts_node_named_child_count(attr_list);
            inst->attributes = puppet_calloc(attr_count, sizeof(puppet_attribute_t));
            inst->attr_count = 0;

            for (uint32_t i = 0; i < attr_count; i++) {
                TSNode attr_node = ts_node_named_child(attr_list, i);
                if (node_is(attr_node, "attribute")) {
                    inst->attributes[inst->attr_count++] = convert_attribute(attr_node, source);
                }
            }
        }
    }

    return stmt;
}

/* Convert class definition */
static puppet_stmt_t *convert_class_def(TSNode node, const char *source) {
    puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
    stmt->type = PUPPET_STMT_CLASS_DEF;
    stmt->loc = node_location(node);

    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "classname") == 0) {
            TSNode name = find_child(child, "name");
            if (!ts_node_is_null(name)) {
                char *name_str = node_text(name, source);
                stmt->data.class_def.name = puppet_string_create(name_str);
                puppet_free(name_str);
            }
        } else if (strcmp(type, "block") == 0) {
            stmt->data.class_def.body = convert_block(child, source);
        }
        /* TODO: parameters, inherits */
    }

    return stmt;
}

/* Convert include/require/contain statement */
static puppet_stmt_t *convert_include(TSNode node, const char *source, puppet_stmt_type_t stmt_type) {
    puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
    stmt->type = stmt_type;
    stmt->loc = node_location(node);

    /* Find argument list or class names */
    TSNode args = find_child(node, "argument_list");
    if (!ts_node_is_null(args)) {
        uint32_t count = ts_node_named_child_count(args);
        stmt->data.names.exprs = puppet_calloc(count, sizeof(puppet_expr_t*));
        stmt->data.names.count = 0;

        for (uint32_t i = 0; i < count; i++) {
            TSNode arg = ts_node_named_child(args, i);
            puppet_expr_t *expr = convert_expression(arg, source);
            if (expr) {
                stmt->data.names.exprs[stmt->data.names.count++] = expr;
            }
        }
    }

    return stmt;
}

/* Convert if statement */
static puppet_stmt_t *convert_if(TSNode node, const char *source) {
    puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
    stmt->type = PUPPET_STMT_IF;
    stmt->loc = node_location(node);

    /* Create first branch */
    puppet_if_branch_t *branch = puppet_calloc(1, sizeof(puppet_if_branch_t));
    stmt->data.if_stmt.branches = branch;

    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "condition") == 0) {
            uint32_t cond_count = ts_node_named_child_count(child);
            if (cond_count > 0) {
                branch->condition = convert_expression(ts_node_named_child(child, 0), source);
            }
        } else if (strcmp(type, "block") == 0 && branch->body.stmts == NULL) {
            branch->body = convert_block(child, source);
        } else if (strcmp(type, "elsif") == 0) {
            /* TODO: handle elsif branches */
        } else if (strcmp(type, "else") == 0) {
            TSNode else_block = find_child(child, "block");
            if (!ts_node_is_null(else_block)) {
                stmt->data.if_stmt.else_body = puppet_calloc(1, sizeof(puppet_stmt_list_t));
                *stmt->data.if_stmt.else_body = convert_block(else_block, source);
            }
        }
    }

    return stmt;
}

/* Convert case statement */
static puppet_stmt_t *convert_case(TSNode node, const char *source) {
    puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
    stmt->type = PUPPET_STMT_CASE;
    stmt->loc = node_location(node);

    /* Find the control expression and cases */
    uint32_t count = ts_node_named_child_count(node);
    size_t when_capacity = 8;
    stmt->data.case_stmt.whens = puppet_calloc(when_capacity, sizeof(puppet_case_when_t));

    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "condition") == 0 || strcmp(type, "variable") == 0 ||
            strcmp(type, "binary") == 0) {
            stmt->data.case_stmt.expr = convert_expression(child, source);
        } else if (strcmp(type, "case_entry") == 0 || strcmp(type, "case_option") == 0) {
            /* Each case_entry/case_option has match values and a block */
            TSNode block = find_child(child, "block");

            uint32_t entry_count = ts_node_named_child_count(child);
            for (uint32_t j = 0; j < entry_count; j++) {
                TSNode entry_child = ts_node_named_child(child, j);
                const char *entry_type = ts_node_type(entry_child);
                if (!node_is(entry_child, "block")) {
                    /* This is a match value - check for 'default' keyword */
                    bool is_default = (strcmp(entry_type, "default") == 0);

                    if (stmt->data.case_stmt.when_count >= when_capacity) {
                        when_capacity *= 2;
                        stmt->data.case_stmt.whens = puppet_realloc(
                            stmt->data.case_stmt.whens,
                            when_capacity * sizeof(puppet_case_when_t));
                    }
                    puppet_case_when_t *when = &stmt->data.case_stmt.whens[stmt->data.case_stmt.when_count++];

                    if (is_default) {
                        /* Default case - test is null */
                        when->test = NULL;
                    } else {
                        when->test = convert_expression(entry_child, source);
                    }

                    if (!ts_node_is_null(block)) {
                        when->body = convert_block(block, source);
                    }
                }
            }
        }
    }

    return stmt;
}

/* Convert function call as statement */
static puppet_stmt_t *convert_function_stmt(TSNode node, const char *source) {
    puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
    stmt->type = PUPPET_STMT_FUNCTION_CALL;
    stmt->loc = node_location(node);
    stmt->data.expr = convert_funcall(node, source);
    return stmt;
}

/* Main statement conversion dispatcher */
static puppet_stmt_t *convert_statement(TSNode node, const char *source) {
    if (ts_node_is_null(node)) return NULL;

    const char *type = ts_node_type(node);

    /* Check 'statement' wrapper for patterns */
    if (strcmp(type, "statement") == 0) {
        uint32_t count = ts_node_named_child_count(node);
        if (count >= 2) {
            TSNode first = ts_node_named_child(node, 0);
            /* assignment: variable = expression */
            if (node_is(first, "variable")) {
                return convert_assignment(node, source);
            }
        }
        /* Unwrap single-child statement */
        if (count > 0) {
            return convert_statement(ts_node_named_child(node, 0), source);
        }
        return NULL;
    }

    /* Specific statement types first (before generic assignment check) */
    if (strcmp(type, "class_definition") == 0)
        return convert_class_def(node, source);
    if (strcmp(type, "resource_type") == 0)
        return convert_resource(node, source);
    if (strcmp(type, "if") == 0)
        return convert_if(node, source);
    if (strcmp(type, "case") == 0)
        return convert_case(node, source);
    if (strcmp(type, "statement_function") == 0) {
        /* Check which type of function */
        TSNode name = find_child(node, "name");
        if (!ts_node_is_null(name)) {
            char *fn_name = node_text(name, source);
            puppet_stmt_t *result = NULL;

            if (strcmp(fn_name, "include") == 0)
                result = convert_include(node, source, PUPPET_STMT_INCLUDE);
            else if (strcmp(fn_name, "require") == 0)
                result = convert_include(node, source, PUPPET_STMT_REQUIRE);
            else if (strcmp(fn_name, "contain") == 0)
                result = convert_include(node, source, PUPPET_STMT_CONTAIN);
            else
                result = convert_function_stmt(node, source);

            puppet_free(fn_name);
            return result;
        }
    }

    /* Handle method calls with lambdas as expression statements */
    if (strcmp(type, "call_method_with_lambda") == 0) {
        puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
        stmt->type = PUPPET_STMT_EXPRESSION;
        stmt->loc = node_location(node);
        stmt->data.expr = convert_method_with_lambda(node, source);
        return stmt;
    }

    /* Handle function calls as expression statements */
    if (strcmp(type, "function_call") == 0) {
        puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
        stmt->type = PUPPET_STMT_EXPRESSION;
        stmt->loc = node_location(node);
        stmt->data.expr = convert_funcall(node, source);
        return stmt;
    }

    /* Handle resource collectors: File <| ensure == present |> */
    if (strcmp(type, "resource_collector") == 0) {
        puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
        stmt->type = PUPPET_STMT_RESOURCE_COLLECTOR;
        stmt->loc = node_location(node);

        /* First child is the type expression, second is collect_query */
        TSNode type_node = ts_node_named_child(node, 0);
        TSNode query_node = find_child(node, "collect_query");

        /* Get the type name */
        if (!ts_node_is_null(type_node)) {
            char *type_text = node_text(type_node, source);
            stmt->data.collector.type = puppet_string_create(type_text);
            puppet_free(type_text);
        }

        /* Determine collector style from the query delimiters */
        /* <| |> = virtual, <<| |>> = exported */
        if (!ts_node_is_null(query_node)) {
            uint32_t start = ts_node_start_byte(query_node);
            /* Check if it starts with <<| (exported) or <| (virtual) */
            if (source[start] == '<' && source[start + 1] == '<') {
                stmt->data.collector.style = PUPPET_RES_EXPORTED;
            } else {
                stmt->data.collector.style = PUPPET_RES_VIRTUAL;
            }

            /* Get the filter expression if present */
            TSNode filter = ts_node_named_child(query_node, 0);
            if (!ts_node_is_null(filter)) {
                stmt->data.collector.search_expr = convert_expression(filter, source);
            }
        }

        return stmt;
    }

    /* Handle node definitions: node 'name' { } or node default { } */
    if (strcmp(type, "node_definition") == 0) {
        puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
        stmt->type = PUPPET_STMT_NODE;
        stmt->loc = node_location(node);

        /* Get hostname (can be string, default, or regex) */
        TSNode hostname = find_child(node, "hostname");
        if (!ts_node_is_null(hostname)) {
            TSNode name_child = ts_node_named_child(hostname, 0);
            if (!ts_node_is_null(name_child)) {
                const char *name_type = ts_node_type(name_child);
                if (strcmp(name_type, "default") == 0) {
                    stmt->data.node.name = puppet_string_create("default");
                } else {
                    /* String or regex - extract text without quotes */
                    char *text = node_text(name_child, source);
                    /* Remove quotes if present */
                    size_t len = strlen(text);
                    if (len >= 2 && (text[0] == '\'' || text[0] == '"')) {
                        memmove(text, text + 1, len - 2);
                        text[len - 2] = '\0';
                    }
                    stmt->data.node.name = puppet_string_create(text);
                    puppet_free(text);
                }
            }
        }

        /* Get node body */
        TSNode block = find_child(node, "block");
        if (!ts_node_is_null(block)) {
            stmt->data.node.body = convert_block(block, source);
        }

        return stmt;
    }

    /* Skip comments and unknown nodes */
    if (strcmp(type, "comment") == 0)
        return NULL;

    return NULL;
}

/* Convert block (list of statements) */
static puppet_stmt_list_t convert_block(TSNode node, const char *source) {
    puppet_stmt_list_t list = {0};

    uint32_t count = ts_node_named_child_count(node);
    list.stmts = puppet_calloc(count, sizeof(puppet_stmt_t*));
    list.count = 0;

    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        puppet_stmt_t *stmt = convert_statement(child, source);
        if (stmt) {
            list.stmts[list.count++] = stmt;
        }
    }

    return list;
}

/*
 * ===========================================================================
 * PUBLIC API
 * ===========================================================================
 */

puppet_stmt_list_t *puppet_ts_parse_string(const char *source, size_t length) {
    TSParser *parser = ts_parser_new();
    if (!parser) return NULL;

    const TSLanguage *lang = tree_sitter_puppet();
    if (!lang || !ts_parser_set_language(parser, lang)) {
        ts_parser_delete(parser);
        return NULL;
    }

    TSTree *tree = ts_parser_parse_string(parser, NULL, source, length);
    if (!tree) {
        ts_parser_delete(parser);
        return NULL;
    }

    TSNode root = ts_tree_root_node(tree);

    /* Check for parse errors and report them */
    if (ts_node_has_error(root)) {
        /* Find and report error nodes */
        TSTreeCursor cursor = ts_tree_cursor_new(root);
        bool found_error = false;

        /* Traverse tree to find ERROR nodes */
        bool done = false;
        while (!done) {
            TSNode node = ts_tree_cursor_current_node(&cursor);

            if (strcmp(ts_node_type(node), "ERROR") == 0) {
                TSPoint start = ts_node_start_point(node);
                uint32_t start_byte = ts_node_start_byte(node);
                uint32_t end_byte = ts_node_end_byte(node);

                /* Extract error context (up to 40 chars) */
                int ctx_len = end_byte - start_byte;
                if (ctx_len > 40) ctx_len = 40;
                char context[41];
                strncpy(context, source + start_byte, ctx_len);
                context[ctx_len] = '\0';
                /* Replace newlines with spaces for display */
                for (int i = 0; i < ctx_len; i++) {
                    if (context[i] == '\n' || context[i] == '\r') context[i] = ' ';
                }

                fprintf(stderr, "Parse error at line %u, column %u: unexpected '%s'\n",
                        start.row + 1, start.column + 1, context);
                found_error = true;
            } else if (ts_node_is_missing(node)) {
                TSPoint start = ts_node_start_point(node);
                fprintf(stderr, "Parse error at line %u, column %u: missing '%s'\n",
                        start.row + 1, start.column + 1, ts_node_type(node));
                found_error = true;
            }

            /* Move to next node */
            if (ts_tree_cursor_goto_first_child(&cursor)) {
                continue;
            }
            while (!ts_tree_cursor_goto_next_sibling(&cursor)) {
                if (!ts_tree_cursor_goto_parent(&cursor)) {
                    done = true;
                    break;
                }
            }
        }

        ts_tree_cursor_delete(&cursor);

        if (!found_error) {
            fprintf(stderr, "Parse errors detected (unknown location)\n");
        }

        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return NULL;
    }

    puppet_stmt_list_t block = convert_block(root, source);

    /* Allocate and copy result */
    puppet_stmt_list_t *result = puppet_malloc(sizeof(puppet_stmt_list_t));
    *result = block;

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return result;
}

puppet_stmt_list_t *puppet_ts_parse_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror(filename);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *source = puppet_malloc(size + 1);
    size_t read = fread(source, 1, size, f);
    source[read] = '\0';
    fclose(f);

    puppet_stmt_list_t *result = puppet_ts_parse_string(source, read);
    puppet_free(source);

    return result;
}
