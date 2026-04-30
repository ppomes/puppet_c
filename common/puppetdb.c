/**
 * @file puppetdb.c
 * @brief SQLite-based PuppetDB implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include "puppetdb.h"   /* Now in include/ */
#include "puppet_memory.h"

struct puppetdb {
    sqlite3 *db;
    char error_msg[512];
};

/* SQL schema for PuppetDB tables */
static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS nodes ("
    "    id INTEGER PRIMARY KEY,"
    "    certname TEXT UNIQUE NOT NULL,"
    "    last_seen INTEGER,"
    "    deactivated INTEGER"
    ");"
    "CREATE TABLE IF NOT EXISTS facts ("
    "    node_id INTEGER PRIMARY KEY,"
    "    facts TEXT NOT NULL,"
    "    updated_at INTEGER,"
    "    FOREIGN KEY (node_id) REFERENCES nodes(id)"
    ");"
    "CREATE TABLE IF NOT EXISTS catalogs ("
    "    node_id INTEGER PRIMARY KEY,"
    "    catalog TEXT NOT NULL,"
    "    compiled_at INTEGER,"
    "    FOREIGN KEY (node_id) REFERENCES nodes(id)"
    ");"
    "CREATE TABLE IF NOT EXISTS exported_resources ("
    "    id INTEGER PRIMARY KEY,"
    "    node_id INTEGER NOT NULL,"
    "    resource_type TEXT NOT NULL,"
    "    title TEXT NOT NULL,"
    "    parameters TEXT,"
    "    tags TEXT,"
    "    UNIQUE(resource_type, title),"
    "    FOREIGN KEY (node_id) REFERENCES nodes(id)"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_exported_type ON exported_resources(resource_type);";

/**
 * Get or create a node ID for the given certname
 */
static int get_or_create_node(puppetdb_t *pdb, const char *certname) {
    sqlite3_stmt *stmt;
    int node_id = -1;
    int rc;

    /* Try to find existing node */
    const char *select_sql = "SELECT id FROM nodes WHERE certname = ?";
    rc = sqlite3_prepare_v2(pdb->db, select_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(pdb->error_msg, sizeof(pdb->error_msg),
                 "Failed to prepare select: %s", sqlite3_errmsg(pdb->db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, certname, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        node_id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (node_id >= 0) {
        /* Update last_seen */
        const char *update_sql = "UPDATE nodes SET last_seen = ?, deactivated = NULL WHERE id = ?";
        rc = sqlite3_prepare_v2(pdb->db, update_sql, -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, time(NULL));
            sqlite3_bind_int(stmt, 2, node_id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return node_id;
    }

    /* Create new node */
    const char *insert_sql = "INSERT INTO nodes (certname, last_seen) VALUES (?, ?)";
    rc = sqlite3_prepare_v2(pdb->db, insert_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(pdb->error_msg, sizeof(pdb->error_msg),
                 "Failed to prepare insert: %s", sqlite3_errmsg(pdb->db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, certname, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, time(NULL));
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        snprintf(pdb->error_msg, sizeof(pdb->error_msg),
                 "Failed to insert node: %s", sqlite3_errmsg(pdb->db));
        return -1;
    }

    return (int)sqlite3_last_insert_rowid(pdb->db);
}

puppetdb_t *puppetdb_open(const char *path) {
    puppetdb_t *pdb = puppet_calloc(1, sizeof(puppetdb_t));
    if (!pdb) return NULL;

    int rc = sqlite3_open(path, &pdb->db);
    if (rc != SQLITE_OK) {
        snprintf(pdb->error_msg, sizeof(pdb->error_msg),
                 "Cannot open database: %s", sqlite3_errmsg(pdb->db));
        sqlite3_close(pdb->db);
        puppet_free(pdb);
        return NULL;
    }

    /* Enable foreign keys */
    sqlite3_exec(pdb->db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);

    /* Create schema */
    char *errmsg = NULL;
    rc = sqlite3_exec(pdb->db, SCHEMA_SQL, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        snprintf(pdb->error_msg, sizeof(pdb->error_msg),
                 "Schema creation failed: %s", errmsg);
        sqlite3_free(errmsg);
        sqlite3_close(pdb->db);
        puppet_free(pdb);
        return NULL;
    }

    return pdb;
}

void puppetdb_close(puppetdb_t *db) {
    if (db) {
        if (db->db) {
            sqlite3_close(db->db);
        }
        puppet_free(db);
    }
}

int puppetdb_store_facts(puppetdb_t *db, const char *certname, const char *facts_json) {
    if (!db || !certname || !facts_json) return -1;

    int node_id = get_or_create_node(db, certname);
    if (node_id < 0) return -1;

    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR REPLACE INTO facts (node_id, facts, updated_at) VALUES (?, ?, ?)";
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(db->error_msg, sizeof(db->error_msg),
                 "Failed to prepare facts insert: %s", sqlite3_errmsg(db->db));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, node_id);
    sqlite3_bind_text(stmt, 2, facts_json, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, time(NULL));
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        snprintf(db->error_msg, sizeof(db->error_msg),
                 "Failed to store facts: %s", sqlite3_errmsg(db->db));
        return -1;
    }

    return 0;
}

char *puppetdb_get_facts(puppetdb_t *db, const char *certname) {
    if (!db || !certname) return NULL;

    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT f.facts FROM facts f "
        "JOIN nodes n ON n.id = f.node_id "
        "WHERE n.certname = ? AND n.deactivated IS NULL";

    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(db->error_msg, sizeof(db->error_msg),
                 "Failed to prepare facts select: %s", sqlite3_errmsg(db->db));
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, certname, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);

    char *result = NULL;
    if (rc == SQLITE_ROW) {
        const char *facts = (const char *)sqlite3_column_text(stmt, 0);
        if (facts) {
            result = puppet_strdup(facts);
        }
    }
    sqlite3_finalize(stmt);

    return result;
}

int puppetdb_store_catalog(puppetdb_t *db, const char *certname, const char *catalog_json) {
    if (!db || !certname || !catalog_json) return -1;

    int node_id = get_or_create_node(db, certname);
    if (node_id < 0) return -1;

    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR REPLACE INTO catalogs (node_id, catalog, compiled_at) VALUES (?, ?, ?)";
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(db->error_msg, sizeof(db->error_msg),
                 "Failed to prepare catalog insert: %s", sqlite3_errmsg(db->db));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, node_id);
    sqlite3_bind_text(stmt, 2, catalog_json, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, time(NULL));
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        snprintf(db->error_msg, sizeof(db->error_msg),
                 "Failed to store catalog: %s", sqlite3_errmsg(db->db));
        return -1;
    }

    return 0;
}

char *puppetdb_get_catalog(puppetdb_t *db, const char *certname) {
    if (!db || !certname) return NULL;

    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT c.catalog FROM catalogs c "
        "JOIN nodes n ON n.id = c.node_id "
        "WHERE n.certname = ? AND n.deactivated IS NULL";

    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(db->error_msg, sizeof(db->error_msg),
                 "Failed to prepare catalog select: %s", sqlite3_errmsg(db->db));
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, certname, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);

    char *result = NULL;
    if (rc == SQLITE_ROW) {
        const char *catalog = (const char *)sqlite3_column_text(stmt, 0);
        if (catalog) {
            result = puppet_strdup(catalog);
        }
    }
    sqlite3_finalize(stmt);

    return result;
}

int puppetdb_store_exported(puppetdb_t *db, const char *certname,
                            const char *type, const char *title,
                            const char *params_json, const char *tags_json) {
    if (!db || !certname || !type || !title) return -1;

    int node_id = get_or_create_node(db, certname);
    if (node_id < 0) return -1;

    sqlite3_stmt *stmt;
    const char *sql =
        "INSERT OR REPLACE INTO exported_resources "
        "(node_id, resource_type, title, parameters, tags) "
        "VALUES (?, ?, ?, ?, ?)";

    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(db->error_msg, sizeof(db->error_msg),
                 "Failed to prepare exported insert: %s", sqlite3_errmsg(db->db));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, node_id);
    sqlite3_bind_text(stmt, 2, type, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, title, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, params_json, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, tags_json, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        snprintf(db->error_msg, sizeof(db->error_msg),
                 "Failed to store exported resource: %s", sqlite3_errmsg(db->db));
        return -1;
    }

    return 0;
}

char *puppetdb_query_exported(puppetdb_t *db, const char *type) {
    if (!db || !type) return NULL;

    sqlite3_stmt *stmt;
    /* Use COLLATE NOCASE for case-insensitive type matching (Host vs host) */
    const char *sql =
        "SELECT e.resource_type, e.title, e.parameters, e.tags, n.certname "
        "FROM exported_resources e "
        "JOIN nodes n ON n.id = e.node_id "
        "WHERE e.resource_type = ? COLLATE NOCASE AND n.deactivated IS NULL";

    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(db->error_msg, sizeof(db->error_msg),
                 "Failed to prepare exported query: %s", sqlite3_errmsg(db->db));
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, type, -1, SQLITE_STATIC);

    /* Build JSON array of results */
    size_t buf_size = 4096;
    size_t buf_len = 0;
    char *buf = puppet_malloc(buf_size);
    if (!buf) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    buf_len = snprintf(buf, buf_size, "[");
    int first = 1;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *res_type = (const char *)sqlite3_column_text(stmt, 0);
        const char *title = (const char *)sqlite3_column_text(stmt, 1);
        const char *params = (const char *)sqlite3_column_text(stmt, 2);
        const char *tags = (const char *)sqlite3_column_text(stmt, 3);
        const char *certname = (const char *)sqlite3_column_text(stmt, 4);

        /* Estimate size needed */
        size_t needed = 256 +
            (res_type ? strlen(res_type) : 0) +
            (title ? strlen(title) : 0) +
            (params ? strlen(params) : 0) +
            (tags ? strlen(tags) : 0) +
            (certname ? strlen(certname) : 0);

        while (buf_len + needed >= buf_size) {
            buf_size *= 2;
            char *new_buf = puppet_realloc(buf, buf_size);
            if (!new_buf) {
                puppet_free(buf);
                sqlite3_finalize(stmt);
                return NULL;
            }
            buf = new_buf;
        }

        /* Capture snprintf's return separately and verify it fit before
         * advancing buf_len. snprintf can return a value greater than
         * the size argument when the output was truncated, so adding
         * it directly back to buf_len would let the next write run
         * past the buffer. CodeQL: cpp/overflowing-snprintf. */
        int written = snprintf(buf + buf_len, buf_size - buf_len,
            "%s{\"type\":\"%s\",\"title\":\"%s\",\"parameters\":%s,\"tags\":%s,\"certname\":\"%s\"}",
            first ? "" : ",",
            res_type ? res_type : "",
            title ? title : "",
            params ? params : "{}",
            tags ? tags : "[]",
            certname ? certname : "");
        if (written < 0 || (size_t)written >= buf_size - buf_len) {
            /* Should not happen — the realloc loop above adds `needed`
             * bytes of slack, more than any single resource entry can
             * possibly need. Bail out rather than corrupt the buffer. */
            puppet_free(buf);
            sqlite3_finalize(stmt);
            return NULL;
        }
        buf_len += (size_t)written;
        first = 0;
    }
    sqlite3_finalize(stmt);

    /* Close array */
    if (buf_len + 2 >= buf_size) {
        buf = puppet_realloc(buf, buf_size + 2);
    }
    strcat(buf, "]");

    return buf;
}

char *puppetdb_list_nodes(puppetdb_t *db) {
    if (!db) return NULL;

    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT certname, last_seen, deactivated FROM nodes ORDER BY certname";

    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(db->error_msg, sizeof(db->error_msg),
                 "Failed to prepare nodes query: %s", sqlite3_errmsg(db->db));
        return NULL;
    }

    /* Build JSON array of results */
    size_t buf_size = 4096;
    size_t buf_len = 0;
    char *buf = puppet_malloc(buf_size);
    if (!buf) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    buf_len = snprintf(buf, buf_size, "[");
    int first = 1;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *certname = (const char *)sqlite3_column_text(stmt, 0);
        int64_t last_seen = sqlite3_column_int64(stmt, 1);
        int deactivated = sqlite3_column_type(stmt, 2) != SQLITE_NULL;

        while (buf_len + 256 >= buf_size) {
            buf_size *= 2;
            char *new_buf = puppet_realloc(buf, buf_size);
            if (!new_buf) {
                puppet_free(buf);
                sqlite3_finalize(stmt);
                return NULL;
            }
            buf = new_buf;
        }

        buf_len += snprintf(buf + buf_len, buf_size - buf_len,
            "%s{\"certname\":\"%s\",\"last_seen\":%lld,\"deactivated\":%s}",
            first ? "" : ",",
            certname ? certname : "",
            (long long)last_seen,
            deactivated ? "true" : "false");
        first = 0;
    }
    sqlite3_finalize(stmt);

    /* Close array */
    if (buf_len + 2 >= buf_size) {
        buf = puppet_realloc(buf, buf_size + 2);
    }
    strcat(buf, "]");

    return buf;
}

int puppetdb_deactivate_node(puppetdb_t *db, const char *certname) {
    if (!db || !certname) return -1;

    sqlite3_stmt *stmt;
    const char *sql = "UPDATE nodes SET deactivated = ? WHERE certname = ?";
    int rc = sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(db->error_msg, sizeof(db->error_msg),
                 "Failed to prepare deactivate: %s", sqlite3_errmsg(db->db));
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, time(NULL));
    sqlite3_bind_text(stmt, 2, certname, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        snprintf(db->error_msg, sizeof(db->error_msg),
                 "Failed to deactivate node: %s", sqlite3_errmsg(db->db));
        return -1;
    }

    return 0;
}

const char *puppetdb_error(puppetdb_t *db) {
    if (!db) return "NULL database handle";
    return db->error_msg;
}
