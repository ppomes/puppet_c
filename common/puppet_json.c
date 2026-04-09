/**
 * @file puppet_json.c
 * @brief Common JSON utilities implementation
 *
 * Provides JSON buffer, parser, and value manipulation functions
 * for use by all puppet_c components.
 */

#include "puppet_json_common.h"
#include "puppet_memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>

/*
 * ===========================================================================
 * JSON BUFFER IMPLEMENTATION
 * ===========================================================================
 */

#define JSON_BUFFER_INITIAL_CAPACITY 1024

json_buffer_t *json_buffer_create(void) {
    json_buffer_t *buf = puppet_malloc(sizeof(json_buffer_t));
    if (!buf) return NULL;

    buf->capacity = JSON_BUFFER_INITIAL_CAPACITY;
    buf->size = 0;
    buf->data = puppet_malloc(buf->capacity);
    if (!buf->data) {
        puppet_free(buf);
        return NULL;
    }
    buf->data[0] = '\0';
    return buf;
}

void json_buffer_destroy(json_buffer_t *buf) {
    if (buf) {
        puppet_free(buf->data);
        puppet_free(buf);
    }
}

void json_buffer_reset(json_buffer_t *buf) {
    if (buf) {
        buf->size = 0;
        buf->data[0] = '\0';
    }
}

static void json_buffer_ensure_capacity(json_buffer_t *buf, size_t additional) {
    size_t needed = buf->size + additional + 1;
    if (needed > buf->capacity) {
        while (buf->capacity < needed) {
            buf->capacity *= 2;
        }
        buf->data = puppet_realloc(buf->data, buf->capacity);
    }
}

void json_buffer_append(json_buffer_t *buf, const char *str) {
    if (!buf || !str) return;
    size_t len = strlen(str);
    json_buffer_ensure_capacity(buf, len);
    memcpy(buf->data + buf->size, str, len + 1);
    buf->size += len;
}

void json_buffer_append_char(json_buffer_t *buf, char c) {
    if (!buf) return;
    json_buffer_ensure_capacity(buf, 1);
    buf->data[buf->size++] = c;
    buf->data[buf->size] = '\0';
}

void json_buffer_appendf(json_buffer_t *buf, const char *fmt, ...) {
    if (!buf || !fmt) return;

    va_list args;
    va_start(args, fmt);

    /* First pass: determine required size */
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    if (needed < 0) {
        va_end(args);
        return;
    }

    json_buffer_ensure_capacity(buf, needed);
    vsnprintf(buf->data + buf->size, needed + 1, fmt, args);
    buf->size += needed;

    va_end(args);
}

void json_buffer_append_escaped(json_buffer_t *buf, const char *str) {
    if (!buf || !str) return;

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
            case '\b':
                json_buffer_append(buf, "\\b");
                break;
            case '\f':
                json_buffer_append(buf, "\\f");
                break;
            default:
                if ((unsigned char)*str < 0x20) {
                    /* Control character - escape as \uXXXX */
                    json_buffer_appendf(buf, "\\u%04x", (unsigned char)*str);
                } else {
                    json_buffer_append_char(buf, *str);
                }
                break;
        }
        str++;
    }
}

void json_buffer_append_string(json_buffer_t *buf, const char *str) {
    json_buffer_append_char(buf, '"');
    json_buffer_append_escaped(buf, str ? str : "");
    json_buffer_append_char(buf, '"');
}

void json_buffer_append_int(json_buffer_t *buf, long value) {
    json_buffer_appendf(buf, "%ld", value);
}

void json_buffer_append_double(json_buffer_t *buf, double value) {
    if (value == (long)value) {
        json_buffer_appendf(buf, "%ld", (long)value);
    } else {
        json_buffer_appendf(buf, "%.15g", value);
    }
}

void json_buffer_append_bool(json_buffer_t *buf, bool value) {
    json_buffer_append(buf, value ? "true" : "false");
}

void json_buffer_append_null(json_buffer_t *buf) {
    json_buffer_append(buf, "null");
}

void json_buffer_indent(json_buffer_t *buf, int level) {
    for (int i = 0; i < level * 2; i++) {
        json_buffer_append_char(buf, ' ');
    }
}

/*
 * ===========================================================================
 * JSON PARSER IMPLEMENTATION
 * ===========================================================================
 */

json_parser_t *json_parser_create(const char *input) {
    if (!input) return NULL;

    json_parser_t *parser = puppet_calloc(1, sizeof(json_parser_t));
    if (!parser) return NULL;

    parser->input = input;
    parser->length = strlen(input);
    parser->pos = 0;
    parser->line = 1;
    parser->column = 1;
    parser->error_message = NULL;
    memset(&parser->current_token, 0, sizeof(json_token_t));

    return parser;
}

void json_parser_destroy(json_parser_t *parser) {
    if (!parser) return;

    if (parser->current_token.value) {
        puppet_free(parser->current_token.value);
    }
    if (parser->error_message) {
        puppet_free(parser->error_message);
    }
    puppet_free(parser);
}

const char *json_parser_get_error(json_parser_t *parser) {
    return parser ? parser->error_message : NULL;
}

static void json_parser_set_error(json_parser_t *parser, const char *message) {
    if (parser->error_message) {
        puppet_free(parser->error_message);
    }
    parser->error_message = puppet_strdup(message);
}

static void json_parser_skip_whitespace(json_parser_t *parser) {
    while (parser->pos < parser->length) {
        char c = parser->input[parser->pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            parser->pos++;
            parser->column++;
        } else if (c == '\n') {
            parser->pos++;
            parser->line++;
            parser->column = 1;
        } else {
            break;
        }
    }
}

static bool json_parser_read_string(json_parser_t *parser) {
    if (parser->pos >= parser->length || parser->input[parser->pos] != '"') {
        json_parser_set_error(parser, "Expected '\"' for string");
        return false;
    }

    parser->pos++; /* Skip opening quote */
    parser->column++;

    /* Build unescaped string */
    json_buffer_t *buf = json_buffer_create();
    if (!buf) {
        json_parser_set_error(parser, "Out of memory");
        return false;
    }

    while (parser->pos < parser->length) {
        char c = parser->input[parser->pos];

        if (c == '"') {
            /* End of string */
            parser->pos++;
            parser->column++;
            parser->current_token.value = puppet_strdup(buf->data);
            parser->current_token.length = buf->size;
            parser->current_token.type = JSON_TOKEN_STRING;
            json_buffer_destroy(buf);
            return true;
        } else if (c == '\\') {
            /* Escape sequence */
            parser->pos++;
            parser->column++;
            if (parser->pos >= parser->length) {
                json_parser_set_error(parser, "Unterminated escape sequence");
                json_buffer_destroy(buf);
                return false;
            }

            char escaped = parser->input[parser->pos];
            switch (escaped) {
                case '"': json_buffer_append_char(buf, '"'); break;
                case '\\': json_buffer_append_char(buf, '\\'); break;
                case '/': json_buffer_append_char(buf, '/'); break;
                case 'b': json_buffer_append_char(buf, '\b'); break;
                case 'f': json_buffer_append_char(buf, '\f'); break;
                case 'n': json_buffer_append_char(buf, '\n'); break;
                case 'r': json_buffer_append_char(buf, '\r'); break;
                case 't': json_buffer_append_char(buf, '\t'); break;
                case 'u':
                    /* Unicode escape - simplified handling */
                    if (parser->pos + 4 < parser->length) {
                        parser->pos += 4;
                        parser->column += 4;
                        json_buffer_append_char(buf, '?'); /* Placeholder */
                    }
                    break;
                default:
                    json_buffer_append_char(buf, escaped);
                    break;
            }
            parser->pos++;
            parser->column++;
        } else {
            json_buffer_append_char(buf, c);
            parser->pos++;
            parser->column++;
        }
    }

    json_parser_set_error(parser, "Unterminated string");
    json_buffer_destroy(buf);
    return false;
}

static bool json_parser_read_number(json_parser_t *parser) {
    size_t start = parser->pos;

    /* Handle negative numbers */
    if (parser->input[parser->pos] == '-') {
        parser->pos++;
    }

    /* Read integer part */
    while (parser->pos < parser->length && isdigit(parser->input[parser->pos])) {
        parser->pos++;
    }

    /* Handle decimal part */
    if (parser->pos < parser->length && parser->input[parser->pos] == '.') {
        parser->pos++;
        while (parser->pos < parser->length && isdigit(parser->input[parser->pos])) {
            parser->pos++;
        }
    }

    /* Handle exponent */
    if (parser->pos < parser->length &&
        (parser->input[parser->pos] == 'e' || parser->input[parser->pos] == 'E')) {
        parser->pos++;
        if (parser->pos < parser->length &&
            (parser->input[parser->pos] == '+' || parser->input[parser->pos] == '-')) {
            parser->pos++;
        }
        while (parser->pos < parser->length && isdigit(parser->input[parser->pos])) {
            parser->pos++;
        }
    }

    size_t len = parser->pos - start;
    parser->current_token.value = puppet_malloc(len + 1);
    memcpy(parser->current_token.value, parser->input + start, len);
    parser->current_token.value[len] = '\0';
    parser->current_token.length = len;
    parser->current_token.type = JSON_TOKEN_NUMBER;
    parser->current_token.number_value = strtod(parser->current_token.value, NULL);
    parser->column += len;

    return true;
}

static bool json_parser_read_keyword(json_parser_t *parser, const char *keyword) {
    size_t keyword_len = strlen(keyword);
    if (parser->pos + keyword_len <= parser->length &&
        memcmp(parser->input + parser->pos, keyword, keyword_len) == 0) {
        parser->pos += keyword_len;
        parser->column += keyword_len;
        return true;
    }
    return false;
}

static bool json_parser_next_token(json_parser_t *parser) {
    /* Clean up previous token */
    if (parser->current_token.value) {
        puppet_free(parser->current_token.value);
        parser->current_token.value = NULL;
    }

    json_parser_skip_whitespace(parser);

    if (parser->pos >= parser->length) {
        parser->current_token.type = JSON_TOKEN_EOF;
        return true;
    }

    char c = parser->input[parser->pos];

    switch (c) {
        case '{':
            parser->current_token.type = JSON_TOKEN_OBJECT_START;
            parser->pos++;
            parser->column++;
            return true;

        case '}':
            parser->current_token.type = JSON_TOKEN_OBJECT_END;
            parser->pos++;
            parser->column++;
            return true;

        case '[':
            parser->current_token.type = JSON_TOKEN_ARRAY_START;
            parser->pos++;
            parser->column++;
            return true;

        case ']':
            parser->current_token.type = JSON_TOKEN_ARRAY_END;
            parser->pos++;
            parser->column++;
            return true;

        case ':':
            parser->current_token.type = JSON_TOKEN_COLON;
            parser->pos++;
            parser->column++;
            return true;

        case ',':
            parser->current_token.type = JSON_TOKEN_COMMA;
            parser->pos++;
            parser->column++;
            return true;

        case '"':
            return json_parser_read_string(parser);

        case 't':
            if (json_parser_read_keyword(parser, "true")) {
                parser->current_token.type = JSON_TOKEN_BOOLEAN;
                parser->current_token.boolean_value = true;
                return true;
            }
            break;

        case 'f':
            if (json_parser_read_keyword(parser, "false")) {
                parser->current_token.type = JSON_TOKEN_BOOLEAN;
                parser->current_token.boolean_value = false;
                return true;
            }
            break;

        case 'n':
            if (json_parser_read_keyword(parser, "null")) {
                parser->current_token.type = JSON_TOKEN_NULL;
                return true;
            }
            break;

        default:
            if (isdigit(c) || c == '-') {
                return json_parser_read_number(parser);
            }
            break;
    }

    parser->current_token.type = JSON_TOKEN_ERROR;
    json_parser_set_error(parser, "Unexpected character");
    return false;
}

/*
 * ===========================================================================
 * JSON VALUE IMPLEMENTATION
 * ===========================================================================
 */

json_value_t *json_value_create_null(void) {
    json_value_t *value = puppet_calloc(1, sizeof(json_value_t));
    if (value) {
        value->type = JSON_VALUE_NULL;
    }
    return value;
}

json_value_t *json_value_create_bool(bool b) {
    json_value_t *value = puppet_calloc(1, sizeof(json_value_t));
    if (value) {
        value->type = JSON_VALUE_BOOLEAN;
        value->data.boolean_value = b;
    }
    return value;
}

json_value_t *json_value_create_number(double n) {
    json_value_t *value = puppet_calloc(1, sizeof(json_value_t));
    if (value) {
        value->type = JSON_VALUE_NUMBER;
        value->data.number_value = n;
    }
    return value;
}

json_value_t *json_value_create_string(const char *s) {
    json_value_t *value = puppet_calloc(1, sizeof(json_value_t));
    if (value) {
        value->type = JSON_VALUE_STRING;
        value->data.string_value = puppet_strdup(s ? s : "");
    }
    return value;
}

json_value_t *json_value_create_array(void) {
    json_value_t *value = puppet_calloc(1, sizeof(json_value_t));
    if (value) {
        value->type = JSON_VALUE_ARRAY;
        value->data.array.capacity = 8;
        value->data.array.count = 0;
        value->data.array.elements = puppet_calloc(8, sizeof(json_value_t*));
    }
    return value;
}

json_value_t *json_value_create_object(void) {
    json_value_t *value = puppet_calloc(1, sizeof(json_value_t));
    if (value) {
        value->type = JSON_VALUE_OBJECT;
        value->data.object.capacity = 8;
        value->data.object.count = 0;
        value->data.object.keys = puppet_calloc(8, sizeof(char*));
        value->data.object.values = puppet_calloc(8, sizeof(json_value_t*));
    }
    return value;
}

void json_value_destroy(json_value_t *value) {
    if (!value) return;

    switch (value->type) {
        case JSON_VALUE_STRING:
            puppet_free(value->data.string_value);
            break;

        case JSON_VALUE_ARRAY:
            for (size_t i = 0; i < value->data.array.count; i++) {
                json_value_destroy(value->data.array.elements[i]);
            }
            puppet_free(value->data.array.elements);
            break;

        case JSON_VALUE_OBJECT:
            for (size_t i = 0; i < value->data.object.count; i++) {
                puppet_free(value->data.object.keys[i]);
                json_value_destroy(value->data.object.values[i]);
            }
            puppet_free(value->data.object.keys);
            puppet_free(value->data.object.values);
            break;

        default:
            break;
    }

    puppet_free(value);
}

/*
 * ===========================================================================
 * ARRAY AND OBJECT OPERATIONS
 * ===========================================================================
 */

void json_array_append(json_value_t *arr, json_value_t *value) {
    if (!arr || arr->type != JSON_VALUE_ARRAY || !value) return;

    if (arr->data.array.count >= arr->data.array.capacity) {
        arr->data.array.capacity *= 2;
        arr->data.array.elements = puppet_realloc(
            arr->data.array.elements,
            arr->data.array.capacity * sizeof(json_value_t*)
        );
    }

    arr->data.array.elements[arr->data.array.count++] = value;
}

json_value_t *json_array_get(json_value_t *arr, size_t index) {
    if (!arr || arr->type != JSON_VALUE_ARRAY) return NULL;
    if (index >= arr->data.array.count) return NULL;
    return arr->data.array.elements[index];
}

size_t json_array_size(json_value_t *arr) {
    if (!arr || arr->type != JSON_VALUE_ARRAY) return 0;
    return arr->data.array.count;
}

void json_object_set(json_value_t *obj, const char *key, json_value_t *value) {
    if (!obj || obj->type != JSON_VALUE_OBJECT || !key || !value) return;

    /* Check if key already exists */
    for (size_t i = 0; i < obj->data.object.count; i++) {
        if (strcmp(obj->data.object.keys[i], key) == 0) {
            json_value_destroy(obj->data.object.values[i]);
            obj->data.object.values[i] = value;
            return;
        }
    }

    /* Add new key */
    if (obj->data.object.count >= obj->data.object.capacity) {
        obj->data.object.capacity *= 2;
        obj->data.object.keys = puppet_realloc(
            obj->data.object.keys,
            obj->data.object.capacity * sizeof(char*)
        );
        obj->data.object.values = puppet_realloc(
            obj->data.object.values,
            obj->data.object.capacity * sizeof(json_value_t*)
        );
    }

    obj->data.object.keys[obj->data.object.count] = puppet_strdup(key);
    obj->data.object.values[obj->data.object.count] = value;
    obj->data.object.count++;
}

json_value_t *json_object_get(json_value_t *obj, const char *key) {
    if (!obj || obj->type != JSON_VALUE_OBJECT || !key) return NULL;

    for (size_t i = 0; i < obj->data.object.count; i++) {
        if (strcmp(obj->data.object.keys[i], key) == 0) {
            return obj->data.object.values[i];
        }
    }

    return NULL;
}

bool json_object_has(json_value_t *obj, const char *key) {
    return json_object_get(obj, key) != NULL;
}

size_t json_object_size(json_value_t *obj) {
    if (!obj || obj->type != JSON_VALUE_OBJECT) return 0;
    return obj->data.object.count;
}

/*
 * ===========================================================================
 * VALUE TYPE CHECKS AND ACCESSORS
 * ===========================================================================
 */

bool json_is_null(json_value_t *value) {
    return value && value->type == JSON_VALUE_NULL;
}

bool json_is_bool(json_value_t *value) {
    return value && value->type == JSON_VALUE_BOOLEAN;
}

bool json_is_number(json_value_t *value) {
    return value && value->type == JSON_VALUE_NUMBER;
}

bool json_is_string(json_value_t *value) {
    return value && value->type == JSON_VALUE_STRING;
}

bool json_is_array(json_value_t *value) {
    return value && value->type == JSON_VALUE_ARRAY;
}

bool json_is_object(json_value_t *value) {
    return value && value->type == JSON_VALUE_OBJECT;
}

bool json_get_bool(json_value_t *value) {
    if (!value || value->type != JSON_VALUE_BOOLEAN) return false;
    return value->data.boolean_value;
}

double json_get_number(json_value_t *value) {
    if (!value || value->type != JSON_VALUE_NUMBER) return 0.0;
    return value->data.number_value;
}

const char *json_get_string(json_value_t *value) {
    if (!value || value->type != JSON_VALUE_STRING) return NULL;
    return value->data.string_value;
}

/*
 * ===========================================================================
 * CONVENIENCE HELPERS
 * ===========================================================================
 */

char *json_object_get_string(json_value_t *obj, const char *key) {
    json_value_t *val = json_object_get(obj, key);
    if (!val || val->type != JSON_VALUE_STRING) return NULL;
    return puppet_strdup(val->data.string_value);
}

double json_object_get_number(json_value_t *obj, const char *key) {
    json_value_t *val = json_object_get(obj, key);
    if (!val || val->type != JSON_VALUE_NUMBER) return 0.0;
    return val->data.number_value;
}

bool json_object_get_bool(json_value_t *obj, const char *key) {
    json_value_t *val = json_object_get(obj, key);
    if (!val || val->type != JSON_VALUE_BOOLEAN) return false;
    return val->data.boolean_value;
}

/*
 * ===========================================================================
 * JSON PARSING
 * ===========================================================================
 */

static json_value_t *json_parse_object_internal(json_parser_t *parser);
static json_value_t *json_parse_array_internal(json_parser_t *parser);

json_value_t *json_parse_value(json_parser_t *parser) {
    if (!json_parser_next_token(parser)) {
        return NULL;
    }

    switch (parser->current_token.type) {
        case JSON_TOKEN_NULL:
            return json_value_create_null();

        case JSON_TOKEN_BOOLEAN:
            return json_value_create_bool(parser->current_token.boolean_value);

        case JSON_TOKEN_NUMBER:
            return json_value_create_number(parser->current_token.number_value);

        case JSON_TOKEN_STRING:
            return json_value_create_string(parser->current_token.value);

        case JSON_TOKEN_OBJECT_START:
            return json_parse_object_internal(parser);

        case JSON_TOKEN_ARRAY_START:
            return json_parse_array_internal(parser);

        default:
            json_parser_set_error(parser, "Unexpected token");
            return NULL;
    }
}

static json_value_t *json_parse_object_internal(json_parser_t *parser) {
    json_value_t *obj = json_value_create_object();
    if (!obj) return NULL;

    /* Check for empty object */
    if (!json_parser_next_token(parser)) {
        json_value_destroy(obj);
        return NULL;
    }

    if (parser->current_token.type == JSON_TOKEN_OBJECT_END) {
        return obj;
    }

    /* Parse key-value pairs */
    while (true) {
        /* Expect string key */
        if (parser->current_token.type != JSON_TOKEN_STRING) {
            json_parser_set_error(parser, "Expected string key");
            json_value_destroy(obj);
            return NULL;
        }

        char *key = puppet_strdup(parser->current_token.value);

        /* Expect colon */
        if (!json_parser_next_token(parser) ||
            parser->current_token.type != JSON_TOKEN_COLON) {
            json_parser_set_error(parser, "Expected ':' after key");
            puppet_free(key);
            json_value_destroy(obj);
            return NULL;
        }

        /* Parse value */
        json_value_t *value = json_parse_value(parser);
        if (!value) {
            puppet_free(key);
            json_value_destroy(obj);
            return NULL;
        }

        json_object_set(obj, key, value);
        puppet_free(key);

        /* Check for continuation */
        if (!json_parser_next_token(parser)) {
            json_value_destroy(obj);
            return NULL;
        }

        if (parser->current_token.type == JSON_TOKEN_OBJECT_END) {
            break;
        } else if (parser->current_token.type == JSON_TOKEN_COMMA) {
            if (!json_parser_next_token(parser)) {
                json_value_destroy(obj);
                return NULL;
            }
        } else {
            json_parser_set_error(parser, "Expected ',' or '}' in object");
            json_value_destroy(obj);
            return NULL;
        }
    }

    return obj;
}

static json_value_t *json_parse_array_internal(json_parser_t *parser) {
    json_value_t *arr = json_value_create_array();
    if (!arr) return NULL;

    /* Check for empty array */
    if (!json_parser_next_token(parser)) {
        json_value_destroy(arr);
        return NULL;
    }

    if (parser->current_token.type == JSON_TOKEN_ARRAY_END) {
        return arr;
    }

    /* Parse elements */
    while (true) {
        /* Parse element based on current token */
        json_value_t *element = NULL;

        switch (parser->current_token.type) {
            case JSON_TOKEN_NULL:
                element = json_value_create_null();
                break;
            case JSON_TOKEN_BOOLEAN:
                element = json_value_create_bool(parser->current_token.boolean_value);
                break;
            case JSON_TOKEN_NUMBER:
                element = json_value_create_number(parser->current_token.number_value);
                break;
            case JSON_TOKEN_STRING:
                element = json_value_create_string(parser->current_token.value);
                break;
            case JSON_TOKEN_OBJECT_START:
                element = json_parse_object_internal(parser);
                break;
            case JSON_TOKEN_ARRAY_START:
                element = json_parse_array_internal(parser);
                break;
            default:
                json_parser_set_error(parser, "Unexpected token in array");
                json_value_destroy(arr);
                return NULL;
        }

        if (!element) {
            json_value_destroy(arr);
            return NULL;
        }

        json_array_append(arr, element);

        /* Check for continuation */
        if (!json_parser_next_token(parser)) {
            json_value_destroy(arr);
            return NULL;
        }

        if (parser->current_token.type == JSON_TOKEN_ARRAY_END) {
            break;
        } else if (parser->current_token.type == JSON_TOKEN_COMMA) {
            if (!json_parser_next_token(parser)) {
                json_value_destroy(arr);
                return NULL;
            }
        } else {
            json_parser_set_error(parser, "Expected ',' or ']' in array");
            json_value_destroy(arr);
            return NULL;
        }
    }

    return arr;
}

json_value_t *json_parse(const char *input) {
    if (!input) return NULL;

    json_parser_t *parser = json_parser_create(input);
    if (!parser) return NULL;

    json_value_t *result = json_parse_value(parser);

    if (parser->error_message) {
        fprintf(stderr, "JSON parse error at line %d, column %d: %s\n",
                parser->line, parser->column, parser->error_message);
    }

    json_parser_destroy(parser);
    return result;
}

json_value_t *json_parse_file(const char *filepath) {
    FILE *file = fopen(filepath, "r");
    if (!file) return NULL;

    /* Get file size */
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return NULL;
    }
    fseek(file, 0, SEEK_SET);

    /* Read file content */
    char *content = puppet_malloc(file_size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }

    size_t read_size = fread(content, 1, file_size, file);
    content[read_size] = '\0';
    fclose(file);

    /* Parse JSON */
    json_value_t *result = json_parse(content);
    puppet_free(content);

    return result;
}

/*
 * ===========================================================================
 * JSON SERIALIZATION
 * ===========================================================================
 */

void json_value_to_buffer(json_value_t *value, json_buffer_t *buf, int indent) {
    if (!value || !buf) return;

    switch (value->type) {
        case JSON_VALUE_NULL:
            json_buffer_append_null(buf);
            break;

        case JSON_VALUE_BOOLEAN:
            json_buffer_append_bool(buf, value->data.boolean_value);
            break;

        case JSON_VALUE_NUMBER:
            json_buffer_append_double(buf, value->data.number_value);
            break;

        case JSON_VALUE_STRING:
            json_buffer_append_string(buf, value->data.string_value);
            break;

        case JSON_VALUE_ARRAY:
            if (value->data.array.count == 0) {
                json_buffer_append(buf, "[]");
            } else {
                json_buffer_append(buf, "[\n");
                for (size_t i = 0; i < value->data.array.count; i++) {
                    if (i > 0) json_buffer_append(buf, ",\n");
                    json_buffer_indent(buf, indent + 1);
                    json_value_to_buffer(value->data.array.elements[i], buf, indent + 1);
                }
                json_buffer_append(buf, "\n");
                json_buffer_indent(buf, indent);
                json_buffer_append(buf, "]");
            }
            break;

        case JSON_VALUE_OBJECT:
            if (value->data.object.count == 0) {
                json_buffer_append(buf, "{}");
            } else {
                json_buffer_append(buf, "{\n");
                for (size_t i = 0; i < value->data.object.count; i++) {
                    if (i > 0) json_buffer_append(buf, ",\n");
                    json_buffer_indent(buf, indent + 1);
                    json_buffer_append_string(buf, value->data.object.keys[i]);
                    json_buffer_append(buf, ": ");
                    json_value_to_buffer(value->data.object.values[i], buf, indent + 1);
                }
                json_buffer_append(buf, "\n");
                json_buffer_indent(buf, indent);
                json_buffer_append(buf, "}");
            }
            break;
    }
}

char *json_value_to_string(json_value_t *value) {
    if (!value) return NULL;

    json_buffer_t *buf = json_buffer_create();
    if (!buf) return NULL;

    json_value_to_buffer(value, buf, 0);

    char *result = puppet_strdup(buf->data);
    json_buffer_destroy(buf);

    return result;
}
