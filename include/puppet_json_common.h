/**
 * @file puppet_json_common.h
 * @brief Common JSON utilities for all puppet_c components
 *
 * This header provides a lightweight JSON library independent of puppet_ast.h,
 * suitable for use by all binaries (puppetc, server, agent, facter).
 *
 * Features:
 * - JSON buffer for building JSON output
 * - JSON tokenizer and parser
 * - JSON value types and accessors
 */

#ifndef PUPPET_JSON_COMMON_H
#define PUPPET_JSON_COMMON_H

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

/*
 * ===========================================================================
 * JSON BUFFER - For building JSON output
 * ===========================================================================
 */

/**
 * @brief Dynamic buffer for JSON output construction
 */
typedef struct {
    char *data;         /**< Buffer content */
    size_t size;        /**< Current content size */
    size_t capacity;    /**< Buffer capacity */
} json_buffer_t;

/* Buffer management */
json_buffer_t *json_buffer_create(void);
void json_buffer_destroy(json_buffer_t *buf);
void json_buffer_reset(json_buffer_t *buf);
void json_buffer_append(json_buffer_t *buf, const char *str);
void json_buffer_append_char(json_buffer_t *buf, char c);
void json_buffer_appendf(json_buffer_t *buf, const char *fmt, ...);

/* JSON-specific operations */
void json_buffer_append_escaped(json_buffer_t *buf, const char *str);
void json_buffer_append_string(json_buffer_t *buf, const char *str);
void json_buffer_append_int(json_buffer_t *buf, long value);
void json_buffer_append_double(json_buffer_t *buf, double value);
void json_buffer_append_bool(json_buffer_t *buf, bool value);
void json_buffer_append_null(json_buffer_t *buf);

/* Indentation helpers */
void json_buffer_indent(json_buffer_t *buf, int level);

/*
 * ===========================================================================
 * JSON TOKENIZER
 * ===========================================================================
 */

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
    JSON_TOKEN_EOF,             /**< End of input */
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
    size_t pos;                 /**< Current position */
    size_t length;              /**< Total input length */
    int line;                   /**< Current line number */
    int column;                 /**< Current column number */
    json_token_t current_token; /**< Current token */
    char *error_message;        /**< Error message if parsing fails */
    int depth;                  /**< Current container nesting depth (recursion guard) */
} json_parser_t;

/*
 * ===========================================================================
 * JSON VALUE TYPES
 * ===========================================================================
 */

/**
 * @brief JSON value types
 */
typedef enum {
    JSON_VALUE_NULL,
    JSON_VALUE_BOOLEAN,
    JSON_VALUE_NUMBER,
    JSON_VALUE_STRING,
    JSON_VALUE_ARRAY,
    JSON_VALUE_OBJECT
} json_value_type_t;

/**
 * @brief JSON value structure
 */
typedef struct json_value {
    json_value_type_t type;
    union {
        bool boolean_value;
        double number_value;
        char *string_value;
        struct {
            struct json_value **elements;
            size_t count;
            size_t capacity;
        } array;
        struct {
            char **keys;
            struct json_value **values;
            size_t count;
            size_t capacity;
        } object;
    } data;
} json_value_t;

/*
 * ===========================================================================
 * JSON PARSER API
 * ===========================================================================
 */

/* Parser lifecycle */
json_parser_t *json_parser_create(const char *input);
void json_parser_destroy(json_parser_t *parser);
const char *json_parser_get_error(json_parser_t *parser);

/* Parsing */
json_value_t *json_parse(const char *input);
json_value_t *json_parse_value(json_parser_t *parser);
json_value_t *json_parse_file(const char *filepath);

/*
 * ===========================================================================
 * JSON VALUE API
 * ===========================================================================
 */

/* Value creation */
json_value_t *json_value_create_null(void);
json_value_t *json_value_create_bool(bool value);
json_value_t *json_value_create_number(double value);
json_value_t *json_value_create_string(const char *value);
json_value_t *json_value_create_array(void);
json_value_t *json_value_create_object(void);

/* Value destruction */
void json_value_destroy(json_value_t *value);

/* Array operations */
void json_array_append(json_value_t *arr, json_value_t *value);
json_value_t *json_array_get(json_value_t *arr, size_t index);
size_t json_array_size(json_value_t *arr);

/* Object operations */
void json_object_set(json_value_t *obj, const char *key, json_value_t *value);
json_value_t *json_object_get(json_value_t *obj, const char *key);
bool json_object_has(json_value_t *obj, const char *key);
size_t json_object_size(json_value_t *obj);

/* Value accessors */
bool json_is_null(json_value_t *value);
bool json_is_bool(json_value_t *value);
bool json_is_number(json_value_t *value);
bool json_is_string(json_value_t *value);
bool json_is_array(json_value_t *value);
bool json_is_object(json_value_t *value);

bool json_get_bool(json_value_t *value);
double json_get_number(json_value_t *value);
const char *json_get_string(json_value_t *value);

/* Serialization */
char *json_value_to_string(json_value_t *value);
void json_value_to_buffer(json_value_t *value, json_buffer_t *buf, int indent);

/*
 * ===========================================================================
 * CONVENIENCE HELPERS
 * ===========================================================================
 */

/**
 * @brief Extract a string value from a JSON object by key
 * @return Newly allocated string or NULL (caller must free)
 */
char *json_object_get_string(json_value_t *obj, const char *key);

/**
 * @brief Extract a number from a JSON object by key
 */
double json_object_get_number(json_value_t *obj, const char *key);

/**
 * @brief Extract a boolean from a JSON object by key
 */
bool json_object_get_bool(json_value_t *obj, const char *key);

#endif /* PUPPET_JSON_COMMON_H */
