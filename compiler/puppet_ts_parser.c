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
static puppet_expr_t *convert_access_element_expr(TSNode access_elem, const char *source);
static puppet_stmt_list_t convert_block(TSNode node, const char *source);
static puppet_lambda_t *convert_lambda(TSNode node, const char *source);
static puppet_expr_t *build_index_expr(puppet_expr_t *object, TSNode access_node, const char *source);
static puppet_expr_t *convert_selector(TSNode node, const char *source);
static puppet_expr_t *build_method_funcall(TSNode named_access, TSNode arglist, const char *source);

/* Current filename being parsed (for source location tracking) - thread-local for parallel safety */
static __thread const char *current_parse_filename = NULL;

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
        .filename = current_parse_filename ? puppet_strdup(current_parse_filename) : NULL,
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
                    /* Process escape sequences for double-quoted string parts */
                    size_t processed_len;
                    char *processed = process_escape_sequences(part, &processed_len);
                    expr->data.interpolated.parts[part_idx] = puppet_string_create(processed);
                    puppet_free(processed);
                    puppet_free(part);
                } else {
                    expr->data.interpolated.parts[part_idx] = puppet_string_create("");
                }

                /* Convert the interpolated expression */
                /* Check for variable + chained access pattern (e.g., ${hash['a']['b']}) */
                uint32_t interp_child_count = ts_node_named_child_count(child);
                TSNode var_child = find_child(child, "variable");

                if (!ts_node_is_null(var_child)) {
                    /* Start with variable expression */
                    puppet_expr_t *result_expr = convert_variable(var_child, source);

                    /* Build chained index expressions for all access nodes */
                    for (uint32_t j = 0; j < interp_child_count; j++) {
                        TSNode access_node = ts_node_named_child(child, j);
                        if (strcmp(ts_node_type(access_node), "access") == 0) {
                            result_expr = build_index_expr(result_expr, access_node, source);
                        }
                    }
                    expr->data.interpolated.exprs[part_idx] = result_expr;
                } else if (interp_child_count > 0) {
                    TSNode interp_expr = ts_node_named_child(child, 0);
                    expr->data.interpolated.exprs[part_idx] = convert_expression(interp_expr, source);
                } else {
                    expr->data.interpolated.exprs[part_idx] = convert_expression(child, source);
                }

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
            /* Process escape sequences for double-quoted string parts */
            size_t processed_len;
            char *processed = process_escape_sequences(part, &processed_len);
            expr->data.interpolated.parts[part_idx] = puppet_string_create(processed);
            puppet_free(processed);
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

/* Convert regex literal - e.g., /^foo$/ */
static puppet_expr_t *convert_regex(TSNode node, const char *source) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_VALUE;
    expr->loc = node_location(node);

    char *text = node_text(node, source);
    size_t len = strlen(text);

    /* Remove surrounding slashes from /pattern/ */
    if (len >= 2 && text[0] == '/' && text[len-1] == '/') {
        puppet_value_t *val = puppet_calloc(1, sizeof(puppet_value_t));
        val->type = PUPPET_VALUE_REGEXP;
        val->data.regexp.len = len - 2;
        val->data.regexp.data = puppet_malloc(len - 1);
        memcpy(val->data.regexp.data, text + 1, len - 2);
        val->data.regexp.data[len - 2] = '\0';
        expr->data.value = val;
    } else {
        /* Fallback: use as-is */
        puppet_value_t *val = puppet_calloc(1, sizeof(puppet_value_t));
        val->type = PUPPET_VALUE_REGEXP;
        val->data.regexp.len = len;
        val->data.regexp.data = puppet_strdup(text);
        expr->data.value = val;
    }

    puppet_free(text);
    return expr;
}

/* Helper to get operator from string */
static puppet_binop_t get_binop(const char *type) {
    if (strcmp(type, "+") == 0) return PUPPET_OP_ADD;
    else if (strcmp(type, "-") == 0) return PUPPET_OP_SUB;
    else if (strcmp(type, "*") == 0) return PUPPET_OP_MUL;
    else if (strcmp(type, "/") == 0) return PUPPET_OP_DIV;
    else if (strcmp(type, "%") == 0) return PUPPET_OP_MOD;
    else if (strcmp(type, "==") == 0) return PUPPET_OP_EQ;
    else if (strcmp(type, "!=") == 0) return PUPPET_OP_NE;
    else if (strcmp(type, "<") == 0) return PUPPET_OP_LT;
    else if (strcmp(type, "<=") == 0) return PUPPET_OP_LE;
    else if (strcmp(type, ">") == 0) return PUPPET_OP_GT;
    else if (strcmp(type, ">=") == 0) return PUPPET_OP_GE;
    else if (strcmp(type, "and") == 0) return PUPPET_OP_AND;
    else if (strcmp(type, "or") == 0) return PUPPET_OP_OR;
    else if (strcmp(type, "=~") == 0) return PUPPET_OP_MATCH;
    else if (strcmp(type, "!~") == 0) return PUPPET_OP_NOT_MATCH;
    else if (strcmp(type, "in") == 0) return PUPPET_OP_IN;
    else if (strcmp(type, "<<") == 0) return PUPPET_OP_LSHIFT;
    else if (strcmp(type, ">>") == 0) return PUPPET_OP_RSHIFT;
    return PUPPET_OP_ADD;  /* default */
}

/* Check if operator is comparison (higher precedence than and/or) */
static bool is_comparison_op(puppet_binop_t op) {
    return op == PUPPET_OP_EQ || op == PUPPET_OP_NE ||
           op == PUPPET_OP_LT || op == PUPPET_OP_LE ||
           op == PUPPET_OP_GT || op == PUPPET_OP_GE ||
           op == PUPPET_OP_MATCH || op == PUPPET_OP_NOT_MATCH;
}

/* Check if operator is logical (lower precedence) */
static bool is_logical_op(puppet_binop_t op) {
    return op == PUPPET_OP_AND || op == PUPPET_OP_OR;
}

/* Convert binary expression */
static puppet_expr_t *convert_binary(TSNode node, const char *source) {
    /* Parse children to find operands and operator
     * Handle the case where variable + access nodes appear together as LHS:
     * e.g., $hash['key'] == value produces:
     *   lhs: variable, lhs: access, rhs: value
     * We need to combine variable + access into an index expression
     *
     * Also handle tree-sitter quirk where access nodes for a nested variable
     * appear at the parent level instead of with the variable.
     */
    uint32_t count = ts_node_child_count(node);
    puppet_expr_t *result = NULL;
    puppet_expr_t *pending_operand = NULL;
    puppet_expr_t *access_accumulator = NULL;  /* For orphaned access nodes */
    puppet_expr_t *pending_type_ref = NULL;    /* For type + access pattern */
    puppet_binop_t pending_op = PUPPET_OP_ADD;
    bool have_op = false;

    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *type = ts_node_type(child);

        /* Skip comment nodes - they can appear in multiline expressions */
        if (strcmp(type, "comment") == 0) continue;

        if (ts_node_is_named(child)) {
            /* Check for type node - start of Type[X] reference */
            if (strcmp(type, "type") == 0) {
                char *type_str = node_text(child, source);
                pending_type_ref = puppet_calloc(1, sizeof(puppet_expr_t));
                pending_type_ref->type = PUPPET_EXPR_RESOURCE_REF;
                pending_type_ref->loc = node_location(child);
                pending_type_ref->data.resource_ref.type = puppet_string_create(type_str);
                puppet_free(type_str);
                continue;
            }

            /* Check if this is an access node that should combine with preceding expression */
            if (strcmp(type, "access") == 0) {
                if (pending_type_ref) {
                    /* Complete Type[X] resource reference */
                    TSNode access_elem = find_child(child, "access_element");
                    if (!ts_node_is_null(access_elem)) {
                        TSNode value_node = ts_node_named_child(access_elem, 0);
                        pending_type_ref->data.resource_ref.title = convert_expression(value_node, source);
                    } else {
                        pending_type_ref->data.resource_ref.title = convert_expression(child, source);
                    }
                    /* Use the completed reference as the operand */
                    if (have_op && !pending_operand) {
                        pending_operand = pending_type_ref;
                    } else if (!result) {
                        result = pending_type_ref;
                    }
                    pending_type_ref = NULL;
                } else if (pending_operand) {
                    /* Combine with pending operand to form index expression */
                    pending_operand = build_index_expr(pending_operand, child, source);
                } else if (access_accumulator) {
                    /* Continue building the access chain */
                    access_accumulator = build_index_expr(access_accumulator, child, source);
                } else if (result && !have_op) {
                    /* Access after a complete expression - tree-sitter quirk where access
                     * nodes for a variable are at a higher level than the variable itself.
                     * Extract the rightmost operand and start accumulating accesses. */
                    if (result->type == PUPPET_EXPR_BINOP && result->data.binop.right &&
                        is_logical_op(result->data.binop.op)) {
                        /* Extract right operand from logical expression for access accumulation */
                        access_accumulator = build_index_expr(result->data.binop.right, child, source);
                        result->data.binop.right = NULL;  /* Will be filled in when we see operator */
                    } else {
                        /* Fallback: apply to result directly */
                        result = build_index_expr(result, child, source);
                    }
                }
            } else {
                /* New operand */
                puppet_expr_t *new_operand = convert_expression(child, source);

                if (!result) {
                    /* First operand */
                    result = new_operand;
                } else if (have_op && !pending_operand) {
                    /* Second operand for current op */
                    pending_operand = new_operand;
                } else if (have_op && pending_operand) {
                    /* Third+ operand - build nested expression first */
                    puppet_expr_t *inner = puppet_calloc(1, sizeof(puppet_expr_t));
                    inner->type = PUPPET_EXPR_BINOP;
                    inner->loc = node_location(node);
                    inner->data.binop.op = pending_op;
                    inner->data.binop.left = result;
                    inner->data.binop.right = pending_operand;
                    result = inner;
                    pending_operand = new_operand;
                    have_op = false;  /* Need a new operator */
                }
            }
        } else {
            /* Check if this is an actual operator token (not punctuation like parens) */
            if (strcmp(type, "(") == 0 || strcmp(type, ")") == 0 ||
                strcmp(type, "{") == 0 || strcmp(type, "}") == 0 ||
                strcmp(type, "[") == 0 || strcmp(type, "]") == 0 ||
                strcmp(type, ",") == 0 || strcmp(type, ";") == 0 ||
                strcmp(type, ":") == 0 || strcmp(type, "=>") == 0) {
                /* Skip punctuation */
                continue;
            }

            /* Operator token */
            puppet_binop_t new_op = get_binop(type);

            /* If we have accumulated accesses and see a comparison operator,
             * the accesses form the left side of the comparison */
            if (access_accumulator && is_comparison_op(new_op)) {
                /* Start building a comparison with the accumulated expression */
                pending_operand = NULL;  /* Will get the RHS value next */
                pending_op = new_op;
                have_op = true;
                /* access_accumulator becomes the left operand of this comparison */
                /* We'll build it when we get the right operand */
                continue;
            }

            if (result && pending_operand) {
                /* Complete the current binary expression */
                puppet_expr_t *inner = puppet_calloc(1, sizeof(puppet_expr_t));
                inner->type = PUPPET_EXPR_BINOP;
                inner->loc = node_location(node);
                inner->data.binop.op = pending_op;
                inner->data.binop.left = result;
                inner->data.binop.right = pending_operand;
                result = inner;
                pending_operand = NULL;
            }
            pending_op = new_op;
            have_op = true;
        }
    }

    /* Handle accumulated accesses that formed a comparison */
    if (access_accumulator && have_op && pending_operand) {
        /* Build: (access_accumulator OP pending_operand) */
        puppet_expr_t *comparison = puppet_calloc(1, sizeof(puppet_expr_t));
        comparison->type = PUPPET_EXPR_BINOP;
        comparison->loc = node_location(node);
        comparison->data.binop.op = pending_op;
        comparison->data.binop.left = access_accumulator;
        comparison->data.binop.right = pending_operand;

        /* If result is a logical expression missing its right operand, fill it in */
        if (result && result->type == PUPPET_EXPR_BINOP &&
            is_logical_op(result->data.binop.op) && !result->data.binop.right) {
            result->data.binop.right = comparison;
            return result;
        }
        /* Otherwise combine with result */
        if (result) {
            puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
            expr->type = PUPPET_EXPR_BINOP;
            expr->loc = node_location(node);
            expr->data.binop.op = PUPPET_OP_AND;  /* Assume AND for safety */
            expr->data.binop.left = result;
            expr->data.binop.right = comparison;
            return expr;
        }
        return comparison;
    }

    /* Handle bare type ref as final operand: $x =~ Array (no access brackets) */
    if (pending_type_ref && have_op && !pending_operand) {
        pending_operand = pending_type_ref;
        pending_type_ref = NULL;
    }

    /* Build final expression */
    if (result && pending_operand && have_op) {
        puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
        expr->type = PUPPET_EXPR_BINOP;
        expr->loc = node_location(node);
        expr->data.binop.op = pending_op;
        expr->data.binop.left = result;
        expr->data.binop.right = pending_operand;
        return expr;
    }

    /* Single operand or just result */
    if (result) return result;

    /* Fallback - shouldn't happen */
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_BINOP;
    expr->loc = node_location(node);
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

        /* Skip comment nodes */
        if (strcmp(type, "comment") == 0) continue;

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
    expr->loc = node_location(node);

    /* Count array elements (skip comments) */
    uint32_t count = ts_node_named_child_count(node);
    size_t elem_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        if (!node_is(child, "comment")) elem_count++;
    }

    /* Convert all elements and check if they're all literals */
    bool all_literals = true;
    puppet_expr_t **temp_items = puppet_calloc(elem_count, sizeof(puppet_expr_t*));
    size_t idx = 0;

    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        if (node_is(child, "comment")) continue;

        temp_items[idx] = convert_expression(child, source);
        if (temp_items[idx] && temp_items[idx]->type != PUPPET_EXPR_VALUE) {
            all_literals = false;
        }
        idx++;
    }

    if (all_literals && elem_count > 0) {
        /* All literals - build array value directly */
        expr->type = PUPPET_EXPR_VALUE;
        expr->data.value = puppet_value_create_array();

        for (size_t i = 0; i < elem_count; i++) {
            if (temp_items[i] && temp_items[i]->type == PUPPET_EXPR_VALUE) {
                puppet_array_append(expr->data.value->data.array,
                                   puppet_value_copy(temp_items[i]->data.value));
            }
            if (temp_items[i]) puppet_expr_destroy(temp_items[i]);
        }
        puppet_free(temp_items);
    } else if (elem_count > 0) {
        /* Has dynamic values - use PUPPET_EXPR_ARRAY for runtime evaluation */
        expr->type = PUPPET_EXPR_ARRAY;
        expr->data.array_items.items = temp_items;
        expr->data.array_items.count = elem_count;
    } else {
        /* Empty array */
        expr->type = PUPPET_EXPR_VALUE;
        expr->data.value = puppet_value_create_array();
        puppet_free(temp_items);
    }

    return expr;
}

/* Convert hash literal */
static puppet_expr_t *convert_hash(TSNode node, const char *source) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->loc = node_location(node);

    /* Count hash entries first */
    uint32_t count = ts_node_named_child_count(node);
    size_t entry_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        if (node_is(child, "hash_entry") || node_is(child, "hashpair")) {
            entry_count++;
        }
    }

    /* Check if all values are literals - if so, use PUPPET_EXPR_VALUE for efficiency */
    bool all_literals = true;
    puppet_expr_t **temp_keys = puppet_calloc(entry_count, sizeof(puppet_expr_t*));
    puppet_expr_t **temp_vals = puppet_calloc(entry_count, sizeof(puppet_expr_t*));
    size_t idx = 0;

    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        if (node_is(child, "hash_entry") || node_is(child, "hashpair")) {
            uint32_t child_count = ts_node_named_child_count(child);
            if (child_count >= 2) {
                TSNode key_node = ts_node_named_child(child, 0);
                /* hashpair has: key, arrow, value - skip the arrow */
                TSNode val_node = ts_node_named_child(child, child_count - 1);

                temp_keys[idx] = convert_expression(key_node, source);
                temp_vals[idx] = convert_expression(val_node, source);

                /* Check if value is a literal */
                if (temp_vals[idx] && temp_vals[idx]->type != PUPPET_EXPR_VALUE) {
                    all_literals = false;
                }
                idx++;
            }
        }
    }

    if (all_literals && entry_count > 0) {
        /* All literals - build hash value directly */
        expr->type = PUPPET_EXPR_VALUE;
        expr->data.value = puppet_value_create_hash();

        for (size_t i = 0; i < entry_count; i++) {
            if (temp_keys[i] && temp_keys[i]->type == PUPPET_EXPR_VALUE &&
                temp_keys[i]->data.value->type == PUPPET_VALUE_STRING &&
                temp_vals[i] && temp_vals[i]->type == PUPPET_EXPR_VALUE) {
                puppet_hash_set(expr->data.value->data.hash,
                               temp_keys[i]->data.value->data.string.data,
                               temp_keys[i]->data.value->data.string.len,
                               puppet_value_copy(temp_vals[i]->data.value));
            }
            if (temp_keys[i]) puppet_expr_destroy(temp_keys[i]);
            if (temp_vals[i]) puppet_expr_destroy(temp_vals[i]);
        }
        puppet_free(temp_keys);
        puppet_free(temp_vals);
    } else if (entry_count > 0) {
        /* Has dynamic values - use PUPPET_EXPR_HASH */
        expr->type = PUPPET_EXPR_HASH;
        expr->data.hash_entries.keys = temp_keys;
        expr->data.hash_entries.values = temp_vals;
        expr->data.hash_entries.count = entry_count;
    } else {
        /* Empty hash */
        expr->type = PUPPET_EXPR_VALUE;
        expr->data.value = puppet_value_create_hash();
        puppet_free(temp_keys);
        puppet_free(temp_vals);
    }

    return expr;
}

/* Convert selector expression: $x ? { val1 => result1, default => defaultval } */
static puppet_expr_t *convert_selector(TSNode node, const char *source) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_SELECTOR;
    expr->loc = node_location(node);

    /* Count selector_option children */
    uint32_t count = ts_node_named_child_count(node);
    size_t opt_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        if (node_is(child, "selector_option")) {
            opt_count++;
        }
    }

    /* Allocate cases array */
    if (opt_count > 0) {
        expr->data.selector.cases = puppet_calloc(opt_count, sizeof(*expr->data.selector.cases));
    }

    size_t case_idx = 0;
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "selector_option") == 0) {
            /* Find name (match) and value parts */
            TSNode match_node = ts_node_child_by_field_name(child, "name", 4);
            TSNode value_node = ts_node_child_by_field_name(child, "value", 5);

            if (!ts_node_is_null(value_node)) {
                puppet_expr_t *value_expr = convert_expression(value_node, source);

                if (!ts_node_is_null(match_node)) {
                    const char *match_type = ts_node_type(match_node);
                    if (strcmp(match_type, "default") == 0) {
                        /* This is the default case */
                        expr->data.selector.default_value = value_expr;
                    } else {
                        /* Regular case */
                        expr->data.selector.cases[case_idx].match = convert_expression(match_node, source);
                        expr->data.selector.cases[case_idx].value = value_expr;
                        case_idx++;
                    }
                }
            }
        }
    }

    expr->data.selector.case_count = case_idx;
    return expr;
}

/* Convert function call expression */
static puppet_expr_t *convert_funcall(TSNode node, const char *source) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_FUNCALL;
    expr->loc = node_location(node);

    /* First check all children (including anonymous) for function name
     * This handles the 'type' keyword which is anonymous in the grammar */
    uint32_t total = ts_node_child_count(node);
    for (uint32_t i = 0; i < total && expr->data.funcall.name.data == NULL; i++) {
        TSNode child = ts_node_child(node, i);
        const char *type = ts_node_type(child);
        /* Check for 'type' keyword (anonymous node) or named 'name'/'type' nodes */
        if (strcmp(type, "type") == 0 || strcmp(type, "name") == 0) {
            char *name = node_text(child, source);
            expr->data.funcall.name = puppet_string_create(name);
            puppet_free(name);
        }
    }

    /* Find arguments and lambda from named children */
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "argument_list") == 0) {
            uint32_t arg_count = ts_node_named_child_count(child);
            expr->data.funcall.args.exprs = puppet_calloc(arg_count, sizeof(puppet_expr_t*));
            expr->data.funcall.args.count = 0;

            for (uint32_t j = 0; j < arg_count; j++) {
                TSNode arg = ts_node_named_child(child, j);
                /* Skip comment nodes in argument list */
                if (node_is(arg, "comment")) continue;
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

        /* Skip comment nodes */
        if (strcmp(type, "comment") == 0) continue;

        if (strcmp(type, "parameter_list") == 0) {
            /* Parse parameters */
            uint32_t param_count = ts_node_named_child_count(child);
            lambda->params.params = puppet_calloc(param_count, sizeof(puppet_param_t));

            for (uint32_t j = 0; j < param_count; j++) {
                TSNode param = ts_node_named_child(child, j);
                /*
                 * Handle both typed and untyped parameters:
                 * - typed: parameter -> typed_parameter -> regular_parameter -> variable -> name
                 * - untyped: parameter -> regular_parameter -> variable -> name
                 */
                TSNode typed = find_child(param, "typed_parameter");
                TSNode reg = ts_node_is_null(typed) ?
                    find_child(param, "regular_parameter") :
                    find_child(typed, "regular_parameter");
                if (ts_node_is_null(reg)) reg = param;
                TSNode var_node = find_child(reg, "variable");
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
                const char *stmt_type = ts_node_type(stmt_node);
                if (strcmp(stmt_type, "statement") == 0) {
                    uint32_t stmt_children = ts_node_named_child_count(stmt_node);

                    /* Single child: check if it's a simple expression type */
                    if (stmt_children == 1) {
                        TSNode inner = ts_node_named_child(stmt_node, 0);
                        const char *inner_type = ts_node_type(inner);
                        /* Expression types that should be treated as expression body */
                        if (strcmp(inner_type, "binary") == 0 ||
                            strcmp(inner_type, "variable") == 0 ||
                            strcmp(inner_type, "number") == 0 ||
                            strcmp(inner_type, "function_call") == 0 ||
                            strcmp(inner_type, "unary") == 0 ||
                            strcmp(inner_type, "array") == 0 ||
                            strcmp(inner_type, "hash") == 0 ||
                            strcmp(inner_type, "double_quoted_string") == 0 ||
                            strcmp(inner_type, "single_quoted_string") == 0) {
                            lambda->expr_body = convert_expression(inner, source);
                            continue;
                        }
                    }

                    /* Multiple children: check for variable + access pattern ($v[0]) */
                    if (stmt_children >= 2) {
                        TSNode first = ts_node_named_child(stmt_node, 0);
                        TSNode second = ts_node_named_child(stmt_node, 1);
                        if (node_is(first, "variable") && node_is(second, "access")) {
                            /* Build index expression from variable and access */
                            puppet_expr_t *var_expr = convert_variable(first, source);
                            /* Chain all access nodes */
                            for (uint32_t j = 1; j < stmt_children; j++) {
                                TSNode acc = ts_node_named_child(stmt_node, j);
                                if (node_is(acc, "access")) {
                                    var_expr = build_index_expr(var_expr, acc, source);
                                }
                            }
                            lambda->expr_body = var_expr;
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
    /* node = call_method_with_lambda: a `call_method` plus an optional `lambda`.
     * Lower the method call (receiver + argument_list) via build_method_funcall,
     * then attach the lambda. Sharing build_method_funcall means call arguments
     * (e.g. reduce(0) |..| {}) are carried, not dropped. */
    puppet_expr_t *expr = NULL;
    TSNode cm = find_child(node, "call_method");
    if (!ts_node_is_null(cm)) {
        TSNode access = find_child(cm, "named_access");
        if (!ts_node_is_null(access)) {
            expr = build_method_funcall(access, find_child(cm, "argument_list"), source);
        }
    }
    if (!expr) {
        expr = puppet_calloc(1, sizeof(puppet_expr_t));
        expr->type = PUPPET_EXPR_FUNCALL;
        expr->loc = node_location(node);
    }

    TSNode lambda = find_child(node, "lambda");
    if (!ts_node_is_null(lambda)) {
        expr->data.funcall.lambda = convert_lambda(lambda, source);
    }

    return expr;
}

/* Lower a method call to a function call: the named_access node supplies the
 * receiver object + method name; arglist (a null node if absent) supplies any
 * extra call arguments. Produces method(receiver, arg1, ...). Every relevant
 * builtin (dig, sort, keys, values, size, count, ...) already takes the
 * receiver as its first positional argument, so the existing puppet_func_*
 * dispatch handles the result unchanged. */
static puppet_expr_t *build_method_funcall(TSNode named_access, TSNode arglist,
                                           const char *source) {
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_FUNCALL;
    expr->loc = node_location(named_access);

    uint32_t count = ts_node_named_child_count(named_access);

    /* Receiver = first child, with any chained access ([i]) applied. */
    puppet_expr_t *recv = NULL;
    if (count >= 1) {
        recv = convert_expression(ts_node_named_child(named_access, 0), source);
        for (uint32_t i = 1; i + 1 < count; i++) {
            TSNode child = ts_node_named_child(named_access, i);
            if (node_is(child, "access")) {
                recv = build_index_expr(recv, child, source);
            }
        }
    }

    /* Method name = last child (a name/type node). */
    if (count >= 2) {
        TSNode field = ts_node_named_child(named_access, count - 1);
        char *method = node_text(field, source);
        expr->data.funcall.name = puppet_string_create(method);
        puppet_free(method);
    }

    /* Arguments = [receiver] followed by the call's argument_list (if any). */
    uint32_t extra = ts_node_is_null(arglist) ? 0 : ts_node_named_child_count(arglist);
    expr->data.funcall.args.exprs = puppet_calloc(1 + extra, sizeof(puppet_expr_t *));
    expr->data.funcall.args.count = 0;
    if (recv) {
        expr->data.funcall.args.exprs[expr->data.funcall.args.count++] = recv;
    }
    for (uint32_t j = 0; j < extra; j++) {
        TSNode arg = ts_node_named_child(arglist, j);
        if (node_is(arg, "comment")) continue;
        puppet_expr_t *arg_expr = convert_expression(arg, source);
        if (arg_expr) {
            expr->data.funcall.args.exprs[expr->data.funcall.args.count++] = arg_expr;
        }
    }

    return expr;
}

/* Convert a method call: obj.method, obj.method(), or obj.method(args). The
 * `call_method` node holds a `named_access` child plus an optional sibling
 * `argument_list`. */
static puppet_expr_t *convert_call_method(TSNode node, const char *source) {
    TSNode na = find_child(node, "named_access");
    if (ts_node_is_null(na)) {
        uint32_t count = ts_node_named_child_count(node);
        return count > 0 ? convert_expression(ts_node_named_child(node, 0), source) : NULL;
    }
    return build_method_funcall(na, find_child(node, "argument_list"), source);
}

/* Convert bare named_access (obj.method, no parens) by lowering to a funcall. */
static puppet_expr_t *convert_named_access(TSNode node, const char *source) {
    TSNode no_args = {0};  /* null node */
    return build_method_funcall(node, no_args, source);
}

/* Main expression conversion dispatcher */
/* Extract the "value expression" of a block used as an rvalue: the last
 * statement if it's an expression-statement. Otherwise returns NULL. */
static puppet_expr_t *block_last_expr(TSNode block_node, const char *source) {
    if (ts_node_is_null(block_node)) return NULL;
    uint32_t count = ts_node_named_child_count(block_node);
    for (int32_t i = (int32_t)count - 1; i >= 0; i--) {
        TSNode child = ts_node_named_child(block_node, (uint32_t)i);
        const char *ctype = ts_node_type(child);
        if (strcmp(ctype, "comment") == 0) continue;
        /* The last meaningful child IS the value expression */
        return convert_expression(child, source);
    }
    return NULL;
}

static puppet_expr_t *convert_expression(TSNode node, const char *source) {
    if (ts_node_is_null(node)) return NULL;

    const char *type = ts_node_type(node);

    /* "if" used as an rvalue expression (Puppet 4+):
     *   $x = if cond { a } elsif cond2 { b } else { c }
     * Convert to nested ternary (PUPPET_EXPR_CONDITIONAL). Only the last
     * expression of each block is kept as the branch value — multi-statement
     * rvalue blocks are uncommon and would need a full block-eval primitive. */
    if (strcmp(type, "if") == 0) {
        /* Collect branches: list of (cond, value_expr) + optional else value */
        TSNode elsif_conds[16];
        TSNode elsif_blocks[16];
        size_t elsif_count = 0;
        TSNode if_cond = {0};
        TSNode if_block = {0};
        TSNode else_block = {0};
        bool have_else = false;

        uint32_t cnt = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < cnt; i++) {
            TSNode c = ts_node_named_child(node, i);
            const char *ct = ts_node_type(c);
            if (strcmp(ct, "condition") == 0 && ts_node_is_null(if_cond)) {
                if_cond = c;
            } else if (strcmp(ct, "block") == 0 && ts_node_is_null(if_block)) {
                if_block = c;
            } else if (strcmp(ct, "elsif") == 0) {
                if (elsif_count < 16) {
                    elsif_conds[elsif_count] = find_child(c, "condition");
                    elsif_blocks[elsif_count] = find_child(c, "block");
                    elsif_count++;
                }
            } else if (strcmp(ct, "else") == 0) {
                else_block = find_child(c, "block");
                have_else = true;
            }
        }

        /* Build the else value first, then wrap backwards with elsif/if */
        puppet_expr_t *else_val = NULL;
        if (have_else && !ts_node_is_null(else_block)) {
            else_val = block_last_expr(else_block, source);
        }
        if (!else_val) {
            /* Default to undef */
            else_val = puppet_calloc(1, sizeof(puppet_expr_t));
            else_val->type = PUPPET_EXPR_VALUE;
            else_val->loc = node_location(node);
            else_val->data.value = puppet_calloc(1, sizeof(puppet_value_t));
            else_val->data.value->type = PUPPET_VALUE_UNDEF;
        }

        puppet_expr_t *acc = else_val;
        for (int32_t i = (int32_t)elsif_count - 1; i >= 0; i--) {
            puppet_expr_t *cond_expr = convert_expression(
                ts_node_is_null(elsif_conds[i]) ? elsif_conds[i] :
                ts_node_named_child(elsif_conds[i], 0), source);
            puppet_expr_t *then_val = block_last_expr(elsif_blocks[i], source);
            puppet_expr_t *cond_node = puppet_calloc(1, sizeof(puppet_expr_t));
            cond_node->type = PUPPET_EXPR_CONDITIONAL;
            cond_node->loc = node_location(node);
            cond_node->data.conditional.condition = cond_expr;
            cond_node->data.conditional.then_expr = then_val;
            cond_node->data.conditional.else_expr = acc;
            acc = cond_node;
        }

        puppet_expr_t *top_cond = NULL;
        if (!ts_node_is_null(if_cond)) {
            uint32_t cc = ts_node_named_child_count(if_cond);
            if (cc > 0) top_cond = convert_expression(ts_node_named_child(if_cond, 0), source);
        }
        puppet_expr_t *if_val = block_last_expr(if_block, source);
        puppet_expr_t *root = puppet_calloc(1, sizeof(puppet_expr_t));
        root->type = PUPPET_EXPR_CONDITIONAL;
        root->loc = node_location(node);
        root->data.conditional.condition = top_cond;
        root->data.conditional.then_expr = if_val;
        root->data.conditional.else_expr = acc;
        return root;
    }

    /* Handle resource references: Type[title] where type and access are siblings in "argument" node
     * Parse tree looks like:
     *   argument: 'Type[title]'
     *     type: 'Type'
     *     access: 'title'
     */
    if (strcmp(type, "argument") == 0) {
        TSNode type_child = find_child(node, "type");
        TSNode access_child = find_child(node, "access");

        if (!ts_node_is_null(type_child) && !ts_node_is_null(access_child)) {
            /* This is a resource reference Type[title] */
            puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
            expr->type = PUPPET_EXPR_RESOURCE_REF;
            expr->loc = node_location(node);

            /* Get the type name from the type node */
            char *type_str = node_text(type_child, source);
            expr->data.resource_ref.type = puppet_string_create(type_str);
            puppet_free(type_str);

            /* Get the title from the first access_element */
            TSNode access_elem = find_child(access_child, "access_element");
            if (!ts_node_is_null(access_elem)) {
                expr->data.resource_ref.title = convert_access_element_expr(access_elem, source);
            } else {
                /* Fallback: use entire access content */
                expr->data.resource_ref.title = convert_expression(access_child, source);
            }

            return expr;
        }

        /* Handle variable with chained bracket access: $var[key1][key2] */
        TSNode var_child = find_child(node, "variable");
        if (!ts_node_is_null(var_child)) {
            puppet_expr_t *var_expr = convert_variable(var_child, source);
            /* Build chained index for all access nodes */
            uint32_t arg_count = ts_node_named_child_count(node);
            for (uint32_t j = 0; j < arg_count; j++) {
                TSNode arg_child = ts_node_named_child(node, j);
                if (strcmp(ts_node_type(arg_child), "access") == 0) {
                    var_expr = build_index_expr(var_expr, arg_child, source);
                }
            }
            return var_expr;
        }
    }

    if (strcmp(type, "variable") == 0)
        return convert_variable(node, source);
    if (strcmp(type, "number") == 0)
        return convert_number(node, source);
    if (strcmp(type, "single_quoted_string") == 0 ||
        strcmp(type, "double_quoted_string") == 0)
        return convert_string_literal(node, source);
    if (strcmp(type, "boolean") == 0 ||
        strcmp(type, "true") == 0 ||
        strcmp(type, "false") == 0)
        return convert_boolean(node, source);
    if (strcmp(type, "undef") == 0)
        return convert_undef(node, source);
    if (strcmp(type, "regex") == 0)
        return convert_regex(node, source);
    if (strcmp(type, "binary") == 0)
        return convert_binary(node, source);
    if (strcmp(type, "unary") == 0)
        return convert_unary(node, source);
    if (strcmp(type, "array") == 0)
        return convert_array(node, source);
    if (strcmp(type, "hash") == 0)
        return convert_hash(node, source);
    if (strcmp(type, "selector") == 0)
        return convert_selector(node, source);
    if (strcmp(type, "function_call") == 0 || strcmp(type, "statement_function") == 0)
        return convert_funcall(node, source);
    if (strcmp(type, "call_method_with_lambda") == 0)
        return convert_method_with_lambda(node, source);
    if (strcmp(type, "call_method") == 0)
        return convert_call_method(node, source);
    if (strcmp(type, "named_access") == 0)
        return convert_named_access(node, source);
    if (strcmp(type, "name") == 0)
        return convert_name_to_value(node, source);

    /* Handle resource references: Type[title] appearing as statement node or array_element
     * Parse tree looks like:
     *   statement/array_element: 'Type[title]'
     *     type: 'Type'
     *     access: '[title]'
     */
    if (strcmp(type, "statement") == 0 || strcmp(type, "array_element") == 0) {
        TSNode type_child = find_child(node, "type");
        TSNode access_child = find_child(node, "access");

        if (!ts_node_is_null(type_child) && !ts_node_is_null(access_child)) {
            /* This is a resource reference Type[title] */
            puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
            expr->type = PUPPET_EXPR_RESOURCE_REF;
            expr->loc = node_location(node);

            /* Get the type name */
            char *type_str = node_text(type_child, source);
            expr->data.resource_ref.type = puppet_string_create(type_str);
            puppet_free(type_str);

            /* Get the title from the first access_element */
            TSNode access_elem = find_child(access_child, "access_element");
            if (!ts_node_is_null(access_elem)) {
                expr->data.resource_ref.title = convert_access_element_expr(access_elem, source);
            } else {
                expr->data.resource_ref.title = convert_expression(access_child, source);
            }

            return expr;
        }
    }

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

    /* Check for selector expression */
    TSNode selector_child = find_child(node, "selector");

    /* Check for access pattern (resource reference or hash access) */
    TSNode access_child = find_child(node, "access");

    /* Check for lhs: field which indicates a selector control variable */
    TSNode lhs_node = ts_node_child_by_field_name(node, "lhs", 3);

    uint32_t count = ts_node_named_child_count(node);
    puppet_expr_t *pending_var_expr = NULL;  /* Track variable for chained access */
    puppet_expr_t *pending_type_expr = NULL; /* Track type for resource reference */

    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "name") == 0 && attr.name.data == NULL) {
            char *name = node_text(child, source);
            attr.name = puppet_string_create(name);
            puppet_free(name);
        } else if (strcmp(type, "arrow") != 0) {
            // Check if this could be the attribute name (for keywords like unless, onlyif, etc.)
            if (attr.name.data == NULL && i == 0) {
                // First child might be the attribute name if it's not "name" type
                char *name = node_text(child, source);
                // Check if it's a valid identifier (not an expression)
                if (name && strlen(name) > 0 && !strchr(name, ' ') && !strchr(name, '(')) {
                    attr.name = puppet_string_create(name);
                    puppet_free(name);
                    continue;
                }
                puppet_free(name);
            }

            /* Handle variable + chained access as combined index expression */
            if (strcmp(type, "variable") == 0) {
                pending_var_expr = convert_variable(child, source);
                continue;
            } else if (strcmp(type, "access") == 0 && pending_var_expr) {
                /* Build chained index expression */
                pending_var_expr = build_index_expr(pending_var_expr, child, source);
                continue;
            }

            /* Handle type + access pattern for resource references (e.g., Service['name']) */
            if (strcmp(type, "type") == 0 && !ts_node_is_null(access_child)) {
                /* Store type string for building resource reference */
                char *type_str = node_text(child, source);
                pending_type_expr = puppet_calloc(1, sizeof(puppet_expr_t));
                pending_type_expr->type = PUPPET_EXPR_RESOURCE_REF;
                pending_type_expr->loc = node_location(child);
                pending_type_expr->data.resource_ref.type = puppet_string_create(type_str);
                puppet_free(type_str);
                continue;
            } else if (strcmp(type, "access") == 0 && pending_type_expr) {
                /* Complete the resource reference with the title */
                TSNode access_elem = find_child(child, "access_element");
                if (!ts_node_is_null(access_elem)) {
                    TSNode value_node = ts_node_named_child(access_elem, 0);
                    pending_type_expr->data.resource_ref.title = convert_expression(value_node, source);
                } else {
                    pending_type_expr->data.resource_ref.title = convert_expression(child, source);
                }
                attr.value = pending_type_expr;
                pending_type_expr = NULL;
                continue;
            }

            /* Handle variable + selector pattern (e.g., certpath => $var ? { ... }) */
            if (strcmp(type, "variable") == 0 && !ts_node_is_null(selector_child)) {
                /* This variable is the selector control, skip - handled with selector */
                continue;
            } else if (strcmp(type, "selector") == 0 && !ts_node_is_null(lhs_node)) {
                /* Selector with lhs control variable */
                puppet_expr_t *selector_expr = convert_selector(child, source);
                selector_expr->data.selector.control = convert_expression(lhs_node, source);
                attr.value = selector_expr;
                continue;
            }

            /* If we have a pending variable expression (with or without access), use it */
            if (pending_var_expr && attr.value == NULL) {
                attr.value = pending_var_expr;
                pending_var_expr = NULL;
            } else {
                attr.value = convert_expression(child, source);
            }
        }
    }

    /* If we ended with a pending variable expression, use it as the value */
    if (pending_var_expr && attr.value == NULL) {
        attr.value = pending_var_expr;
    }

    return attr;
}

/**
 * @brief Convert an access_element node to a full expression, handling chained hash access.
 *
 * An access_element like $myhash['a']['b'] is parsed by tree-sitter as:
 *   access_element
 *     variable ($myhash)
 *     access (['a'])
 *     access (['b'])
 *
 * This function builds the full chained index expression instead of
 * just taking the first child (which would return only $myhash).
 */
static puppet_expr_t *convert_access_element_expr(TSNode access_elem, const char *source) {
    uint32_t named_count = ts_node_named_child_count(access_elem);
    if (named_count == 0) return NULL;

    TSNode first = ts_node_named_child(access_elem, 0);

    /* Check if first child is a variable followed by access nodes */
    if (named_count >= 2 && node_is(first, "variable")) {
        TSNode second = ts_node_named_child(access_elem, 1);
        if (node_is(second, "access")) {
            /* Variable with chained access: $var['a']['b'] */
            puppet_expr_t *expr = convert_variable(first, source);
            for (uint32_t j = 1; j < named_count; j++) {
                TSNode acc = ts_node_named_child(access_elem, j);
                if (node_is(acc, "access")) {
                    expr = build_index_expr(expr, acc, source);
                }
            }
            return expr;
        }
    }

    /* Simple case: just convert the first child */
    return convert_expression(first, source);
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

    /* Check if this is a resource reference (Type[title]) instead of an index expression
     * Resource references have:
     * - A variable base (type name)
     * - The variable name starts with an uppercase letter (capitalized type)
     */
    if (object->type == PUPPET_EXPR_VARIABLE) {
        const char *var_name = object->data.variable.data;
        if (var_name && var_name[0] >= 'A' && var_name[0] <= 'Z') {
            /* This is a resource reference Type[title] */
            puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
            expr->type = PUPPET_EXPR_RESOURCE_REF;
            expr->loc = node_location(access_node);
            expr->data.resource_ref.type = puppet_string_create(var_name);
            expr->data.resource_ref.title = index_expr;

            /* Free the object variable expression since we've copied its data */
            puppet_expr_destroy(object);

            return expr;
        }
    }

    /* Regular index expression */
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_INDEX;
    expr->loc = node_location(access_node);
    expr->data.index.object = object;
    expr->data.index.index = index_expr;
    return expr;
}

/* Convert assignment: variable = expression or variable += expression */
static puppet_stmt_t *convert_assignment(TSNode node, const char *source) {
    puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
    stmt->loc = node_location(node);

    /* Check operator field to distinguish = vs += */
    TSNode op_node = ts_node_child_by_field_name(node, "operator", 8);
    bool is_append = false;
    if (!ts_node_is_null(op_node)) {
        char *op = node_text(op_node, source);
        is_append = (strcmp(op, "+=") == 0);
        puppet_free(op);
    }
    stmt->type = is_append ? PUPPET_STMT_APPEND : PUPPET_STMT_ASSIGNMENT;

    /*
     * Assignment can have multiple forms:
     * 1. Simple: $var = expr
     *    Children: variable, expr
     * 2. With bracket access on RHS: $var = $obj['key1']['key2']
     *    Children: variable, variable, access, access
     *    First variable is LHS, second + access nodes are the indexed expression
     * 3. With selector: $var = $cond ? { val1 => res1, default => res2 }
     *    Children: variable, lhs:variable, selector
     *    First variable is LHS, lhs: variable is selector control, selector has cases
     */

    /* Check for selector with lhs: control expression */
    TSNode selector_node = find_child(node, "selector");
    TSNode lhs_node = ts_node_child_by_field_name(node, "lhs", 3);

    if (!ts_node_is_null(selector_node) && !ts_node_is_null(lhs_node)) {
        /* Assignment with selector: $x = $y ? { ... } */
        /* First unnamed variable is the target */
        uint32_t count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_named_child(node, i);
            if (node_is(child, "variable")) {
                /* Check if this is the lhs: field (control) or just a variable (target) */
                if (ts_node_eq(child, lhs_node)) {
                    continue; /* Skip the control expression, handled below */
                }
                TSNode name_node = find_child(child, "name");
                if (!ts_node_is_null(name_node)) {
                    char *name = node_text(name_node, source);
                    stmt->data.assignment.variable = puppet_string_create(name);
                    puppet_free(name);
                }
                break;
            }
        }

        /* Build selector expression with control from lhs:
         *
         * The lhs field points only at the bare variable. When the source
         * is `$x[a][b] ? { ... }` the access nodes sit between lhs and the
         * selector as siblings — we must fold them into the control so the
         * full indexed expression is what's matched against, not just $x.
         */
        puppet_expr_t *selector_expr = convert_selector(selector_node, source);
        puppet_expr_t *control = convert_expression(lhs_node, source);
        bool past_lhs = false;
        uint32_t cnt2 = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < cnt2; i++) {
            TSNode child = ts_node_named_child(node, i);
            if (!past_lhs) {
                if (ts_node_eq(child, lhs_node)) past_lhs = true;
                continue;
            }
            if (ts_node_eq(child, selector_node)) break;
            if (strcmp(ts_node_type(child), "access") == 0) {
                control = build_index_expr(control, child, source);
            }
        }
        selector_expr->data.selector.control = control;
        stmt->data.assignment.value = selector_expr;
        return stmt;
    }

    bool lhs_set = false;
    puppet_expr_t *rhs_expr = NULL;
    puppet_expr_t *pending_type_ref = NULL;  /* For type + access pattern */

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
        } else if (strcmp(type, "type") == 0 && !rhs_expr) {
            /* Type node - could be resource reference like Type[title] */
            char *type_str = node_text(child, source);
            pending_type_ref = puppet_calloc(1, sizeof(puppet_expr_t));
            pending_type_ref->type = PUPPET_EXPR_RESOURCE_REF;
            pending_type_ref->loc = node_location(child);
            pending_type_ref->data.resource_ref.type = puppet_string_create(type_str);
            puppet_free(type_str);
        } else if (strcmp(type, "access") == 0) {
            if (pending_type_ref) {
                /* Complete the resource reference: Type[title] */
                TSNode access_elem = find_child(child, "access_element");
                if (!ts_node_is_null(access_elem)) {
                    TSNode value_node = ts_node_named_child(access_elem, 0);
                    pending_type_ref->data.resource_ref.title = convert_expression(value_node, source);
                } else {
                    pending_type_ref->data.resource_ref.title = convert_expression(child, source);
                }
                rhs_expr = pending_type_ref;
                pending_type_ref = NULL;
            } else if (rhs_expr) {
                /* Build index expression: rhs_expr[access] */
                rhs_expr = build_index_expr(rhs_expr, child, source);
            }
        } else {
            /* Other expression types */
            rhs_expr = convert_expression(child, source);
        }
    }

    /* Clean up if we had a pending type ref without access */
    if (pending_type_ref && !rhs_expr) {
        /* Just a bare type name - convert to string value */
        puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
        expr->type = PUPPET_EXPR_VALUE;
        expr->loc = pending_type_ref->loc;
        expr->data.value = puppet_value_create_string(
            pending_type_ref->data.resource_ref.type.data,
            pending_type_ref->data.resource_ref.type.len);
        puppet_expr_destroy(pending_type_ref);
        rhs_expr = expr;
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

    /* Count all resource_body nodes (for multi-instance declarations like:
     * class { 'c1': ; 'c2': ; 'c3': ; }
     */
    uint32_t body_count = 0;
    uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; i++) {
        TSNode child = ts_node_named_child(node, i);
        if (node_is(child, "resource_body")) {
            body_count++;
        }
    }

    if (body_count == 0) {
        return stmt;
    }

    /* Allocate instances for all bodies */
    stmt->data.resource.instances = puppet_calloc(body_count, sizeof(puppet_resource_instance_t));
    stmt->data.resource.instance_count = body_count;

    /* Process each resource_body */
    size_t inst_idx = 0;
    for (uint32_t i = 0; i < child_count && inst_idx < body_count; i++) {
        TSNode body = ts_node_named_child(node, i);
        if (!node_is(body, "resource_body")) {
            continue;
        }

        puppet_resource_instance_t *inst = &stmt->data.resource.instances[inst_idx++];

        /* Get title */
        TSNode title = find_child(body, "resource_title");
        if (!ts_node_is_null(title)) {
            uint32_t title_count = ts_node_named_child_count(title);
            if (title_count > 0) {
                /* Convert first child as base expression */
                puppet_expr_t *title_expr = convert_expression(ts_node_named_child(title, 0), source);

                /* Build chained index expressions for any access nodes */
                for (uint32_t j = 1; j < title_count; j++) {
                    TSNode access_node = ts_node_named_child(title, j);
                    if (strcmp(ts_node_type(access_node), "access") == 0) {
                        title_expr = build_index_expr(title_expr, access_node, source);
                    }
                }
                inst->title = title_expr;
            }
        }

        /* Get attributes */
        TSNode attr_list = find_child(body, "attribute_list");
        if (!ts_node_is_null(attr_list)) {
            uint32_t attr_count = ts_node_named_child_count(attr_list);
            inst->attributes = puppet_calloc(attr_count, sizeof(puppet_attribute_t));
            inst->attr_count = 0;

            for (uint32_t j = 0; j < attr_count; j++) {
                TSNode attr_node = ts_node_named_child(attr_list, j);
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
    stmt->data.class_def.inherits = NULL;  /* Initialize inherits to NULL */

    bool found_classname = false;  /* Track if we've seen the first classname (the class name itself) */

    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "classname") == 0) {
            TSNode name = find_child(child, "name");
            if (!ts_node_is_null(name)) {
                char *name_str = node_text(name, source);
                if (!found_classname) {
                    /* First classname is the class name */
                    stmt->data.class_def.name = puppet_string_create(name_str);
                    found_classname = true;
                } else {
                    /* Second classname is the parent class (inherits clause) */
                    stmt->data.class_def.inherits = puppet_calloc(1, sizeof(puppet_string_t));
                    *stmt->data.class_def.inherits = puppet_string_create(name_str);
                }
                puppet_free(name_str);
            }
        } else if (strcmp(type, "block") == 0) {
            stmt->data.class_def.body = convert_block(child, source);
        } else if (strcmp(type, "parameter_list") == 0) {
            /* Parse class parameters */
            uint32_t param_count = ts_node_named_child_count(child);
            stmt->data.class_def.params.params = puppet_calloc(param_count, sizeof(puppet_param_t));
            stmt->data.class_def.params.count = 0;

            for (uint32_t j = 0; j < param_count; j++) {
                TSNode param = ts_node_named_child(child, j);
                if (!node_is(param, "parameter")) continue;

                /* Handle both typed and untyped parameters:
                 * - typed: parameter -> typed_parameter -> regular_parameter -> variable
                 * - untyped: parameter -> regular_parameter -> variable
                 */
                TSNode typed_param = find_child(param, "typed_parameter");
                TSNode reg_param = ts_node_is_null(typed_param) ?
                    find_child(param, "regular_parameter") :
                    find_child(typed_param, "regular_parameter");
                if (ts_node_is_null(reg_param)) reg_param = param;

                TSNode var_node = find_child(reg_param, "variable");
                if (ts_node_is_null(var_node)) continue;

                TSNode name_node = find_child(var_node, "name");
                if (ts_node_is_null(name_node)) continue;

                /* Get parameter name */
                char *name_str = node_text(name_node, source);
                puppet_param_t *p = &stmt->data.class_def.params.params[stmt->data.class_def.params.count];
                p->name = puppet_string_create(name_str);
                puppet_free(name_str);

                /* Extract the type constraint expression, if any. The
                 * typed_parameter node wraps:
                 *   <type_expr>  e.g. Hash, String[1], Pattern[/.../]
                 *   regular_parameter
                 * We take the first named child that isn't the
                 * regular_parameter wrapper. For the raw source text
                 * we splice everything from the start of the
                 * typed_param up to the start of the regular_parameter
                 * — node_text on the type child only catches the base
                 * name (`String`), losing the bracket content
                 * (`String[1]`). */
                if (!ts_node_is_null(typed_param)) {
                    uint32_t tc = ts_node_named_child_count(typed_param);
                    TSNode reg_in_typed = {0};
                    bool has_reg = false;
                    for (uint32_t k = 0; k < tc; k++) {
                        TSNode tch = ts_node_named_child(typed_param, k);
                        if (node_is(tch, "regular_parameter")) {
                            reg_in_typed = tch;
                            has_reg = true;
                        } else if (!p->type_expr) {
                            p->type_expr = convert_expression(tch, source);
                        }
                    }
                    uint32_t start = ts_node_start_byte(typed_param);
                    uint32_t end = has_reg ? ts_node_start_byte(reg_in_typed)
                                           : ts_node_end_byte(typed_param);
                    if (end > start) {
                        size_t len = end - start;
                        char *raw = puppet_malloc(len + 1);
                        memcpy(raw, source + start, len);
                        raw[len] = '\0';
                        /* Trim trailing whitespace. */
                        while (len > 0 && (raw[len-1] == ' ' || raw[len-1] == '\t' ||
                                           raw[len-1] == '\n' || raw[len-1] == '\r')) {
                            raw[--len] = '\0';
                        }
                        p->type_str = puppet_string_create(raw);
                        puppet_free(raw);
                    }
                }

                /* Check for default value - it's the second child (first is the param variable) */
                uint32_t reg_param_children = ts_node_named_child_count(reg_param);
                if (reg_param_children > 1) {
                    /* The second named child is the default value expression */
                    TSNode default_node = ts_node_named_child(reg_param, 1);
                    p->default_value = convert_expression(default_node, source);
                    /* Chain any following "access" sibling nodes onto the default:
                     * tree-sitter flattens $facts['os']['name'] into variable + access + access */
                    for (uint32_t k = 2; k < reg_param_children; k++) {
                        TSNode sibling = ts_node_named_child(reg_param, k);
                        if (node_is(sibling, "access")) {
                            p->default_value = build_index_expr(p->default_value, sibling, source);
                        }
                    }
                }

                stmt->data.class_def.params.count++;
            }
        }
    }

    return stmt;
}

/* Convert define definition */
static puppet_stmt_t *convert_define_def(TSNode node, const char *source) {
    puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
    stmt->type = PUPPET_STMT_DEFINE;
    stmt->loc = node_location(node);

    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "classname") == 0) {
            TSNode name = find_child(child, "name");
            if (!ts_node_is_null(name)) {
                char *name_str = node_text(name, source);
                stmt->data.define.name = puppet_string_create(name_str);
                puppet_free(name_str);
            }
        } else if (strcmp(type, "block") == 0) {
            stmt->data.define.body = convert_block(child, source);
        } else if (strcmp(type, "parameter_list") == 0) {
            /* Parse define parameters */
            uint32_t param_count = ts_node_named_child_count(child);
            stmt->data.define.params.params = puppet_calloc(param_count, sizeof(puppet_param_t));
            stmt->data.define.params.count = 0;

            for (uint32_t j = 0; j < param_count; j++) {
                TSNode param = ts_node_named_child(child, j);
                if (!node_is(param, "parameter")) continue;

                /* Handle both typed and untyped parameters:
                 * - typed: parameter -> typed_parameter -> regular_parameter -> variable
                 * - untyped: parameter -> regular_parameter -> variable
                 */
                TSNode typed_param = find_child(param, "typed_parameter");
                TSNode reg_param = ts_node_is_null(typed_param) ?
                    find_child(param, "regular_parameter") :
                    find_child(typed_param, "regular_parameter");
                if (ts_node_is_null(reg_param)) reg_param = param;

                TSNode var_node = find_child(reg_param, "variable");
                if (ts_node_is_null(var_node)) continue;

                TSNode name_node = find_child(var_node, "name");
                if (ts_node_is_null(name_node)) continue;

                /* Get parameter name */
                char *name_str = node_text(name_node, source);
                puppet_param_t *p = &stmt->data.define.params.params[stmt->data.define.params.count];
                p->name = puppet_string_create(name_str);
                puppet_free(name_str);

                /* Extract the type constraint expression and raw text
                 * (see convert_class_def for the rationale). */
                if (!ts_node_is_null(typed_param)) {
                    uint32_t tc = ts_node_named_child_count(typed_param);
                    TSNode reg_in_typed = {0};
                    bool has_reg = false;
                    for (uint32_t k = 0; k < tc; k++) {
                        TSNode tch = ts_node_named_child(typed_param, k);
                        if (node_is(tch, "regular_parameter")) {
                            reg_in_typed = tch;
                            has_reg = true;
                        } else if (!p->type_expr) {
                            p->type_expr = convert_expression(tch, source);
                        }
                    }
                    uint32_t start = ts_node_start_byte(typed_param);
                    uint32_t end = has_reg ? ts_node_start_byte(reg_in_typed)
                                           : ts_node_end_byte(typed_param);
                    if (end > start) {
                        size_t len = end - start;
                        char *raw = puppet_malloc(len + 1);
                        memcpy(raw, source + start, len);
                        raw[len] = '\0';
                        while (len > 0 && (raw[len-1] == ' ' || raw[len-1] == '\t' ||
                                           raw[len-1] == '\n' || raw[len-1] == '\r')) {
                            raw[--len] = '\0';
                        }
                        p->type_str = puppet_string_create(raw);
                        puppet_free(raw);
                    }
                }

                /* Check for default value - it's the second child (first is the param variable) */
                uint32_t reg_param_children = ts_node_named_child_count(reg_param);
                if (reg_param_children > 1) {
                    /* The second named child is the default value expression */
                    TSNode default_node = ts_node_named_child(reg_param, 1);
                    p->default_value = convert_expression(default_node, source);
                    /* Chain any following "access" sibling nodes onto the default:
                     * tree-sitter flattens $facts['os']['name'] into variable + access + access */
                    for (uint32_t k = 2; k < reg_param_children; k++) {
                        TSNode sibling = ts_node_named_child(reg_param, k);
                        if (node_is(sibling, "access")) {
                            p->default_value = build_index_expr(p->default_value, sibling, source);
                        }
                    }
                }

                stmt->data.define.params.count++;
            }
        }
    }

    return stmt;
}

/* Convert a tree-sitter "parameter_list" node into a puppet_param_list_t.
 * Mirrors the parameter extraction in convert_define_def / convert_class_def
 * (name, optional typed_parameter -> type_expr + raw type_str, optional
 * default value with chained access). */
static void convert_param_list(TSNode list_node, const char *source,
                               puppet_param_list_t *out) {
    uint32_t param_count = ts_node_named_child_count(list_node);
    out->params = puppet_calloc(param_count, sizeof(puppet_param_t));
    out->count = 0;

    for (uint32_t j = 0; j < param_count; j++) {
        TSNode param = ts_node_named_child(list_node, j);
        if (!node_is(param, "parameter")) continue;

        TSNode typed_param = find_child(param, "typed_parameter");
        TSNode reg_param = ts_node_is_null(typed_param) ?
            find_child(param, "regular_parameter") :
            find_child(typed_param, "regular_parameter");
        if (ts_node_is_null(reg_param)) reg_param = param;

        TSNode var_node = find_child(reg_param, "variable");
        if (ts_node_is_null(var_node)) continue;
        TSNode name_node = find_child(var_node, "name");
        if (ts_node_is_null(name_node)) continue;

        char *name_str = node_text(name_node, source);
        puppet_param_t *p = &out->params[out->count];
        p->name = puppet_string_create(name_str);
        puppet_free(name_str);

        /* Type constraint expression + raw text (e.g. "String[1]") */
        if (!ts_node_is_null(typed_param)) {
            uint32_t tc = ts_node_named_child_count(typed_param);
            TSNode reg_in_typed = {0};
            bool has_reg = false;
            for (uint32_t k = 0; k < tc; k++) {
                TSNode tch = ts_node_named_child(typed_param, k);
                if (node_is(tch, "regular_parameter")) {
                    reg_in_typed = tch;
                    has_reg = true;
                } else if (!p->type_expr) {
                    p->type_expr = convert_expression(tch, source);
                }
            }
            uint32_t start = ts_node_start_byte(typed_param);
            uint32_t end = has_reg ? ts_node_start_byte(reg_in_typed)
                                   : ts_node_end_byte(typed_param);
            if (end > start) {
                size_t len = end - start;
                char *raw = puppet_malloc(len + 1);
                memcpy(raw, source + start, len);
                raw[len] = '\0';
                while (len > 0 && (raw[len-1] == ' ' || raw[len-1] == '\t' ||
                                   raw[len-1] == '\n' || raw[len-1] == '\r')) {
                    raw[--len] = '\0';
                }
                p->type_str = puppet_string_create(raw);
                puppet_free(raw);
            }
        }

        /* Default value: second named child of regular_parameter, with any
         * trailing access nodes chained on (e.g. $x = $facts['os']). */
        uint32_t reg_param_children = ts_node_named_child_count(reg_param);
        if (reg_param_children > 1) {
            TSNode default_node = ts_node_named_child(reg_param, 1);
            p->default_value = convert_expression(default_node, source);
            for (uint32_t k = 2; k < reg_param_children; k++) {
                TSNode sibling = ts_node_named_child(reg_param, k);
                if (node_is(sibling, "access")) {
                    p->default_value = build_index_expr(p->default_value, sibling, source);
                }
            }
        }

        out->count++;
    }
}

/* Convert a user-defined function: function NAME(params) >> RetType { body } */
static puppet_stmt_t *convert_function_def(TSNode node, const char *source) {
    puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
    stmt->type = PUPPET_STMT_FUNCTION_DEF;
    stmt->loc = node_location(node);

    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "classname") == 0) {
            TSNode name = find_child(child, "name");
            if (!ts_node_is_null(name)) {
                char *name_str = node_text(name, source);
                stmt->data.function_def.name = puppet_string_create(name_str);
                puppet_free(name_str);
            }
        } else if (strcmp(type, "parameter_list") == 0) {
            convert_param_list(child, source, &stmt->data.function_def.params);
        } else if (strcmp(type, "return_type") == 0) {
            /* return_type = '>>' type [access]. Store the type expr + raw text;
             * enforcement is item 8, not here. */
            TSNode rt = find_child(child, "type");
            if (!ts_node_is_null(rt)) {
                stmt->data.function_def.return_type_expr = convert_expression(rt, source);
            }
            char *raw = node_text(child, source);
            if (raw) {
                /* drop the leading ">>" and surrounding whitespace */
                char *p = raw;
                if (p[0] == '>' && p[1] == '>') p += 2;
                while (*p == ' ' || *p == '\t') p++;
                stmt->data.function_def.return_type_str = puppet_string_create(p);
                puppet_free(raw);
            }
        } else if (strcmp(type, "block") == 0) {
            stmt->data.function_def.body = convert_block(child, source);
        }
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
            /* Check for variable + chained access pattern (e.g., if $hash['a']['b']) */
            TSNode var_child = find_child(child, "variable");
            uint32_t cond_count = ts_node_named_child_count(child);

            if (!ts_node_is_null(var_child)) {
                puppet_expr_t *cond_expr = convert_variable(var_child, source);
                /* Build chained index for all access nodes */
                for (uint32_t j = 0; j < cond_count; j++) {
                    TSNode cond_child = ts_node_named_child(child, j);
                    if (strcmp(ts_node_type(cond_child), "access") == 0) {
                        cond_expr = build_index_expr(cond_expr, cond_child, source);
                    }
                }
                branch->condition = cond_expr;
            } else if (cond_count > 0) {
                branch->condition = convert_expression(ts_node_named_child(child, 0), source);
            }
        } else if (strcmp(type, "block") == 0 && branch->body.stmts == NULL) {
            branch->body = convert_block(child, source);
        } else if (strcmp(type, "elsif") == 0) {
            /* Handle elsif branches - create new branch and chain to previous */
            puppet_if_branch_t *elsif_branch = puppet_calloc(1, sizeof(puppet_if_branch_t));
            branch->next = elsif_branch;
            branch = elsif_branch;

            /* Extract condition and block from elsif */
            TSNode elsif_cond = find_child(child, "condition");
            TSNode elsif_block = find_child(child, "block");

            if (!ts_node_is_null(elsif_cond)) {
                /* Check for variable + chained access pattern */
                TSNode var_child = find_child(elsif_cond, "variable");
                uint32_t cond_count = ts_node_named_child_count(elsif_cond);

                if (!ts_node_is_null(var_child)) {
                    puppet_expr_t *cond_expr = convert_variable(var_child, source);
                    /* Build chained index for all access nodes */
                    for (uint32_t j = 0; j < cond_count; j++) {
                        TSNode cond_child = ts_node_named_child(elsif_cond, j);
                        if (strcmp(ts_node_type(cond_child), "access") == 0) {
                            cond_expr = build_index_expr(cond_expr, cond_child, source);
                        }
                    }
                    branch->condition = cond_expr;
                } else if (cond_count > 0) {
                    branch->condition = convert_expression(ts_node_named_child(elsif_cond, 0), source);
                }
            }
            if (!ts_node_is_null(elsif_block)) {
                branch->body = convert_block(elsif_block, source);
            }
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

/* Convert `type Name = <type-expr>` (e.g. `type Stdlib::Fqdn = Pattern[/.../]`).
 * The grammar node is `type_alias` with the `operator` field = "=", the LHS
 * alias name (a `type` node) and the RHS type expression. We capture the RHS as
 * raw source text and hand it to value_matches_type_str at match time, which
 * already understands Pattern/Variant/Enum/Optional/builtins. */
static puppet_stmt_t *convert_type_alias(TSNode node, const char *source) {
    puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
    stmt->type = PUPPET_STMT_TYPE_ALIAS;
    stmt->loc = node_location(node);

    /* The alias name is the first `type` child (before the `=`). */
    TSNode name_node = find_child(node, "type");
    if (!ts_node_is_null(name_node)) {
        char *nm = node_text(name_node, source);
        stmt->data.type_alias.name = puppet_string_create(nm);
        puppet_free(nm);
    }

    /* The RHS is everything after the `=` operator to the end of the node,
     * trimmed — robust regardless of whether it parses as type/access/array/
     * hash children. */
    TSNode op = ts_node_child_by_field_name(node, "operator", 8);
    if (!ts_node_is_null(op)) {
        uint32_t s = ts_node_end_byte(op);
        uint32_t e = ts_node_end_byte(node);
        while (s < e && (source[s] == ' ' || source[s] == '\t' ||
                         source[s] == '\n' || source[s] == '\r')) s++;
        while (e > s && (source[e-1] == ' ' || source[e-1] == '\t' ||
                         source[e-1] == '\n' || source[e-1] == '\r')) e--;
        size_t len = (size_t)(e - s);
        char *rhs = puppet_malloc(len + 1);
        memcpy(rhs, source + s, len);
        rhs[len] = '\0';
        stmt->data.type_alias.type_str = puppet_string_create(rhs);
        puppet_free(rhs);
    }

    return stmt;
}

/* Convert `unless <cond> { body } [else { else_body }]`.
 * The grammar node is `unless` with children: condition, block, optional else.
 * Without this the node fell through to the expression fallback and the body
 * block was silently dropped, so resources declared under an `unless` guard
 * (the common `unless defined(...) { ... }` idempotence idiom) never reached
 * the catalog. The interpreter's PUPPET_STMT_UNLESS handler already does the
 * negated-if execution. */
static puppet_stmt_t *convert_unless(TSNode node, const char *source) {
    puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
    stmt->type = PUPPET_STMT_UNLESS;
    stmt->loc = node_location(node);

    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "condition") == 0) {
            /* Mirror convert_if: support `$hash['a']['b']` chained access. */
            TSNode var_child = find_child(child, "variable");
            uint32_t cond_count = ts_node_named_child_count(child);
            if (!ts_node_is_null(var_child)) {
                puppet_expr_t *cond_expr = convert_variable(var_child, source);
                for (uint32_t j = 0; j < cond_count; j++) {
                    TSNode cond_child = ts_node_named_child(child, j);
                    if (strcmp(ts_node_type(cond_child), "access") == 0) {
                        cond_expr = build_index_expr(cond_expr, cond_child, source);
                    }
                }
                stmt->data.unless_stmt.condition = cond_expr;
            } else if (cond_count > 0) {
                stmt->data.unless_stmt.condition =
                    convert_expression(ts_node_named_child(child, 0), source);
            }
        } else if (strcmp(type, "block") == 0 && stmt->data.unless_stmt.body.stmts == NULL) {
            stmt->data.unless_stmt.body = convert_block(child, source);
        } else if (strcmp(type, "else") == 0) {
            TSNode else_block = find_child(child, "block");
            if (!ts_node_is_null(else_block)) {
                stmt->data.unless_stmt.else_body = puppet_calloc(1, sizeof(puppet_stmt_list_t));
                *stmt->data.unless_stmt.else_body = convert_block(else_block, source);
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

    /* First pass: find the case expression (variable, possibly with chained access)
     * For $facts['os']['family'], we get: variable, access, access
     * We need to iterate and build nested index expressions */
    puppet_expr_t *case_expr = NULL;

    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "variable") == 0 && !case_expr) {
            case_expr = convert_expression(child, source);
        } else if (strcmp(type, "access") == 0 && case_expr) {
            /* Build chained index expression: expr[access] */
            case_expr = build_index_expr(case_expr, child, source);
        } else if (strcmp(type, "case_entry") == 0 || strcmp(type, "case_option") == 0) {
            /* Stop when we hit case entries - rest is the match/body */
            break;
        }
    }

    if (case_expr) {
        stmt->data.case_stmt.expr = case_expr;
    }

    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "condition") == 0 || strcmp(type, "binary") == 0) {
            /* Only handle these if we didn't already set expr */
            if (!stmt->data.case_stmt.expr) {
                stmt->data.case_stmt.expr = convert_expression(child, source);
            }
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

        /* Check for resource chains first - they have chaining_arrow children */
        TSNode arrow_node = find_child(node, "chaining_arrow");
        if (!ts_node_is_null(arrow_node)) {
            /* This is a chain statement - find all arrows and operands */
            size_t arrow_count = 0;
            uint32_t arrow_positions[16];
            char *arrow_types[16];

            for (uint32_t i = 0; i < count && arrow_count < 16; i++) {
                TSNode child = ts_node_named_child(node, i);
                if (node_is(child, "chaining_arrow")) {
                    arrow_positions[arrow_count] = i;
                    arrow_types[arrow_count] = node_text(child, source);
                    arrow_count++;
                }
            }

            if (arrow_count > 0) {
                /* Build chain statements for each arrow */
                puppet_stmt_t *chain_stmt = NULL;
                puppet_stmt_t *prev_stmt = NULL;

                uint32_t start = 0;
                for (size_t a = 0; a <= arrow_count; a++) {
                    uint32_t end = (a < arrow_count) ? arrow_positions[a] : count;

                    /* Build resource reference for children from start to end */
                    puppet_string_t res_type = {0};
                    puppet_expr_t *title_expr = NULL;
                    puppet_stmt_t *cur_stmt = NULL;

                    for (uint32_t i = start; i < end; i++) {
                        TSNode child = ts_node_named_child(node, i);
                        const char *child_type = ts_node_type(child);

                        if (strcmp(child_type, "type") == 0) {
                            char *type_str = node_text(child, source);
                            res_type = puppet_string_create(type_str);
                            puppet_free(type_str);
                        } else if (strcmp(child_type, "access") == 0) {
                            TSNode access_elem = find_child(child, "access_element");
                            if (!ts_node_is_null(access_elem)) {
                                TSNode value_node = ts_node_named_child(access_elem, 0);
                                title_expr = convert_expression(value_node, source);
                            }
                        } else if (strcmp(child_type, "resource_type") == 0) {
                            /* Inline resource declaration in chain */
                            cur_stmt = convert_statement(child, source);
                        }
                    }

                    /* Build resource reference from type + title */
                    if (!cur_stmt && res_type.data && title_expr) {
                        puppet_expr_t *ref_expr = puppet_calloc(1, sizeof(puppet_expr_t));
                        ref_expr->type = PUPPET_EXPR_RESOURCE_REF;
                        ref_expr->loc = node_location(node);
                        ref_expr->data.resource_ref.type = res_type;
                        ref_expr->data.resource_ref.title = title_expr;

                        cur_stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
                        cur_stmt->type = PUPPET_STMT_EXPRESSION;
                        cur_stmt->loc = node_location(node);
                        cur_stmt->data.expr = ref_expr;
                    } else if (res_type.data && !cur_stmt) {
                        puppet_free(res_type.data);
                    }

                    if (a == 0) {
                        prev_stmt = cur_stmt;
                    } else if (cur_stmt && prev_stmt) {
                        int is_notify = (strcmp(arrow_types[a-1], "~>") == 0 ||
                                       strcmp(arrow_types[a-1], "<~") == 0);
                        int is_reverse = (strcmp(arrow_types[a-1], "<-") == 0 ||
                                        strcmp(arrow_types[a-1], "<~") == 0);

                        puppet_stmt_t *new_chain = puppet_calloc(1, sizeof(puppet_stmt_t));
                        new_chain->type = PUPPET_STMT_RESOURCE_CHAIN;
                        new_chain->loc = node_location(node);
                        new_chain->data.chain.type = is_notify ? CHAIN_NOTIFY : CHAIN_BEFORE;

                        if (is_reverse) {
                            new_chain->data.chain.left = cur_stmt;
                            new_chain->data.chain.right = chain_stmt ? chain_stmt : prev_stmt;
                        } else {
                            new_chain->data.chain.left = chain_stmt ? chain_stmt : prev_stmt;
                            new_chain->data.chain.right = cur_stmt;
                        }
                        chain_stmt = new_chain;
                        prev_stmt = cur_stmt;
                    }

                    if (a < arrow_count) {
                        start = arrow_positions[a] + 1;
                    }
                }

                for (size_t a = 0; a < arrow_count; a++) {
                    puppet_free(arrow_types[a]);
                }

                return chain_stmt;
            }
        }

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
    if (strcmp(type, "define_definition") == 0)
        return convert_define_def(node, source);
    if (strcmp(type, "function_definition") == 0)
        return convert_function_def(node, source);
    if (strcmp(type, "resource_type") == 0)
        return convert_resource(node, source);
    if (strcmp(type, "if") == 0)
        return convert_if(node, source);
    if (strcmp(type, "unless") == 0)
        return convert_unless(node, source);
    if (strcmp(type, "type_alias") == 0)
        return convert_type_alias(node, source);
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

    /* Handle resource overrides and resource defaults:
     * Resource override: Type['title'] { attr => value } - has access element (title)
     * Resource default: Type { attr => value } - no access element
     */
    if (strcmp(type, "resource_reference") == 0) {
        TSNode attr_list = find_child(node, "attribute_list");
        if (!ts_node_is_null(attr_list)) {
            TSNode type_node = find_child(node, "type");
            TSNode access_node = find_child(node, "access");

            if (!ts_node_is_null(type_node) && !ts_node_is_null(access_node)) {
                /* Resource override: Type['title'] { attr => value } */
                puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
                stmt->type = PUPPET_STMT_RESOURCE_OVERRIDE;
                stmt->loc = node_location(node);

                /* Create resource reference expression */
                puppet_expr_t *ref = puppet_calloc(1, sizeof(puppet_expr_t));
                ref->type = PUPPET_EXPR_RESOURCE_REF;
                ref->loc = node_location(node);

                char *type_str = node_text(type_node, source);
                ref->data.resource_ref.type = puppet_string_create(type_str);
                puppet_free(type_str);

                /* Get title from access element */
                TSNode access_elem = find_child(access_node, "access_element");
                if (!ts_node_is_null(access_elem)) {
                    ref->data.resource_ref.title = convert_access_element_expr(access_elem, source);
                }

                stmt->data.resource_override.reference = ref;

                /* Convert attributes */
                uint32_t attr_count = ts_node_named_child_count(attr_list);
                stmt->data.resource_override.attributes = puppet_calloc(attr_count, sizeof(puppet_attribute_t));
                stmt->data.resource_override.attr_count = 0;

                for (uint32_t i = 0; i < attr_count; i++) {
                    TSNode attr_node = ts_node_named_child(attr_list, i);
                    if (node_is(attr_node, "attribute")) {
                        stmt->data.resource_override.attributes[stmt->data.resource_override.attr_count++] =
                            convert_attribute(attr_node, source);
                    }
                }

                return stmt;
            } else if (!ts_node_is_null(type_node)) {
                /* Resource default: Type { attr => value } */
                puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
                stmt->type = PUPPET_STMT_RESOURCE_DEFAULT;
                stmt->loc = node_location(node);

                char *type_str = node_text(type_node, source);
                stmt->data.resource_default.type = puppet_string_create(type_str);
                puppet_free(type_str);

                /* Convert attributes */
                uint32_t attr_count = ts_node_named_child_count(attr_list);
                stmt->data.resource_default.attributes = puppet_calloc(attr_count, sizeof(puppet_attribute_t));
                stmt->data.resource_default.attr_count = 0;

                for (uint32_t i = 0; i < attr_count; i++) {
                    TSNode attr_node = ts_node_named_child(attr_list, i);
                    if (node_is(attr_node, "attribute")) {
                        stmt->data.resource_default.attributes[stmt->data.resource_default.attr_count++] =
                            convert_attribute(attr_node, source);
                    }
                }

                return stmt;
            }
        } else {
            /* Resource reference without attribute_list - wrap as expression statement */
            puppet_expr_t *expr = convert_expression(node, source);
            if (expr) {
                puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
                stmt->type = PUPPET_STMT_EXPRESSION;
                stmt->loc = node_location(node);
                stmt->data.expr = expr;
                return stmt;
            }
        }
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

    /* Fallback: a bare expression used as a statement — e.g. the last line of a
     * function or lambda body (`$n * 2`, `"hello ${who}"`). Wrap it as an
     * expression statement so blocks can yield its value. */
    {
        puppet_expr_t *e = convert_expression(node, source);
        if (e) {
            puppet_stmt_t *stmt = puppet_calloc(1, sizeof(puppet_stmt_t));
            stmt->type = PUPPET_STMT_EXPRESSION;
            stmt->loc = node_location(node);
            stmt->data.expr = e;
            return stmt;
        }
    }

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

    /* Set current filename for source location tracking */
    const char *old_filename = current_parse_filename;
    current_parse_filename = filename;

    puppet_stmt_list_t *result = puppet_ts_parse_string(source, read);

    /* Restore previous filename (for nested includes) */
    current_parse_filename = old_filename;

    puppet_free(source);

    return result;
}

/**
 * Parse a Puppet expression string
 *
 * Wraps the expression in a temporary assignment to make it a valid statement,
 * then extracts and returns just the expression.
 *
 * @param source The Puppet expression (e.g., "$foo", "$hash['key']", "1 + 2")
 * @param length Length of the source
 * @return Parsed expression, or NULL on error
 */
puppet_expr_t *puppet_ts_parse_expression(const char *source, size_t length) {
    /* Wrap expression as: $_epp_tmp = (expr) */
    size_t wrapped_len = length + 20;
    char *wrapped = puppet_malloc(wrapped_len);
    snprintf(wrapped, wrapped_len, "$_epp_tmp = (%.*s)", (int)length, source);

    puppet_stmt_list_t *stmts = puppet_ts_parse_string(wrapped, strlen(wrapped));
    puppet_free(wrapped);

    if (!stmts || stmts->count == 0) {
        if (stmts) puppet_free(stmts);
        return NULL;
    }

    /* Extract the expression from the assignment */
    puppet_stmt_t *stmt = stmts->stmts[0];
    puppet_expr_t *result = NULL;

    if (stmt && stmt->type == PUPPET_STMT_ASSIGNMENT && stmt->data.assignment.value) {
        /* Clone the expression so we can free the statement */
        result = stmt->data.assignment.value;
        stmt->data.assignment.value = NULL;  /* Prevent double-free */
    }

    /* Clean up (but we've stolen the expression) */
    for (size_t i = 0; i < stmts->count; i++) {
        puppet_stmt_destroy(stmts->stmts[i]);
    }
    puppet_free(stmts->stmts);
    puppet_free(stmts);

    return result;
}
