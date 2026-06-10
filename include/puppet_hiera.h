/**
 * @file puppet_hiera.h
 * @brief Hiera data lookup implementation for Puppet C
 *
 * Provides hierarchical data lookup functionality compatible with Puppet's Hiera.
 * Supports YAML data sources, multiple lookup strategies, and merge behaviors.
 */

#ifndef PUPPET_HIERA_H
#define PUPPET_HIERA_H

#include "puppet_ast.h"
#include "puppet_interpreter.h"

#ifdef HAVE_YAML
#include <yaml.h>
#endif

/**
 * @brief Hiera merge strategies
 */
typedef enum {
    HIERA_MERGE_FIRST,    /**< Return first value found (default) */
    HIERA_MERGE_UNIQUE,   /**< Array merge, unique values only */
    HIERA_MERGE_HASH,     /**< Hash merge, top-level keys */
    HIERA_MERGE_DEEP      /**< Deep hash merge, recursive */
} puppet_hiera_merge_t;

/**
 * @brief Hiera backend types
 */
typedef enum {
    HIERA_BACKEND_YAML,   /**< YAML file backend */
    HIERA_BACKEND_JSON,   /**< JSON file backend (future) */
    HIERA_BACKEND_EYAML   /**< Encrypted YAML (future) */
} puppet_hiera_backend_t;

/**
 * @brief Hiera hierarchy level
 */
typedef struct puppet_hiera_level {
    char *name;                          /**< Level name/description */
    char *path_template;                 /**< Path template with interpolation */
    char *datadir;                       /**< Data directory for this level */
    puppet_hiera_backend_t backend;      /**< Backend type */
    struct puppet_hiera_level *next;     /**< Next level in hierarchy */
} puppet_hiera_level_t;

/**
 * @brief Hiera configuration
 */
typedef struct puppet_hiera_config {
    int version;                         /**< Hiera version (5) */
    puppet_hiera_merge_t default_merge;  /**< Default merge strategy */
    char *datadir;                       /**< Default data directory */
    puppet_hiera_level_t *hierarchy;     /**< Hierarchy levels */
    puppet_value_t *defaults;            /**< Default values */
    puppet_value_t *cache;               /**< Cached lookups */
} puppet_hiera_config_t;

/**
 * @brief Hiera context for a lookup
 */
typedef struct puppet_hiera_context {
    puppet_hiera_config_t *config;       /**< Hiera configuration */
    puppet_env_t *env;                   /**< Current environment */
    puppet_value_t *variables;           /**< Variables for interpolation */
    char *environment;                   /**< Current environment name */
    char *node;                          /**< Current node name */
    puppet_value_t *facts;               /**< Current facts */
} puppet_hiera_context_t;

/**
 * @brief Create a new Hiera configuration
 * @param config_path Path to hiera.yaml configuration file
 * @return New Hiera configuration or NULL on error
 */
puppet_hiera_config_t *puppet_hiera_config_create(const char *config_path);

/**
 * @brief Create default Hiera configuration
 * @param datadir Default data directory
 * @return New Hiera configuration with default hierarchy
 */
puppet_hiera_config_t *puppet_hiera_config_create_default(const char *datadir);

/**
 * @brief Destroy Hiera configuration
 * @param config Configuration to destroy
 */
void puppet_hiera_config_destroy(puppet_hiera_config_t *config);

/**
 * @brief Create Hiera lookup context
 * @param config Hiera configuration
 * @param env Current environment
 * @return New Hiera context
 */
puppet_hiera_context_t *puppet_hiera_context_create(
    puppet_hiera_config_t *config,
    puppet_env_t *env
);

/**
 * @brief Destroy Hiera context
 * @param context Context to destroy
 */
void puppet_hiera_context_destroy(puppet_hiera_context_t *context);

/**
 * @brief Perform Hiera lookup
 * @param context Hiera context
 * @param key Key to look up
 * @param default_value Default value if not found
 * @param merge_strategy Merge strategy to use
 * @return Value found or default_value
 */
puppet_value_t *puppet_hiera_lookup(
    puppet_hiera_context_t *context,
    const char *key,
    puppet_value_t *default_value,
    puppet_hiera_merge_t merge_strategy
);

/**
 * @brief Data provider interface for Hiera
 * @param key Variable name to look up
 * @param env Current environment
 * @param provider_data Hiera configuration
 * @return Value found or NULL
 */
puppet_value_t *puppet_hiera_data_provider_lookup(
    const char *key,
    puppet_env_t *env,
    void *provider_data
);

/**
 * @brief Initialize Hiera data provider
 * @param provider_data Output pointer for provider data
 * @param config Configuration string (path to hiera.yaml or datadir)
 * @return 0 on success, -1 on error
 */
int puppet_hiera_data_provider_init(void **provider_data, const char *config);

/**
 * @brief Cleanup Hiera data provider
 * @param provider_data Provider data to cleanup
 */
void puppet_hiera_data_provider_cleanup(void *provider_data);

/**
 * @brief Register Hiera as a data provider
 * @param env Environment to register with
 * @param config_path Path to hiera.yaml or NULL for defaults
 * @return 0 on success, -1 on error
 */
int puppet_hiera_register_provider(puppet_env_t *env, const char *config_path);

/**
 * @brief Load YAML file into Puppet value
 * @param filepath Path to YAML file
 * @return Puppet value representation of YAML data
 */
puppet_value_t *puppet_hiera_load_yaml(const char *filepath);

/**
 * @brief Interpolate string with Hiera context variables
 * @param template Template string with %{var} placeholders
 * @param context Hiera context for variable lookup
 * @return Interpolated string (must be freed)
 */
char *puppet_hiera_interpolate(const char *template, puppet_hiera_context_t *context);

/**
 * @brief Merge two values according to strategy
 * @param existing Existing value
 * @param new New value to merge
 * @param strategy Merge strategy
 * @return Merged value (may be existing, new, or newly allocated)
 */
puppet_value_t *puppet_hiera_merge_values(
    puppet_value_t *existing,
    puppet_value_t *new,
    puppet_hiera_merge_t strategy
);


/* ============================================================================
 * Item 33 — module-layer hiera (data in modules)
 * ============================================================================
 * Parsed view of a module's v5 hiera.yaml: just the ordered, fully-prefixed
 * path templates (<modules_path>/<module>/<datadir>/<path>). Only
 * `data_hash: yaml_data` levels are kept; other backends are skipped with a
 * warning at parse time.
 */
typedef struct puppet_module_hiera {
    char **path_templates;
    size_t path_count;
} puppet_module_hiera_t;

/**
 * @brief Look up "<class>::<param>" in a module's own data layer.
 *
 * Parses modules/<module>/hiera.yaml (v5) once (cached on the loader),
 * interpolates each hierarchy path with the current node's facts
 * (%{facts.x.y}), loads the YAML data files (cached per interpolated path)
 * and returns an owned copy of the first hit, or NULL.
 */
puppet_value_t *puppet_hiera_module_lookup(puppet_env_t *env,
                                           const char *module_name,
                                           const char *key);

/** @brief Free a parsed module hiera config (NULL-safe). */
void puppet_hiera_module_config_destroy(puppet_module_hiera_t *cfg);

#endif /* PUPPET_HIERA_H */