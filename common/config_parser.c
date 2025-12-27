/**
 * @file config_parser.c
 * @brief Simple INI configuration file parser implementation
 */

#include "config_parser.h"
#include "puppet_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/**
 * @brief Trim leading and trailing whitespace in place
 */
static char *trim(char *str) {
    if (!str) return NULL;

    /* Trim leading whitespace */
    while (isspace((unsigned char)*str)) str++;

    if (*str == '\0') return str;

    /* Trim trailing whitespace */
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';

    return str;
}

/**
 * @brief Find or create a section
 */
static config_section_t *find_or_create_section(config_file_t *config, const char *name) {
    /* Search for existing section */
    config_section_t *section = config->sections;
    config_section_t *prev = NULL;

    while (section) {
        if (strcmp(section->name, name) == 0) {
            return section;
        }
        prev = section;
        section = section->next;
    }

    /* Create new section */
    section = puppet_calloc(1, sizeof(config_section_t));
    section->name = puppet_strdup(name);
    section->entries = NULL;
    section->next = NULL;

    if (prev) {
        prev->next = section;
    } else {
        config->sections = section;
    }

    return section;
}

/**
 * @brief Add an entry to a section
 */
static void add_entry(config_section_t *section, const char *key, const char *value) {
    config_entry_t *entry = puppet_calloc(1, sizeof(config_entry_t));
    entry->key = puppet_strdup(key);
    entry->value = puppet_strdup(value);
    entry->next = NULL;

    /* Add to end of list */
    if (!section->entries) {
        section->entries = entry;
    } else {
        config_entry_t *e = section->entries;
        while (e->next) e = e->next;
        e->next = entry;
    }
}

config_file_t *config_parse(const char *filepath) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        return NULL;
    }

    config_file_t *config = puppet_calloc(1, sizeof(config_file_t));
    config->filepath = puppet_strdup(filepath);
    config->sections = NULL;

    /* Create a default "main" section for entries before any section header */
    config_section_t *current_section = find_or_create_section(config, "main");

    char line[CONFIG_MAX_VALUE + CONFIG_MAX_KEY + 16];
    int line_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;

        /* Remove newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (len > 1 && line[len - 2] == '\r') line[len - 2] = '\0';

        char *trimmed = trim(line);

        /* Skip empty lines and comments */
        if (*trimmed == '\0' || *trimmed == '#' || *trimmed == ';') {
            continue;
        }

        /* Section header */
        if (*trimmed == '[') {
            char *end = strchr(trimmed, ']');
            if (!end) {
                fprintf(stderr, "Warning: %s:%d: Malformed section header\n",
                        filepath, line_num);
                continue;
            }
            *end = '\0';
            char *section_name = trim(trimmed + 1);
            current_section = find_or_create_section(config, section_name);
            continue;
        }

        /* Key = value */
        char *equals = strchr(trimmed, '=');
        if (!equals) {
            fprintf(stderr, "Warning: %s:%d: Malformed line (no '=')\n",
                    filepath, line_num);
            continue;
        }

        *equals = '\0';
        char *key = trim(trimmed);
        char *value = trim(equals + 1);

        if (*key == '\0') {
            fprintf(stderr, "Warning: %s:%d: Empty key\n", filepath, line_num);
            continue;
        }

        add_entry(current_section, key, value);
    }

    fclose(fp);
    return config;
}

void config_free(config_file_t *config) {
    if (!config) return;

    config_section_t *section = config->sections;
    while (section) {
        config_section_t *next_section = section->next;

        config_entry_t *entry = section->entries;
        while (entry) {
            config_entry_t *next_entry = entry->next;
            puppet_free(entry->key);
            puppet_free(entry->value);
            puppet_free(entry);
            entry = next_entry;
        }

        puppet_free(section->name);
        puppet_free(section);
        section = next_section;
    }

    puppet_free(config->filepath);
    puppet_free(config);
}

/**
 * @brief Find a section by name
 */
static config_section_t *find_section(config_file_t *config, const char *section_name) {
    if (!config) return NULL;

    /* Default to "main" section */
    if (!section_name) section_name = "main";

    config_section_t *section = config->sections;
    while (section) {
        if (strcmp(section->name, section_name) == 0) {
            return section;
        }
        section = section->next;
    }
    return NULL;
}

/**
 * @brief Find an entry in a section
 */
static config_entry_t *find_entry(config_section_t *section, const char *key) {
    if (!section) return NULL;

    config_entry_t *entry = section->entries;
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

const char *config_get_string(config_file_t *config, const char *section,
                               const char *key, const char *default_value) {
    config_section_t *sec = find_section(config, section);
    if (!sec) return default_value;

    config_entry_t *entry = find_entry(sec, key);
    if (!entry) return default_value;

    return entry->value;
}

int config_get_int(config_file_t *config, const char *section,
                   const char *key, int default_value) {
    const char *value = config_get_string(config, section, key, NULL);
    if (!value) return default_value;

    char *endptr;
    long result = strtol(value, &endptr, 10);

    /* Check for valid conversion */
    if (endptr == value || *endptr != '\0') {
        return default_value;
    }

    return (int)result;
}

bool config_get_bool(config_file_t *config, const char *section,
                     const char *key, bool default_value) {
    const char *value = config_get_string(config, section, key, NULL);
    if (!value) return default_value;

    /* True values */
    if (strcasecmp(value, "true") == 0 ||
        strcasecmp(value, "yes") == 0 ||
        strcasecmp(value, "on") == 0 ||
        strcmp(value, "1") == 0) {
        return true;
    }

    /* False values */
    if (strcasecmp(value, "false") == 0 ||
        strcasecmp(value, "no") == 0 ||
        strcasecmp(value, "off") == 0 ||
        strcmp(value, "0") == 0) {
        return false;
    }

    return default_value;
}

bool config_has_section(config_file_t *config, const char *section) {
    return find_section(config, section) != NULL;
}

bool config_has_key(config_file_t *config, const char *section, const char *key) {
    config_section_t *sec = find_section(config, section);
    if (!sec) return false;
    return find_entry(sec, key) != NULL;
}
