/**
 * @file puppet_hiera_simple.c
 * @brief Simplified Hiera implementation that compiles with current API
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <yaml.h>
#include "puppet_hiera.h"
#include "puppet_memory.h"
#include "puppet_stdlib.h"

/* YAML support is now mandatory */

/* Forward declarations for YAML parsing */
static puppet_value_t *yaml_node_to_puppet_value(yaml_node_t *node, yaml_document_t *document);

/**
 * @brief Convert YAML sequence to Puppet array
 */
static puppet_value_t *yaml_sequence_to_array(yaml_node_t *node, yaml_document_t *document) {
    puppet_value_t *array = puppet_value_create_array();
    
    for (yaml_node_item_t *item = node->data.sequence.items.start;
         item < node->data.sequence.items.top; item++) {
        yaml_node_t *value_node = yaml_document_get_node(document, *item);
        if (value_node) {
            puppet_value_t *value = yaml_node_to_puppet_value(value_node, document);
            if (value) {
                puppet_array_append(array->data.array, value);
            }
        }
    }
    
    return array;
}

/**
 * @brief Convert YAML mapping to Puppet hash
 */
static puppet_value_t *yaml_mapping_to_hash(yaml_node_t *node, yaml_document_t *document) {
    puppet_value_t *hash = puppet_value_create_hash();
    
    for (yaml_node_pair_t *pair = node->data.mapping.pairs.start;
         pair < node->data.mapping.pairs.top; pair++) {
        yaml_node_t *key_node = yaml_document_get_node(document, pair->key);
        yaml_node_t *value_node = yaml_document_get_node(document, pair->value);
        
        if (key_node && value_node && key_node->type == YAML_SCALAR_NODE) {
            char *key = (char *)key_node->data.scalar.value;
            puppet_value_t *value = yaml_node_to_puppet_value(value_node, document);
            if (value) {
                puppet_hash_set(hash->data.hash, key, strlen(key), value);
            }
        }
    }
    
    return hash;
}

/**
 * @brief Convert YAML scalar to appropriate Puppet value type
 */
static puppet_value_t *yaml_scalar_to_value(yaml_node_t *node) {
    char *scalar = (char *)node->data.scalar.value;
    
    /* Check for boolean values */
    if (strcmp(scalar, "true") == 0 || strcmp(scalar, "yes") == 0 || 
        strcmp(scalar, "on") == 0 || strcmp(scalar, "TRUE") == 0) {
        return puppet_value_create_bool(1);
    }
    if (strcmp(scalar, "false") == 0 || strcmp(scalar, "no") == 0 || 
        strcmp(scalar, "off") == 0 || strcmp(scalar, "FALSE") == 0) {
        return puppet_value_create_bool(0);
    }
    
    /* Check for null */
    if (strcmp(scalar, "null") == 0 || strcmp(scalar, "~") == 0 || 
        strcmp(scalar, "NULL") == 0 || strlen(scalar) == 0) {
        return puppet_value_create_undef();
    }
    
    /* Check for number */
    char *endptr;
    double num = strtod(scalar, &endptr);
    if (*endptr == '\0') {
        return puppet_value_create_number(num);
    }
    
    /* Default to string */
    return puppet_value_create_string(scalar, strlen(scalar));
}

/**
 * @brief Convert YAML node to Puppet value
 */
static puppet_value_t *yaml_node_to_puppet_value(yaml_node_t *node, yaml_document_t *document) {
    if (!node) return NULL;
    
    switch (node->type) {
        case YAML_SCALAR_NODE:
            return yaml_scalar_to_value(node);
            
        case YAML_SEQUENCE_NODE:
            return yaml_sequence_to_array(node, document);
            
        case YAML_MAPPING_NODE:
            return yaml_mapping_to_hash(node, document);
            
        default:
            return NULL;
    }
}

/**
 * @brief Load and parse YAML file into Puppet value
 */
puppet_value_t *puppet_hiera_load_yaml(const char *filepath) {
    FILE *file = fopen(filepath, "r");
    if (!file) {
        return NULL;
    }
    
    yaml_parser_t parser;
    yaml_document_t document;
    puppet_value_t *result = NULL;
    
    if (!yaml_parser_initialize(&parser)) {
        fclose(file);
        return NULL;
    }
    
    yaml_parser_set_input_file(&parser, file);
    
    if (yaml_parser_load(&parser, &document)) {
        yaml_node_t *root = yaml_document_get_root_node(&document);
        if (root) {
            result = yaml_node_to_puppet_value(root, &document);
        }
        yaml_document_delete(&document);
    }
    
    yaml_parser_delete(&parser);
    fclose(file);
    
    return result;
}

/* YAML support is now mandatory - removed conditional compilation */

/**
 * @brief Create default Hiera configuration
 */
puppet_hiera_config_t *puppet_hiera_config_create_default(const char *datadir) {
    puppet_hiera_config_t *config = puppet_calloc(1, sizeof(puppet_hiera_config_t));
    config->version = 5;
    config->default_merge = HIERA_MERGE_FIRST;
    config->datadir = puppet_strdup(datadir ? datadir : "data");
    
    // Create simple hierarchy
    puppet_hiera_level_t *common = puppet_calloc(1, sizeof(puppet_hiera_level_t));
    common->name = puppet_strdup("Common");
    common->path_template = puppet_strdup("common.yaml");
    common->datadir = puppet_strdup(config->datadir);
    common->backend = HIERA_BACKEND_YAML;
    config->hierarchy = common;
    
    // Initialize cache and defaults as empty hashes
    config->cache = puppet_value_create_hash();
    config->defaults = puppet_value_create_hash();
    
    return config;
}

/**
 * @brief Create Hiera configuration from file (hiera.yaml)
 */
puppet_hiera_config_t *puppet_hiera_config_create(const char *config_path) {
    puppet_value_t *yaml_config = puppet_hiera_load_yaml(config_path);
    if (!yaml_config || yaml_config->type != PUPPET_VALUE_HASH) {
        if (yaml_config) puppet_value_destroy(yaml_config);
        return puppet_hiera_config_create_default("data");
    }

    /* Extract base directory from config_path */
    char basedir[1024] = {0};
    const char *last_slash = strrchr(config_path, '/');
    if (last_slash) {
        size_t len = last_slash - config_path;
        if (len < sizeof(basedir)) {
            memcpy(basedir, config_path, len);
            basedir[len] = '\0';
        }
    } else {
        strcpy(basedir, ".");
    }

    puppet_hiera_config_t *config = puppet_calloc(1, sizeof(puppet_hiera_config_t));
    config->version = 5;
    config->default_merge = HIERA_MERGE_FIRST;

    /* Get datadir from :yaml: or :json: section, or use default */
    const char *rel_datadir = "hieradata";
    puppet_value_t *yaml_section = puppet_hash_get(yaml_config->data.hash, ":yaml:", 6);
    if (yaml_section && yaml_section->type == PUPPET_VALUE_HASH) {
        puppet_value_t *datadir_val = puppet_hash_get(yaml_section->data.hash, ":datadir", 8);
        if (datadir_val && datadir_val->type == PUPPET_VALUE_STRING) {
            rel_datadir = datadir_val->data.string.data;
        }
    }
    /* Make datadir absolute relative to basedir */
    char abs_datadir[1024];
    snprintf(abs_datadir, sizeof(abs_datadir), "%s/%s", basedir, rel_datadir);
    config->datadir = puppet_strdup(abs_datadir);

    /* Parse hierarchy */
    puppet_value_t *hierarchy = puppet_hash_get(yaml_config->data.hash, ":hierarchy", 10);
    if (hierarchy && hierarchy->type == PUPPET_VALUE_ARRAY) {
        puppet_hiera_level_t *last_level = NULL;
        for (size_t i = 0; i < hierarchy->data.array->count; i++) {
            puppet_value_t *item = hierarchy->data.array->items[i];
            if (item && item->type == PUPPET_VALUE_STRING) {
                puppet_hiera_level_t *level = puppet_calloc(1, sizeof(puppet_hiera_level_t));
                /* Build path template with .yaml extension */
                size_t path_len = item->data.string.len + 6; /* ".yaml\0" */
                level->path_template = puppet_malloc(path_len);
                snprintf(level->path_template, path_len, "%s.yaml", item->data.string.data);
                level->name = puppet_strdup(item->data.string.data);
                level->datadir = puppet_strdup(config->datadir);
                level->backend = HIERA_BACKEND_YAML;

                if (last_level) {
                    last_level->next = level;
                } else {
                    config->hierarchy = level;
                }
                last_level = level;
            }
        }
    }

    /* Initialize cache and defaults */
    config->cache = puppet_value_create_hash();
    config->defaults = puppet_value_create_hash();

    puppet_value_destroy(yaml_config);
    return config;
}

/**
 * @brief Destroy Hiera configuration
 */
void puppet_hiera_config_destroy(puppet_hiera_config_t *config) {
    if (!config) return;
    
    puppet_hiera_level_t *level = config->hierarchy;
    while (level) {
        puppet_hiera_level_t *next = level->next;
        puppet_free(level->name);
        puppet_free(level->path_template);
        puppet_free(level->datadir);
        puppet_free(level);
        level = next;
    }
    
    puppet_free(config->datadir);
    if (config->cache) puppet_value_destroy((puppet_value_t *)config->cache);
    if (config->defaults) puppet_value_destroy((puppet_value_t *)config->defaults);
    puppet_free(config);
}

/**
 * @brief Create Hiera context
 */
puppet_hiera_context_t *puppet_hiera_context_create(
    puppet_hiera_config_t *config,
    puppet_env_t *env
) {
    puppet_hiera_context_t *context = puppet_calloc(1, sizeof(puppet_hiera_context_t));
    context->config = config;
    context->env = env;
    context->variables = puppet_value_create_hash();
    
    // Set up basic variables
    if (env && env->node_name) {
        context->node = puppet_strdup(env->node_name);
        puppet_hash_set(context->variables->data.hash, "::fqdn", strlen("::fqdn"),
            puppet_value_create_string(env->node_name, strlen(env->node_name)));
    }
    
    return context;
}

/**
 * @brief Destroy Hiera context
 */
void puppet_hiera_context_destroy(puppet_hiera_context_t *context) {
    if (!context) return;
    
    puppet_free(context->environment);
    puppet_free(context->node);
    if (context->variables) puppet_value_destroy((puppet_value_t *)context->variables);
    puppet_free(context);
}

/**
 * @brief Interpolate variables in template string
 * Supports %{variable} and %{::variable} syntax
 */
char *puppet_hiera_interpolate(const char *template, puppet_hiera_context_t *context) {
    if (!template) return NULL;
    if (!context) return puppet_strdup(template);

    /* Estimate result size - may need expansion */
    size_t result_size = strlen(template) * 2 + 256;
    char *result = puppet_malloc(result_size);
    size_t result_pos = 0;

    const char *p = template;
    while (*p) {
        if (p[0] == '%' && p[1] == '{') {
            /* Find end of variable */
            const char *end = strchr(p + 2, '}');
            if (end) {
                /* Extract variable name */
                size_t var_len = end - (p + 2);
                char *var_name = puppet_malloc(var_len + 1);
                memcpy(var_name, p + 2, var_len);
                var_name[var_len] = '\0';

                /* Look up variable value */
                const char *value = NULL;
                char value_buf[256] = {0};

                /* Check context variables first */
                if (context->variables) {
                    puppet_value_t *var_val = puppet_hash_get(context->variables->data.hash,
                        var_name, strlen(var_name));
                    if (var_val && var_val->type == PUPPET_VALUE_STRING) {
                        value = var_val->data.string.data;
                    }
                }

                /* Check environment for scope lookups */
                if (!value && context->env) {
                    /* Strip leading :: for lookup */
                    const char *lookup_name = var_name;
                    if (strncmp(lookup_name, "::", 2) == 0) {
                        lookup_name += 2;
                    }
                    /* Set flag to prevent recursive hiera lookups during path interpolation */
                    bool was_in_interpolation = context->env->in_hiera_interpolation;
                    context->env->in_hiera_interpolation = true;
                    puppet_value_t *looked_up = puppet_variable_lookup_chain(context->env, lookup_name);
                    context->env->in_hiera_interpolation = was_in_interpolation;
                    if (looked_up && looked_up->type == PUPPET_VALUE_STRING) {
                        strncpy(value_buf, looked_up->data.string.data, sizeof(value_buf) - 1);
                        value = value_buf;
                    }
                }

                /* Append value or empty string */
                if (value) {
                    size_t value_len = strlen(value);
                    if (result_pos + value_len >= result_size - 1) {
                        result_size = result_size * 2 + value_len;
                        result = puppet_realloc(result, result_size);
                    }
                    strcpy(result + result_pos, value);
                    result_pos += value_len;
                }

                puppet_free(var_name);
                p = end + 1;
                continue;
            }
        }

        /* Copy regular character */
        if (result_pos >= result_size - 1) {
            result_size *= 2;
            result = puppet_realloc(result, result_size);
        }
        result[result_pos++] = *p++;
    }

    result[result_pos] = '\0';
    return result;
}

/**
 * @brief Check if array contains a value (for unique merge)
 */
static bool array_contains_value(puppet_array_t *array, puppet_value_t *value) {
    if (!array || !value) return false;

    for (size_t i = 0; i < array->count; i++) {
        puppet_value_t *item = array->items[i];
        if (!item) continue;

        /* Simple equality check for strings */
        if (item->type == PUPPET_VALUE_STRING && value->type == PUPPET_VALUE_STRING) {
            if (item->data.string.len == value->data.string.len &&
                memcmp(item->data.string.data, value->data.string.data, item->data.string.len) == 0) {
                return true;
            }
        }
        /* Simple equality check for numbers */
        else if (item->type == PUPPET_VALUE_NUMBER && value->type == PUPPET_VALUE_NUMBER) {
            if (item->data.number == value->data.number) {
                return true;
            }
        }
        /* Simple equality check for booleans */
        else if (item->type == PUPPET_VALUE_BOOL && value->type == PUPPET_VALUE_BOOL) {
            if (item->data.boolean == value->data.boolean) {
                return true;
            }
        }
    }
    return false;
}

/**
 * @brief Deep merge two hash values recursively
 */
static puppet_value_t *deep_merge_hashes(puppet_value_t *base, puppet_value_t *overlay) {
    if (!base || base->type != PUPPET_VALUE_HASH) {
        return overlay ? puppet_value_copy(overlay) : NULL;
    }
    if (!overlay || overlay->type != PUPPET_VALUE_HASH) {
        return puppet_value_copy(base);
    }

    puppet_value_t *result = puppet_value_copy(base);

    /* Iterate over overlay hash and merge into result */
    for (size_t i = 0; i < overlay->data.hash->bucket_count; i++) {
        puppet_hash_entry_t *entry = overlay->data.hash->buckets[i];
        while (entry) {
            puppet_value_t *existing = puppet_hash_get(result->data.hash,
                                                        entry->key.data, entry->key.len);

            if (existing && existing->type == PUPPET_VALUE_HASH &&
                entry->value->type == PUPPET_VALUE_HASH) {
                /* Recursively merge nested hashes */
                puppet_value_t *merged = deep_merge_hashes(existing, entry->value);
                puppet_hash_set(result->data.hash, entry->key.data, entry->key.len, merged);
            } else {
                /* Overlay value wins */
                puppet_hash_set(result->data.hash, entry->key.data, entry->key.len,
                               puppet_value_copy(entry->value));
            }
            entry = entry->next;
        }
    }

    return result;
}

/**
 * @brief Merge values according to strategy
 */
puppet_value_t *puppet_hiera_merge_values(
    puppet_value_t *existing,
    puppet_value_t *new,
    puppet_hiera_merge_t strategy
) {
    if (!new) return existing;
    if (!existing) return puppet_value_copy(new);

    switch (strategy) {
        case HIERA_MERGE_FIRST:
            /* First value wins - existing is already first */
            return existing;

        case HIERA_MERGE_UNIQUE:
            /* Array merge with unique values */
            if (existing->type == PUPPET_VALUE_ARRAY && new->type == PUPPET_VALUE_ARRAY) {
                for (size_t i = 0; i < new->data.array->count; i++) {
                    puppet_value_t *item = new->data.array->items[i];
                    if (item && !array_contains_value(existing->data.array, item)) {
                        puppet_array_append(existing->data.array, puppet_value_copy(item));
                    }
                }
            }
            return existing;

        case HIERA_MERGE_HASH:
            /* Shallow hash merge - overlay keys on existing */
            if (existing->type == PUPPET_VALUE_HASH && new->type == PUPPET_VALUE_HASH) {
                for (size_t i = 0; i < new->data.hash->bucket_count; i++) {
                    puppet_hash_entry_t *entry = new->data.hash->buckets[i];
                    while (entry) {
                        /* Only add if key doesn't exist in existing */
                        if (!puppet_hash_get(existing->data.hash, entry->key.data, entry->key.len)) {
                            puppet_hash_set(existing->data.hash, entry->key.data, entry->key.len,
                                           puppet_value_copy(entry->value));
                        }
                        entry = entry->next;
                    }
                }
            }
            return existing;

        case HIERA_MERGE_DEEP:
            /* Deep hash merge */
            if (existing->type == PUPPET_VALUE_HASH && new->type == PUPPET_VALUE_HASH) {
                puppet_value_t *merged = deep_merge_hashes(existing, new);
                puppet_value_destroy(existing);
                return merged;
            }
            return existing;
    }

    return existing;
}

/**
 * @brief Perform Hiera lookup
 */
puppet_value_t *puppet_hiera_lookup(
    puppet_hiera_context_t *context,
    const char *key,
    puppet_value_t *default_value,
    puppet_hiera_merge_t merge_strategy
) {
    if (!context || !key) return default_value;

    // Check cache first
    puppet_value_t *cached = puppet_hash_get(context->config->cache->data.hash, key, strlen(key));
    if (cached) {
        return cached;
    }

    /* Extract module_name from key (e.g., "tomee::wslist" -> "tomee") */
    char module_name[256] = {0};
    const char *sep = strstr(key, "::");
    if (sep) {
        size_t len = sep - key;
        if (len < sizeof(module_name)) {
            memcpy(module_name, key, len);
            module_name[len] = '\0';
        }
    }

    /* Add module_name to context variables for interpolation */
    if (module_name[0] && context->variables) {
        puppet_hash_set(context->variables->data.hash, "module_name", 11,
            puppet_value_create_string(module_name, strlen(module_name)));
    }

    puppet_value_t *result = NULL;

    // Iterate through hierarchy levels
    for (puppet_hiera_level_t *level = context->config->hierarchy; level; level = level->next) {
        /* Interpolate the path template to expand variables */
        char *interpolated_path = puppet_hiera_interpolate(level->path_template, context);
        if (!interpolated_path) continue;

        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", level->datadir, interpolated_path);
        puppet_free(interpolated_path);

        struct stat st;
        if (stat(fullpath, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }

        puppet_value_t *data = puppet_hiera_load_yaml(fullpath);
        if (data && data->type == PUPPET_VALUE_HASH) {
            puppet_value_t *value = puppet_hash_get(data->data.hash, key, strlen(key));

            if (value) {
                if (merge_strategy == HIERA_MERGE_FIRST) {
                    /* First match wins - return immediately */
                    result = puppet_value_copy(value);
                    puppet_value_destroy(data);
                    break;
                } else {
                    /* Merge with existing result */
                    result = puppet_hiera_merge_values(result, value, merge_strategy);
                }
            }

            puppet_value_destroy(data);
        }
    }

    // Use default if nothing found
    if (!result && default_value) {
        result = puppet_value_copy(default_value);
    }

    // Cache the result
    if (result) {
        puppet_hash_set(context->config->cache->data.hash, key, strlen(key), puppet_value_copy(result));
    }

    return result;
}

/**
 * @brief Data provider lookup function
 */
puppet_value_t *puppet_hiera_data_provider_lookup(
    const char *key,
    puppet_env_t *env,
    void *provider_data
) {
    puppet_hiera_config_t *config = (puppet_hiera_config_t *)provider_data;
    if (!config) return NULL;
    
    puppet_hiera_context_t *context = puppet_hiera_context_create(config, env);
    puppet_value_t *result = puppet_hiera_lookup(context, key, NULL, config->default_merge);
    
    puppet_value_t *copy = result ? puppet_value_copy(result) : NULL;
    puppet_hiera_context_destroy(context);
    
    return copy;
}

/**
 * @brief Initialize Hiera data provider
 */
int puppet_hiera_data_provider_init(void **provider_data, const char *config) {
    if (!provider_data) return -1;
    
    puppet_hiera_config_t *hiera_config = NULL;
    
    if (config) {
        struct stat st;
        if (stat(config, &st) == 0 && S_ISREG(st.st_mode)) {
            hiera_config = puppet_hiera_config_create(config);
        } else {
            hiera_config = puppet_hiera_config_create_default(config);
        }
    } else {
        hiera_config = puppet_hiera_config_create_default("data");
    }
    
    if (!hiera_config) {
        return -1;
    }
    
    *provider_data = hiera_config;
    return 0;
}

/**
 * @brief Cleanup Hiera data provider
 */
void puppet_hiera_data_provider_cleanup(void *provider_data) {
    if (provider_data) {
        puppet_hiera_config_destroy((puppet_hiera_config_t *)provider_data);
    }
}

/**
 * @brief Register Hiera as a data provider
 */
int puppet_hiera_register_provider(puppet_env_t *env, const char *config_path) {
    if (!env) return -1;
    
    puppet_data_provider_t *provider = puppet_calloc(1, sizeof(puppet_data_provider_t));
    provider->name = puppet_strdup("hiera");
    provider->lookup = puppet_hiera_data_provider_lookup;
    provider->init = puppet_hiera_data_provider_init;
    provider->cleanup = puppet_hiera_data_provider_cleanup;
    
    void *provider_data = NULL;
    if (provider->init(&provider_data, config_path) != 0) {
        puppet_free(provider->name);
        puppet_free(provider);
        return -1;
    }
    
    provider->data = provider_data;
    
    // Register with environment
    return puppet_register_data_provider(env, provider);
}