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
#include "puppet_json_common.h"
#include <stdio.h>

/*
 * Compatibility: json_buffer_append_escaped_string writes "escaped" (with quotes)
 * The common library has json_buffer_append_string which does the same.
 */
#define json_buffer_append_escaped_string(buf, str) json_buffer_append_string(buf, str)

/* AST to JSON serialization */
void puppet_program_to_json(puppet_program_t *program, FILE *output);
void puppet_stmt_list_to_json(json_buffer_t *buf, puppet_stmt_list_t *stmts, int indent);
void puppet_stmt_to_json(json_buffer_t *buf, puppet_stmt_t *stmt, int indent);
void puppet_expr_to_json(json_buffer_t *buf, puppet_expr_t *expr, int indent);
void puppet_value_to_json(json_buffer_t *buf, puppet_value_t *value, int indent);

#endif