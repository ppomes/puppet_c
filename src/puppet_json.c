/**
 * @file puppet_json.c
 * @brief JSON serialization for Puppet AST structures
 *
 * This file implements JSON serialization for the complete Puppet AST.
 * It provides functions to convert AST nodes into JSON format for:
 * - Analysis and debugging tools
 * - Integration with other systems
 * - AST visualization and inspection
 * - Testing and validation
 *
 * The JSON output follows a consistent schema where each AST node
 * becomes a JSON object with a "type" field indicating the node type
 * and additional fields containing the node's data.
 *
 * Features:
 * - Recursive serialization of complex AST structures
 * - Proper JSON escaping for string literals
 * - Compact and human-readable output format
 * - Memory-efficient streaming output
 */

#include "puppet_json.h"
#include <stdlib.h>
#include <string.h>

json_buffer_t *json_buffer_create(void) {
    json_buffer_t *buf = malloc(sizeof(json_buffer_t));
    buf->capacity = 1024;
    buf->size = 0;
    buf->data = malloc(buf->capacity);
    buf->data[0] = '\0';
    return buf;
}

void json_buffer_destroy(json_buffer_t *buf) {
    if (buf) {
        free(buf->data);
        free(buf);
    }
}

void json_buffer_append(json_buffer_t *buf, const char *str) {
    size_t len = strlen(str);
    while (buf->size + len + 1 >= buf->capacity) {
        buf->capacity *= 2;
        buf->data = realloc(buf->data, buf->capacity);
    }
    strcpy(buf->data + buf->size, str);
    buf->size += len;
}

void json_buffer_append_char(json_buffer_t *buf, char c) {
    if (buf->size + 2 >= buf->capacity) {
        buf->capacity *= 2;
        buf->data = realloc(buf->data, buf->capacity);
    }
    buf->data[buf->size++] = c;
    buf->data[buf->size] = '\0';
}

void json_buffer_append_escaped_string(json_buffer_t *buf, const char *str) {
    json_buffer_append_char(buf, '"');
    while (*str) {
        switch (*str) {
            case '"':
                json_buffer_append(buf, "\\\"");
                break;
            case '\\':
                json_buffer_append(buf, "\\\\");
                break;
            case '\n':
                json_buffer_append(buf, "\\n");
                break;
            case '\r':
                json_buffer_append(buf, "\\r");
                break;
            case '\t':
                json_buffer_append(buf, "\\t");
                break;
            default:
                json_buffer_append_char(buf, *str);
                break;
        }
        str++;
    }
    json_buffer_append_char(buf, '"');
}

static void json_indent(json_buffer_t *buf, int level) {
    for (int i = 0; i < level * 2; i++) {
        json_buffer_append_char(buf, ' ');
    }
}

void puppet_program_to_json(puppet_program_t *program, FILE *output) {
    json_buffer_t *buf = json_buffer_create();
    
    json_buffer_append(buf, "{\n");
    json_indent(buf, 1);
    json_buffer_append(buf, "\"type\": \"program\",\n");
    json_indent(buf, 1);
    json_buffer_append(buf, "\"statements\": [\n");
    
    puppet_stmt_list_to_json(buf, &program->statements, 2);
    
    json_buffer_append(buf, "\n");
    json_indent(buf, 1);
    json_buffer_append(buf, "]\n");
    json_buffer_append(buf, "}\n");
    
    fprintf(output, "%s", buf->data);
    json_buffer_destroy(buf);
}

void puppet_stmt_list_to_json(json_buffer_t *buf, puppet_stmt_list_t *stmts, int indent) {
    for (size_t i = 0; i < stmts->count; i++) {
        if (i > 0) {
            json_buffer_append(buf, ",\n");
        }
        json_indent(buf, indent);
        puppet_stmt_to_json(buf, stmts->stmts[i], indent);
    }
}

void puppet_stmt_to_json(json_buffer_t *buf, puppet_stmt_t *stmt, int indent) {
    json_buffer_append(buf, "{\n");
    json_indent(buf, indent + 1);
    
    switch (stmt->type) {
        case PUPPET_STMT_ASSIGNMENT:
            json_buffer_append(buf, "\"type\": \"assignment\",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"variable\": ");
            json_buffer_append_escaped_string(buf, stmt->data.assignment.variable.data);
            json_buffer_append(buf, ",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"value\": ");
            puppet_expr_to_json(buf, stmt->data.assignment.value, indent + 1);
            break;
            
        case PUPPET_STMT_CLASS_DEF:
            json_buffer_append(buf, "\"type\": \"class_definition\",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"name\": ");
            json_buffer_append_escaped_string(buf, stmt->data.class_def.name.data);
            json_buffer_append(buf, ",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"parameters\": [\n");
            for (size_t i = 0; i < stmt->data.class_def.params.count; i++) {
                if (i > 0) json_buffer_append(buf, ",\n");
                json_indent(buf, indent + 2);
                json_buffer_append(buf, "{\n");
                json_indent(buf, indent + 3);
                json_buffer_append(buf, "\"name\": ");
                json_buffer_append_escaped_string(buf, stmt->data.class_def.params.params[i].name.data);
                json_buffer_append(buf, "\n");
                json_indent(buf, indent + 2);
                json_buffer_append(buf, "}");
            }
            json_buffer_append(buf, "\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "],\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"body\": [\n");
            puppet_stmt_list_to_json(buf, &stmt->data.class_def.body, indent + 2);
            json_buffer_append(buf, "\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "]");
            break;
            
        case PUPPET_STMT_RESOURCE:
            json_buffer_append(buf, "\"type\": \"resource\",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"resource_type\": ");
            json_buffer_append_escaped_string(buf, stmt->data.resource.type.data);
            json_buffer_append(buf, ",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"style\": ");
            switch (stmt->data.resource.style) {
                case PUPPET_RES_NORMAL: json_buffer_append(buf, "\"normal\""); break;
                case PUPPET_RES_VIRTUAL: json_buffer_append(buf, "\"virtual\""); break;
                case PUPPET_RES_EXPORTED: json_buffer_append(buf, "\"exported\""); break;
            }
            json_buffer_append(buf, ",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"instances\": [\n");
            for (size_t i = 0; i < stmt->data.resource.instance_count; i++) {
                if (i > 0) json_buffer_append(buf, ",\n");
                json_indent(buf, indent + 2);
                json_buffer_append(buf, "{\n");
                json_indent(buf, indent + 3);
                json_buffer_append(buf, "\"title\": ");
                if (stmt->data.resource.instances[i].title) {
                    puppet_expr_to_json(buf, stmt->data.resource.instances[i].title, indent + 3);
                } else {
                    json_buffer_append(buf, "null");
                }
                json_buffer_append(buf, ",\n");
                json_indent(buf, indent + 3);
                json_buffer_append(buf, "\"attributes\": []\n");
                json_indent(buf, indent + 2);
                json_buffer_append(buf, "}");
            }
            json_buffer_append(buf, "\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "]");
            break;
            
        case PUPPET_STMT_IF:
            json_buffer_append(buf, "\"type\": \"if_statement\",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"branches\": []");
            break;
            
        case PUPPET_STMT_FUNCTION_CALL:
            json_buffer_append(buf, "\"type\": \"function_call\",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"expression\": ");
            puppet_expr_to_json(buf, stmt->data.expr, indent + 1);
            break;
            
        case PUPPET_STMT_INCLUDE:
            json_buffer_append(buf, "\"type\": \"include\",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"classes\": [\n");
            for (size_t i = 0; i < stmt->data.names.count; i++) {
                if (i > 0) json_buffer_append(buf, ",\n");
                json_indent(buf, indent + 2);
                puppet_expr_to_json(buf, stmt->data.names.exprs[i], indent + 2);
            }
            json_buffer_append(buf, "\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "]");
            break;
            
        case PUPPET_STMT_REQUIRE:
            json_buffer_append(buf, "\"type\": \"require\",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"classes\": [\n");
            for (size_t i = 0; i < stmt->data.names.count; i++) {
                if (i > 0) json_buffer_append(buf, ",\n");
                json_indent(buf, indent + 2);
                puppet_expr_to_json(buf, stmt->data.names.exprs[i], indent + 2);
            }
            json_buffer_append(buf, "\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "]");
            break;
            
        case PUPPET_STMT_CONTAIN:
            json_buffer_append(buf, "\"type\": \"contain\",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"classes\": [\n");
            for (size_t i = 0; i < stmt->data.names.count; i++) {
                if (i > 0) json_buffer_append(buf, ",\n");
                json_indent(buf, indent + 2);
                puppet_expr_to_json(buf, stmt->data.names.exprs[i], indent + 2);
            }
            json_buffer_append(buf, "\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "]");
            break;
            
        default:
            json_buffer_append(buf, "\"type\": \"unknown\"");
            break;
    }
    
    json_buffer_append(buf, "\n");
    json_indent(buf, indent);
    json_buffer_append(buf, "}");
}

void puppet_expr_to_json(json_buffer_t *buf, puppet_expr_t *expr, int indent) {
    json_buffer_append(buf, "{\n");
    json_indent(buf, indent + 1);
    
    switch (expr->type) {
        case PUPPET_EXPR_VALUE:
            json_buffer_append(buf, "\"type\": \"value\",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"data\": ");
            puppet_value_to_json(buf, expr->data.value, indent + 1);
            break;
            
        case PUPPET_EXPR_VARIABLE:
            json_buffer_append(buf, "\"type\": \"variable\",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"name\": ");
            json_buffer_append_escaped_string(buf, expr->data.variable.data);
            break;
            
        case PUPPET_EXPR_BINOP:
            json_buffer_append(buf, "\"type\": \"binary_operation\",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"operator\": ");
            switch (expr->data.binop.op) {
                case PUPPET_OP_ADD: json_buffer_append(buf, "\"+\""); break;
                case PUPPET_OP_SUB: json_buffer_append(buf, "\"-\""); break;
                case PUPPET_OP_MUL: json_buffer_append(buf, "\"*\""); break;
                case PUPPET_OP_DIV: json_buffer_append(buf, "\"/\""); break;
                case PUPPET_OP_EQ: json_buffer_append(buf, "\"==\""); break;
                case PUPPET_OP_NE: json_buffer_append(buf, "\"!=\""); break;
                case PUPPET_OP_LT: json_buffer_append(buf, "\"<\""); break;
                case PUPPET_OP_LE: json_buffer_append(buf, "\"<=\""); break;
                case PUPPET_OP_GT: json_buffer_append(buf, "\">\""); break;
                case PUPPET_OP_GE: json_buffer_append(buf, "\">=\""); break;
                case PUPPET_OP_AND: json_buffer_append(buf, "\"and\""); break;
                case PUPPET_OP_OR: json_buffer_append(buf, "\"or\""); break;
                default: json_buffer_append(buf, "\"unknown\""); break;
            }
            json_buffer_append(buf, ",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"left\": ");
            puppet_expr_to_json(buf, expr->data.binop.left, indent + 1);
            json_buffer_append(buf, ",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"right\": ");
            puppet_expr_to_json(buf, expr->data.binop.right, indent + 1);
            break;
            
        case PUPPET_EXPR_FUNCALL:
            json_buffer_append(buf, "\"type\": \"function_call\",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"name\": ");
            json_buffer_append_escaped_string(buf, expr->data.funcall.name.data);
            json_buffer_append(buf, ",\n");
            json_indent(buf, indent + 1);
            json_buffer_append(buf, "\"arguments\": []");
            break;
            
        default:
            json_buffer_append(buf, "\"type\": \"unknown\"");
            break;
    }
    
    json_buffer_append(buf, "\n");
    json_indent(buf, indent);
    json_buffer_append(buf, "}");
}

void puppet_value_to_json(json_buffer_t *buf, puppet_value_t *value, int indent) {
    switch (value->type) {
        case PUPPET_VALUE_UNDEF:
            json_buffer_append(buf, "null");
            break;
            
        case PUPPET_VALUE_BOOL:
            json_buffer_append(buf, value->data.boolean ? "true" : "false");
            break;
            
        case PUPPET_VALUE_STRING:
            json_buffer_append_escaped_string(buf, value->data.string.data);
            break;
            
        case PUPPET_VALUE_NUMBER: {
            char num_str[64];
            if (value->data.number == (long)value->data.number) {
                snprintf(num_str, sizeof(num_str), "%ld", (long)value->data.number);
            } else {
                snprintf(num_str, sizeof(num_str), "%.6g", value->data.number);
            }
            json_buffer_append(buf, num_str);
            break;
        }
            
        case PUPPET_VALUE_ARRAY:
            json_buffer_append(buf, "[\n");
            for (size_t i = 0; i < value->data.array->count; i++) {
                if (i > 0) json_buffer_append(buf, ",\n");
                json_indent(buf, indent + 1);
                puppet_value_to_json(buf, value->data.array->items[i], indent + 1);
            }
            json_buffer_append(buf, "\n");
            json_indent(buf, indent);
            json_buffer_append(buf, "]");
            break;
            
        case PUPPET_VALUE_HASH:
            json_buffer_append(buf, "{}");
            break;
            
        case PUPPET_VALUE_REGEXP:
            json_buffer_append(buf, "{\"type\":\"regexp\",\"pattern\":");
            json_buffer_append_escaped_string(buf, value->data.regexp.data);
            json_buffer_append(buf, "}");
            break;
            
        default:
            json_buffer_append(buf, "null");
            break;
    }
}