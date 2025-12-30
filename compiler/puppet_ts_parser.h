/**
 * @file puppet_ts_parser.h
 * @brief Tree-sitter based parser for Puppet language
 *
 * This module provides a tree-sitter based parser as an alternative
 * to the flex/bison parser. It converts tree-sitter AST nodes to
 * our internal puppet_ast_t structures.
 */

#ifndef PUPPET_TS_PARSER_H
#define PUPPET_TS_PARSER_H

#include "puppet_ast.h"

/**
 * Parse a Puppet file using tree-sitter
 *
 * @param filename Path to the Puppet file
 * @return Parsed statement list, or NULL on error
 */
puppet_stmt_list_t *puppet_ts_parse_file(const char *filename);

/**
 * Parse Puppet source code using tree-sitter
 *
 * @param source The Puppet source code
 * @param length Length of the source code
 * @return Parsed statement list, or NULL on error
 */
puppet_stmt_list_t *puppet_ts_parse_string(const char *source, size_t length);

#endif /* PUPPET_TS_PARSER_H */
