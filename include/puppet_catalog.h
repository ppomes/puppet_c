/**
 * @file puppet_catalog.h
 * @brief Puppet resource catalog data structures and serialization
 *
 * Defines the catalog format for storing compiled Puppet resources.
 * Compatible with Puppet's catalog wire format for interoperability.
 */

#ifndef PUPPET_CATALOG_H
#define PUPPET_CATALOG_H

#include "puppet_ast.h"
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

/*
 * ===========================================================================
 * CATALOG DATA STRUCTURES
 * ===========================================================================
 */

/**
 * @brief Resource parameter (attribute)
 */
typedef struct puppet_catalog_param {
    char *name;                     /**< Parameter name */
    puppet_value_t *value;          /**< Parameter value */
} puppet_catalog_param_t;

/**
 * @brief Catalog resource entry
 */
typedef struct puppet_catalog_resource {
    char *type;                     /**< Resource type (e.g., "File", "Package") */
    char *title;                    /**< Resource title (unique within type) */
    puppet_catalog_param_t *parameters;  /**< Resource parameters/attributes */
    size_t param_count;             /**< Number of parameters */
    char **tags;                    /**< Resource tags */
    size_t tag_count;               /**< Number of tags */
    char *file;                     /**< Source file (optional) */
    int line;                       /**< Source line (optional) */
    bool exported;                  /**< Is this an exported resource? */
} puppet_catalog_resource_t;

/**
 * @brief Edge relationship types
 */
typedef enum {
    PUPPET_REL_BEFORE,              /**< Source must be applied before target */
    PUPPET_REL_REQUIRE,             /**< Source requires target (reversed before) */
    PUPPET_REL_NOTIFY,              /**< Source notifies target on change */
    PUPPET_REL_SUBSCRIBE            /**< Source subscribes to target changes */
} puppet_relationship_t;

/**
 * @brief Resource reference for edges
 */
typedef struct puppet_resource_ref {
    char *type;                     /**< Resource type */
    char *title;                    /**< Resource title */
} puppet_resource_ref_t;

/**
 * @brief Catalog edge (resource relationship)
 */
typedef struct puppet_catalog_edge {
    puppet_resource_ref_t source;   /**< Source resource */
    puppet_resource_ref_t target;   /**< Target resource */
    puppet_relationship_t relationship;  /**< Relationship type */
} puppet_catalog_edge_t;

/**
 * @brief Complete resource catalog
 */
typedef struct puppet_catalog {
    /* Metadata */
    char *certname;                 /**< Node certificate name */
    char *environment;              /**< Puppet environment */
    char *version;                  /**< Catalog version (timestamp) */
    time_t timestamp;               /**< Compilation timestamp */

    /* Resources */
    puppet_catalog_resource_t *resources;  /**< Array of resources */
    size_t resource_count;          /**< Number of resources */
    size_t resource_capacity;       /**< Resource array capacity */

    /* Edges (relationships) */
    puppet_catalog_edge_t *edges;   /**< Array of edges */
    size_t edge_count;              /**< Number of edges */
    size_t edge_capacity;           /**< Edge array capacity */

    /* Classes included */
    char **classes;                 /**< Array of included class names */
    size_t class_count;             /**< Number of classes */
    size_t class_capacity;          /**< Class array capacity */

    /* Tags */
    char **tags;                    /**< Catalog-level tags */
    size_t tag_count;               /**< Number of tags */
} puppet_catalog_t;

/*
 * ===========================================================================
 * CATALOG MANAGEMENT API
 * ===========================================================================
 */

/**
 * @brief Create a new empty catalog
 * @param certname Node certificate name
 * @param environment Puppet environment (or NULL for "production")
 * @return New catalog or NULL on failure
 */
puppet_catalog_t *puppet_catalog_create(const char *certname, const char *environment);

/**
 * @brief Destroy a catalog and free all resources
 * @param catalog Catalog to destroy
 */
void puppet_catalog_destroy(puppet_catalog_t *catalog);

/**
 * @brief Add a resource to the catalog
 * @param catalog Target catalog
 * @param type Resource type
 * @param title Resource title
 * @param params Array of parameters (ownership transferred)
 * @param param_count Number of parameters
 * @param file Source file (may be NULL)
 * @param line Source line number
 * @return 0 on success, -1 on duplicate, -2 on error
 */
int puppet_catalog_add_resource(puppet_catalog_t *catalog,
                                const char *type,
                                const char *title,
                                puppet_catalog_param_t *params,
                                size_t param_count,
                                const char *file,
                                int line);

/**
 * @brief Update an existing resource's parameters in the catalog
 * @param catalog Target catalog
 * @param type Resource type
 * @param title Resource title
 * @param params New parameters (ownership transferred)
 * @param param_count Number of parameters
 * @return 0 on success, -1 if not found, -2 on error
 */
int puppet_catalog_update_resource(puppet_catalog_t *catalog,
                                   const char *type,
                                   const char *title,
                                   puppet_catalog_param_t *params,
                                   size_t param_count);

/**
 * @brief Add an edge (relationship) to the catalog
 * @param catalog Target catalog
 * @param source_type Source resource type
 * @param source_title Source resource title
 * @param target_type Target resource type
 * @param target_title Target resource title
 * @param relationship Relationship type
 * @return 0 on success, -1 on error
 */
int puppet_catalog_add_edge(puppet_catalog_t *catalog,
                            const char *source_type,
                            const char *source_title,
                            const char *target_type,
                            const char *target_title,
                            puppet_relationship_t relationship);

/**
 * @brief Add a class to the catalog's class list
 * @param catalog Target catalog
 * @param class_name Class name
 * @return 0 on success, -1 on error
 */
int puppet_catalog_add_class(puppet_catalog_t *catalog, const char *class_name);

/**
 * @brief Find a resource in the catalog
 * @param catalog Catalog to search
 * @param type Resource type
 * @param title Resource title
 * @return Resource pointer or NULL if not found
 */
puppet_catalog_resource_t *puppet_catalog_find_resource(puppet_catalog_t *catalog,
                                                        const char *type,
                                                        const char *title);

/*
 * ===========================================================================
 * CATALOG SERIALIZATION
 * ===========================================================================
 */

/**
 * @brief Serialize catalog to JSON string
 * @param catalog Catalog to serialize
 * @return JSON string (caller must free) or NULL on error
 */
char *puppet_catalog_to_json(puppet_catalog_t *catalog);

/**
 * @brief Write catalog to a file as JSON
 * @param catalog Catalog to write
 * @param filepath Output file path
 * @return 0 on success, -1 on error
 */
int puppet_catalog_write_json(puppet_catalog_t *catalog, const char *filepath);

/**
 * @brief Get relationship type as string
 * @param rel Relationship type
 * @return String representation
 */
const char *puppet_relationship_to_string(puppet_relationship_t rel);

/**
 * @brief Pretty-print catalog to output stream (language-puppet style)
 * @param catalog Catalog to print
 * @param out Output stream (e.g., stdout)
 * @param use_color Whether to use ANSI color codes
 */
void puppet_catalog_pretty_print(puppet_catalog_t *catalog, FILE *out, bool use_color);

#endif /* PUPPET_CATALOG_H */
