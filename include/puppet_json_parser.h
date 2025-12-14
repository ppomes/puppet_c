/**
 * @file puppet_json_parser.h
 * @brief Simple JSON parser for facts loading
 *
 * This header provides a lightweight JSON parser specifically designed
 * for parsing PuppetDB fact exports. It supports the subset of JSON
 * needed for facts files.
 */

#ifndef PUPPET_JSON_PARSER_H
#define PUPPET_JSON_PARSER_H

#include "puppet_ast.h"

/**
 * @brief JSON token types
 */
typedef enum {
    JSON_TOKEN_OBJECT_START,    /**< { */
    JSON_TOKEN_OBJECT_END,      /**< } */
    JSON_TOKEN_ARRAY_START,     /**< [ */
    JSON_TOKEN_ARRAY_END,       /**< ] */
    JSON_TOKEN_STRING,          /**< "string" */
    JSON_TOKEN_NUMBER,          /**< 123 or 123.45 */
    JSON_TOKEN_BOOLEAN,         /**< true or false */
    JSON_TOKEN_NULL,            /**< null */
    JSON_TOKEN_COLON,           /**< : */
    JSON_TOKEN_COMMA,           /**< , */
    JSON_TOKEN_EOF,             /**< End of file */
    JSON_TOKEN_ERROR            /**< Parse error */
} json_token_type_t;

/**
 * @brief JSON token
 */
typedef struct {
    json_token_type_t type;
    char *value;                /**< String value (for STRING, NUMBER tokens) */
    size_t length;              /**< Length of value */
    bool boolean_value;         /**< For BOOLEAN tokens */
    double number_value;        /**< For NUMBER tokens */
} json_token_t;

/**
 * @brief JSON parser state
 */
typedef struct {
    const char *input;          /**< Input JSON string */
    size_t pos;                 /**< Current position in input */
    size_t length;              /**< Total input length */
    int line;                   /**< Current line number */
    int column;                 /**< Current column number */
    json_token_t current_token; /**< Current token */
    char *error_message;        /**< Error message if parsing fails */
} json_parser_t;

/**
 * @brief JSON value types
 */
typedef enum {
    JSON_VALUE_STRING,
    JSON_VALUE_NUMBER,
    JSON_VALUE_BOOLEAN,
    JSON_VALUE_NULL,
    JSON_VALUE_OBJECT,
    JSON_VALUE_ARRAY
} json_value_type_t;

/**
 * @brief JSON value structure
 */
typedef struct json_value {
    json_value_type_t type;
    union {
        char *string_value;
        double number_value;
        bool boolean_value;
        struct {
            char **keys;
            struct json_value **values;
            size_t count;
        } object;
        struct {
            struct json_value **elements;
            size_t count;
        } array;
    } data;
} json_value_t;

/* JSON Parser Functions */
json_parser_t *json_parser_create(const char *input);
void json_parser_destroy(json_parser_t *parser);
json_value_t *json_parse_value(json_parser_t *parser);
json_value_t *json_parse_file(const char *filepath);

/* JSON Value Functions */
void json_value_destroy(json_value_t *value);
json_value_t *json_object_get(json_value_t *obj, const char *key);
json_value_t *json_array_get(json_value_t *arr, size_t index);
const char *json_value_to_string(json_value_t *value);
double json_value_to_number(json_value_t *value);
bool json_value_to_boolean(json_value_t *value);

/* Forward declarations */
struct puppet_facts_db;

/* Utility Functions */
puppet_value_t *json_value_to_puppet_value(json_value_t *json_val);
int json_load_facts_from_file(const char *filepath, struct puppet_facts_db *facts_db);

#endif /* PUPPET_JSON_PARSER_H */