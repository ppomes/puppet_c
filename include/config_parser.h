/**
 * @file config_parser.h
 * @brief Simple INI configuration file parser
 *
 * Parses INI-style configuration files with sections and key-value pairs.
 * Supports:
 *   - Sections: [section_name]
 *   - Key-value pairs: key = value
 *   - Comments: # or ; at start of line
 *   - Whitespace trimming
 */

#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum length for section names
 */
#define CONFIG_MAX_SECTION 64

/**
 * @brief Maximum length for keys
 */
#define CONFIG_MAX_KEY 64

/**
 * @brief Maximum length for values
 */
#define CONFIG_MAX_VALUE 1024

/**
 * @brief Configuration entry (key-value pair)
 */
typedef struct config_entry {
    char *key;
    char *value;
    struct config_entry *next;
} config_entry_t;

/**
 * @brief Configuration section
 */
typedef struct config_section {
    char *name;
    config_entry_t *entries;
    struct config_section *next;
} config_section_t;

/**
 * @brief Configuration file context
 */
typedef struct config_file {
    char *filepath;
    config_section_t *sections;
} config_file_t;

/**
 * @brief Parse a configuration file
 *
 * @param filepath Path to the configuration file
 * @return Parsed configuration, or NULL on error
 */
config_file_t *config_parse(const char *filepath);

/**
 * @brief Free a configuration file context
 *
 * @param config Configuration to free
 */
void config_free(config_file_t *config);

/**
 * @brief Get a string value from configuration
 *
 * @param config Configuration context
 * @param section Section name (NULL for global/first section)
 * @param key Key name
 * @param default_value Default value if not found
 * @return Value string (do not free), or default_value if not found
 */
const char *config_get_string(config_file_t *config, const char *section,
                               const char *key, const char *default_value);

/**
 * @brief Get an integer value from configuration
 *
 * @param config Configuration context
 * @param section Section name (NULL for global)
 * @param key Key name
 * @param default_value Default value if not found
 * @return Integer value, or default_value if not found
 */
int config_get_int(config_file_t *config, const char *section,
                   const char *key, int default_value);

/**
 * @brief Get a boolean value from configuration
 *
 * Accepts: true/false, yes/no, 1/0, on/off
 *
 * @param config Configuration context
 * @param section Section name (NULL for global)
 * @param key Key name
 * @param default_value Default value if not found
 * @return Boolean value, or default_value if not found
 */
bool config_get_bool(config_file_t *config, const char *section,
                     const char *key, bool default_value);

/**
 * @brief Check if a section exists
 *
 * @param config Configuration context
 * @param section Section name
 * @return true if section exists
 */
bool config_has_section(config_file_t *config, const char *section);

/**
 * @brief Check if a key exists in a section
 *
 * @param config Configuration context
 * @param section Section name (NULL for global)
 * @param key Key name
 * @return true if key exists
 */
bool config_has_key(config_file_t *config, const char *section, const char *key);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_PARSER_H */
