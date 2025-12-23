/**
 * @file puppet_json_parser.h
 * @brief JSON parser interface for Puppet facts loading
 *
 * This header extends the common JSON library with Puppet-specific
 * functionality for loading facts from JSON files.
 *
 * Core JSON types and functions are provided by puppet_json_common.h.
 * This header adds:
 * - Conversion from json_value_t to puppet_value_t
 * - Facts loading from JSON files into puppet_facts_db
 */

#ifndef PUPPET_JSON_PARSER_H
#define PUPPET_JSON_PARSER_H

#include "puppet_json_common.h"
#include "puppet_ast.h"

/*
 * ===========================================================================
 * PUPPET-SPECIFIC JSON UTILITIES
 * ===========================================================================
 */

/**
 * @brief Convert a JSON value to a Puppet value
 * @param json_val The JSON value to convert
 * @return Newly allocated puppet_value_t (caller must free)
 */
puppet_value_t *json_value_to_puppet_value(json_value_t *json_val);

#endif /* PUPPET_JSON_PARSER_H */
