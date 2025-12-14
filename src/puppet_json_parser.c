/**
 * @file puppet_json_parser.c
 * @brief Simple JSON parser for facts loading
 *
 * Lightweight JSON parser specifically designed for parsing facts files.
 * Supports both facter JSON format and PuppetDB export format.
 */

#include "puppet_json_parser.h"
#include "puppet_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/*
 * ===========================================================================
 * JSON PARSER IMPLEMENTATION
 * ===========================================================================
 */

json_parser_t *json_parser_create(const char *input) {
    if (!input) return NULL;
    
    json_parser_t *parser = puppet_calloc(1, sizeof(json_parser_t));
    parser->input = input;
    parser->length = strlen(input);
    parser->pos = 0;
    parser->line = 1;
    parser->column = 1;
    parser->error_message = NULL;
    
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
    
    parser->pos++; // Skip opening quote
    size_t start = parser->pos;
    
    // Find end quote, handling escapes
    while (parser->pos < parser->length) {
        char c = parser->input[parser->pos];
        if (c == '"') {
            // Found end quote
            size_t len = parser->pos - start;
            parser->current_token.value = puppet_malloc(len + 1);
            memcpy(parser->current_token.value, parser->input + start, len);
            parser->current_token.value[len] = '\0';
            parser->current_token.length = len;
            parser->current_token.type = JSON_TOKEN_STRING;
            parser->pos++; // Skip closing quote
            return true;
        } else if (c == '\\') {
            // Skip escaped character
            parser->pos += 2;
        } else {
            parser->pos++;
        }
    }
    
    json_parser_set_error(parser, "Unterminated string");
    return false;
}

static bool json_parser_read_number(json_parser_t *parser) {
    size_t start = parser->pos;
    
    // Handle negative numbers
    if (parser->input[parser->pos] == '-') {
        parser->pos++;
    }
    
    // Read integer part
    while (parser->pos < parser->length && isdigit(parser->input[parser->pos])) {
        parser->pos++;
    }
    
    // Handle decimal part
    if (parser->pos < parser->length && parser->input[parser->pos] == '.') {
        parser->pos++;
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
    parser->current_token.number_value = atof(parser->current_token.value);
    
    return true;
}

static bool json_parser_read_keyword(json_parser_t *parser, const char *keyword) {
    size_t keyword_len = strlen(keyword);
    if (parser->pos + keyword_len <= parser->length &&
        memcmp(parser->input + parser->pos, keyword, keyword_len) == 0) {
        parser->pos += keyword_len;
        return true;
    }
    return false;
}

static bool json_parser_next_token(json_parser_t *parser) {
    // Clean up previous token
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
            return true;
            
        case '}':
            parser->current_token.type = JSON_TOKEN_OBJECT_END;
            parser->pos++;
            return true;
            
        case '[':
            parser->current_token.type = JSON_TOKEN_ARRAY_START;
            parser->pos++;
            return true;
            
        case ']':
            parser->current_token.type = JSON_TOKEN_ARRAY_END;
            parser->pos++;
            return true;
            
        case ':':
            parser->current_token.type = JSON_TOKEN_COLON;
            parser->pos++;
            return true;
            
        case ',':
            parser->current_token.type = JSON_TOKEN_COMMA;
            parser->pos++;
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

void json_value_destroy(json_value_t *value) {
    if (!value) return;
    
    switch (value->type) {
        case JSON_VALUE_STRING:
            puppet_free(value->data.string_value);
            break;
            
        case JSON_VALUE_OBJECT:
            for (size_t i = 0; i < value->data.object.count; i++) {
                puppet_free(value->data.object.keys[i]);
                json_value_destroy(value->data.object.values[i]);
            }
            puppet_free(value->data.object.keys);
            puppet_free(value->data.object.values);
            break;
            
        case JSON_VALUE_ARRAY:
            for (size_t i = 0; i < value->data.array.count; i++) {
                json_value_destroy(value->data.array.elements[i]);
            }
            puppet_free(value->data.array.elements);
            break;
            
        default:
            break;
    }
    
    puppet_free(value);
}

static json_value_t *json_parse_object(json_parser_t *parser);
static json_value_t *json_parse_array(json_parser_t *parser);

json_value_t *json_parse_value(json_parser_t *parser) {
    if (!json_parser_next_token(parser)) {
        return NULL;
    }
    
    json_value_t *value = puppet_calloc(1, sizeof(json_value_t));
    
    switch (parser->current_token.type) {
        case JSON_TOKEN_STRING:
            value->type = JSON_VALUE_STRING;
            value->data.string_value = puppet_strdup(parser->current_token.value);
            break;
            
        case JSON_TOKEN_NUMBER:
            value->type = JSON_VALUE_NUMBER;
            value->data.number_value = parser->current_token.number_value;
            break;
            
        case JSON_TOKEN_BOOLEAN:
            value->type = JSON_VALUE_BOOLEAN;
            value->data.boolean_value = parser->current_token.boolean_value;
            break;
            
        case JSON_TOKEN_NULL:
            value->type = JSON_VALUE_NULL;
            break;
            
        case JSON_TOKEN_OBJECT_START:
            puppet_free(value);
            return json_parse_object(parser);
            
        case JSON_TOKEN_ARRAY_START:
            puppet_free(value);
            return json_parse_array(parser);
            
        default:
            puppet_free(value);
            json_parser_set_error(parser, "Unexpected token");
            return NULL;
    }
    
    return value;
}

static json_value_t *json_parse_object(json_parser_t *parser) {
    json_value_t *obj = puppet_calloc(1, sizeof(json_value_t));
    obj->type = JSON_VALUE_OBJECT;
    obj->data.object.count = 0;
    obj->data.object.keys = NULL;
    obj->data.object.values = NULL;
    
    size_t capacity = 8;
    obj->data.object.keys = puppet_calloc(capacity, sizeof(char*));
    obj->data.object.values = puppet_calloc(capacity, sizeof(json_value_t*));
    
    // Check for empty object
    if (!json_parser_next_token(parser)) {
        json_value_destroy(obj);
        return NULL;
    }
    
    if (parser->current_token.type == JSON_TOKEN_OBJECT_END) {
        return obj;
    }
    
    // Parse key-value pairs
    while (true) {
        // Expect string key
        if (parser->current_token.type != JSON_TOKEN_STRING) {
            json_parser_set_error(parser, "Expected string key");
            json_value_destroy(obj);
            return NULL;
        }
        
        // Expand arrays if needed
        if (obj->data.object.count >= capacity) {
            capacity *= 2;
            obj->data.object.keys = puppet_realloc(obj->data.object.keys, capacity * sizeof(char*));
            obj->data.object.values = puppet_realloc(obj->data.object.values, capacity * sizeof(json_value_t*));
        }
        
        // Store key
        obj->data.object.keys[obj->data.object.count] = puppet_strdup(parser->current_token.value);
        
        // Expect colon
        if (!json_parser_next_token(parser) || parser->current_token.type != JSON_TOKEN_COLON) {
            json_parser_set_error(parser, "Expected ':' after key");
            json_value_destroy(obj);
            return NULL;
        }
        
        // Parse value
        json_value_t *value = json_parse_value(parser);
        if (!value) {
            json_value_destroy(obj);
            return NULL;
        }
        
        obj->data.object.values[obj->data.object.count] = value;
        obj->data.object.count++;
        
        // Check for continuation
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

static json_value_t *json_parse_array(json_parser_t *parser) {
    json_value_t *arr = puppet_calloc(1, sizeof(json_value_t));
    arr->type = JSON_VALUE_ARRAY;
    arr->data.array.count = 0;
    arr->data.array.elements = NULL;
    
    size_t capacity = 8;
    arr->data.array.elements = puppet_calloc(capacity, sizeof(json_value_t*));
    
    // Check for empty array
    if (!json_parser_next_token(parser)) {
        json_value_destroy(arr);
        return NULL;
    }
    
    if (parser->current_token.type == JSON_TOKEN_ARRAY_END) {
        return arr;
    }
    
    // Parse array elements
    while (true) {
        // Expand array if needed
        if (arr->data.array.count >= capacity) {
            capacity *= 2;
            arr->data.array.elements = puppet_realloc(arr->data.array.elements, capacity * sizeof(json_value_t*));
        }
        
        // Parse element (current token is already read)
        json_value_t *element;
        if (parser->current_token.type == JSON_TOKEN_OBJECT_START) {
            element = json_parse_object(parser);
        } else if (parser->current_token.type == JSON_TOKEN_ARRAY_START) {
            element = json_parse_array(parser);
        } else {
            // Create value from current token
            element = puppet_calloc(1, sizeof(json_value_t));
            switch (parser->current_token.type) {
                case JSON_TOKEN_STRING:
                    element->type = JSON_VALUE_STRING;
                    element->data.string_value = puppet_strdup(parser->current_token.value);
                    break;
                case JSON_TOKEN_NUMBER:
                    element->type = JSON_VALUE_NUMBER;
                    element->data.number_value = parser->current_token.number_value;
                    break;
                case JSON_TOKEN_BOOLEAN:
                    element->type = JSON_VALUE_BOOLEAN;
                    element->data.boolean_value = parser->current_token.boolean_value;
                    break;
                case JSON_TOKEN_NULL:
                    element->type = JSON_VALUE_NULL;
                    break;
                default:
                    puppet_free(element);
                    json_parser_set_error(parser, "Unexpected token in array");
                    json_value_destroy(arr);
                    return NULL;
            }
        }
        
        if (!element) {
            json_value_destroy(arr);
            return NULL;
        }
        
        arr->data.array.elements[arr->data.array.count++] = element;
        
        // Check for continuation
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

json_value_t *json_parse_file(const char *filepath) {
    FILE *file = fopen(filepath, "r");
    if (!file) {
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Read file content
    char *content = puppet_malloc(file_size + 1);
    size_t read_size = fread(content, 1, file_size, file);
    content[read_size] = '\0';
    
    fclose(file);
    
    // Parse JSON
    json_parser_t *parser = json_parser_create(content);
    json_value_t *result = json_parse_value(parser);
    
    if (parser->error_message) {
        printf("JSON Parse Error: %s at line %d, column %d\n", 
               parser->error_message, parser->line, parser->column);
    }
    
    json_parser_destroy(parser);
    puppet_free(content);
    
    return result;
}

/*
 * ===========================================================================
 * JSON VALUE UTILITY FUNCTIONS
 * ===========================================================================
 */

json_value_t *json_object_get(json_value_t *obj, const char *key) {
    if (!obj || obj->type != JSON_VALUE_OBJECT || !key) {
        return NULL;
    }
    
    for (size_t i = 0; i < obj->data.object.count; i++) {
        if (strcmp(obj->data.object.keys[i], key) == 0) {
            return obj->data.object.values[i];
        }
    }
    
    return NULL;
}

json_value_t *json_array_get(json_value_t *arr, size_t index) {
    if (!arr || arr->type != JSON_VALUE_ARRAY || index >= arr->data.array.count) {
        return NULL;
    }
    
    return arr->data.array.elements[index];
}

const char *json_value_to_string(json_value_t *value) {
    if (!value || value->type != JSON_VALUE_STRING) {
        return NULL;
    }
    return value->data.string_value;
}

double json_value_to_number(json_value_t *value) {
    if (!value || value->type != JSON_VALUE_NUMBER) {
        return 0.0;
    }
    return value->data.number_value;
}

bool json_value_to_boolean(json_value_t *value) {
    if (!value || value->type != JSON_VALUE_BOOLEAN) {
        return false;
    }
    return value->data.boolean_value;
}

puppet_value_t *json_value_to_puppet_value(json_value_t *json_val) {
    if (!json_val) {
        return puppet_value_create_undef();
    }
    
    switch (json_val->type) {
        case JSON_VALUE_STRING:
            return puppet_value_create_string(json_val->data.string_value, 
                                            strlen(json_val->data.string_value));
        case JSON_VALUE_NUMBER:
            return puppet_value_create_number(json_val->data.number_value);
        case JSON_VALUE_BOOLEAN:
            return puppet_value_create_bool(json_val->data.boolean_value);
        case JSON_VALUE_NULL:
            return puppet_value_create_undef();
        default:
            // For objects and arrays, convert to string representation
            return puppet_value_create_string("(complex)", 9);
    }
}