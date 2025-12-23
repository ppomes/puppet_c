/**
 * @file puppet_json_parser.c
 * @brief Puppet-specific JSON utilities
 *
 * This file provides Puppet-specific extensions to the common JSON library:
 * - Conversion from json_value_t to puppet_value_t
 *
 * Core JSON parsing is provided by libpuppetc_common (common/puppet_json.c).
 */

#include "puppet_json_parser.h"
#include "puppet_memory.h"
#include <string.h>

/*
 * ===========================================================================
 * JSON TO PUPPET VALUE CONVERSION
 * ===========================================================================
 */

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

        case JSON_VALUE_ARRAY:
        case JSON_VALUE_OBJECT:
            /* Complex types are serialized as string representation */
            /* TODO: Implement proper array/hash conversion when needed */
            return puppet_value_create_string("(complex)", 9);

        default:
            return puppet_value_create_undef();
    }
}
