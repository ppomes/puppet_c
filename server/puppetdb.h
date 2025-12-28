/**
 * @file puppetdb.h
 * @brief PuppetDB interface for storing facts, catalogs, and exported resources
 *
 * This module provides SQLite-based storage for PuppetDB data including
 * node facts, compiled catalogs, and exported resources.
 */

#ifndef PUPPETDB_H
#define PUPPETDB_H

#include <stdbool.h>
#include <stddef.h>

/* Opaque handle to PuppetDB connection */
typedef struct puppetdb puppetdb_t;

/**
 * Open or create a PuppetDB database
 * @param path Path to the SQLite database file
 * @return Database handle, or NULL on failure
 */
puppetdb_t *puppetdb_open(const char *path);

/**
 * Close PuppetDB connection
 * @param db Database handle
 */
void puppetdb_close(puppetdb_t *db);

/**
 * Store facts for a node (creates or updates node record)
 * @param db Database handle
 * @param certname Node certificate name
 * @param facts_json Facts as JSON string
 * @return 0 on success, -1 on failure
 */
int puppetdb_store_facts(puppetdb_t *db, const char *certname, const char *facts_json);

/**
 * Get facts for a node
 * @param db Database handle
 * @param certname Node certificate name
 * @return Facts as JSON string (caller must free), or NULL if not found
 */
char *puppetdb_get_facts(puppetdb_t *db, const char *certname);

/**
 * Store catalog for a node
 * @param db Database handle
 * @param certname Node certificate name
 * @param catalog_json Catalog as JSON string
 * @return 0 on success, -1 on failure
 */
int puppetdb_store_catalog(puppetdb_t *db, const char *certname, const char *catalog_json);

/**
 * Get catalog for a node
 * @param db Database handle
 * @param certname Node certificate name
 * @return Catalog as JSON string (caller must free), or NULL if not found
 */
char *puppetdb_get_catalog(puppetdb_t *db, const char *certname);

/**
 * Store an exported resource
 * @param db Database handle
 * @param certname Node that exports the resource
 * @param type Resource type (e.g., "File", "User")
 * @param title Resource title
 * @param params_json Resource parameters as JSON
 * @param tags_json Resource tags as JSON array
 * @return 0 on success, -1 on failure
 */
int puppetdb_store_exported(puppetdb_t *db, const char *certname,
                            const char *type, const char *title,
                            const char *params_json, const char *tags_json);

/**
 * Query exported resources by type
 * @param db Database handle
 * @param type Resource type to query (e.g., "File")
 * @return JSON array of matching resources (caller must free), or NULL on error
 */
char *puppetdb_query_exported(puppetdb_t *db, const char *type);

/**
 * List all nodes
 * @param db Database handle
 * @return JSON array of node objects (caller must free), or NULL on error
 */
char *puppetdb_list_nodes(puppetdb_t *db);

/**
 * Deactivate a node (mark as no longer active)
 * @param db Database handle
 * @param certname Node certificate name
 * @return 0 on success, -1 on failure
 */
int puppetdb_deactivate_node(puppetdb_t *db, const char *certname);

/**
 * Get last error message
 * @param db Database handle
 * @return Error message string (valid until next DB operation)
 */
const char *puppetdb_error(puppetdb_t *db);

#endif /* PUPPETDB_H */
