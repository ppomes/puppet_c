/**
 * @file puppet_pluginsync.h
 * @brief Pluginsync client for puppetc-agent
 *
 * Downloads custom types, providers, and facts from the Puppet server's
 * module lib/ directories before catalog compilation.
 */

#ifndef PUPPET_PLUGINSYNC_H
#define PUPPET_PLUGINSYNC_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Pluginsync configuration
 */
typedef struct {
    const char *server_url;   /* Puppet server URL */
    const char *libdir;       /* Local plugin directory */
    const char *environment;  /* Environment name */
    bool verbose;             /* Verbose output */
} pluginsync_config_t;

/**
 * Pluginsync result statistics
 */
typedef struct {
    int files_checked;        /* Total files in metadata */
    int files_downloaded;     /* Files that were updated */
    int files_skipped;        /* Files already up to date */
    int errors;               /* Download errors */
} pluginsync_result_t;

/**
 * Run pluginsync to download plugin files from server
 *
 * @param config Pluginsync configuration
 * @param result Result statistics (optional, can be NULL)
 * @return 0 on success, -1 on failure
 */
int pluginsync_run(const pluginsync_config_t *config, pluginsync_result_t *result);

/**
 * Check if a file exists locally with matching checksum
 *
 * @param path Local file path
 * @param expected_md5 Expected MD5 checksum (hex string)
 * @return true if file exists and matches, false otherwise
 */
bool pluginsync_file_matches(const char *path, const char *expected_md5);

/**
 * Create necessary directories for a file path
 *
 * @param path File path (directories will be created for parent)
 * @return 0 on success, -1 on failure
 */
int pluginsync_mkdir_p(const char *path);

#endif /* PUPPET_PLUGINSYNC_H */
