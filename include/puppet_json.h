/**
 * @file puppet_json.h
 * @brief JSON serialization interface for Puppet AST
 *
 * This header provides functions to serialize Puppet AST structures
 * into JSON format. The JSON output can be used for analysis tools,
 * debugging, integration with other systems, or AST visualization.
 *
 * The serialization maintains the complete AST structure and can be
 * used to reconstruct the original parse tree. All node types are
 * supported with consistent JSON schema.
 */

#ifndef PUPPET_JSON_H
#define PUPPET_JSON_H

#include "puppet_ast.h"
#include <stdio.h>

/**
 * @brief Dynamic buffer for JSON output construction
 * 
 * Provides efficient string building for JSON serialization with
 * automatic memory management and capacity expansion.
 */
typedef struct {
    char *data;         /**< Buffer content */
    size_t size;        /**< Current content size */
    size_t capacity;    /**< Buffer capacity */
} json_buffer_t;

/* Buffer management */
json_buffer_t *json_buffer_create(void);
void json_buffer_destroy(json_buffer_t *buf);
void json_buffer_append(json_buffer_t *buf, const char *str);
void json_buffer_append_char(json_buffer_t *buf, char c);
void json_buffer_append_escaped_string(json_buffer_t *buf, const char *str);

/* AST to JSON serialization */
void puppet_program_to_json(puppet_program_t *program, FILE *output);
void puppet_stmt_list_to_json(json_buffer_t *buf, puppet_stmt_list_t *stmts, int indent);
void puppet_stmt_to_json(json_buffer_t *buf, puppet_stmt_t *stmt, int indent);
void puppet_expr_to_json(json_buffer_t *buf, puppet_expr_t *expr, int indent);
void puppet_value_to_json(json_buffer_t *buf, puppet_value_t *value, int indent);

#endif