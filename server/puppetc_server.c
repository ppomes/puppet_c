/**
 * @file puppetc_server.c
 * @brief HTTPS server for Puppet catalog compilation with TLS support
 *
 * Provides a REST API for compiling Puppet catalogs:
 * - GET  /status           - Health check
 * - POST /puppet/v4/catalog - Compile catalog for a node
 * - GET  /puppet/v3/file_content/* - File server
 * - GET  /puppet/v3/file_metadatas/plugins - Plugin metadata
 * - POST /puppet-ca/v1/certificate_request/:certname - Submit CSR for signing
 * - GET  /pdb/query/v4/* - PuppetDB queries
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/md5.h>

#include <microhttpd.h>

#include "puppet_ast.h"
#include "puppet_catalog.h"
#include "puppet_interpreter.h"
#include "puppet_loader.h"
#include "puppet_memory.h"
#include "puppet_json_parser.h"
#include "puppet_hiera.h"
#include "config_parser.h"
#include "puppetdb.h"
#include "puppet_ssl.h"
#include "puppet_ca.h"
#include "puppet_autosign.h"

/* Default configuration */
#define DEFAULT_PORT 8140
#define DEFAULT_ENVIRONMENT "production"
#define DEFAULT_CONFIG_FILE "/etc/puppetc/puppet.conf"
#define DEFAULT_CA_DIR "/etc/puppetc/ssl/ca"

/* Default PuppetDB path */
#define DEFAULT_PUPPETDB_PATH "/var/lib/puppetc/puppetdb.sqlite"

/* Global state */
static volatile int running = 1;
static char *manifest_path = NULL;
static char *modules_path = NULL;
static char *hiera_datadir = NULL;
static char *puppetdb_path = NULL;
static char *ca_dir = NULL;
static puppetdb_t *pdb = NULL;
static puppet_ssl_ctx_t *ssl_ctx = NULL;
static puppet_ca_ctx_t *ca_ctx = NULL;
static puppet_autosign_config_t *autosign_config = NULL;
static int verbose = 0;
static char *server_cert_pem = NULL;  /* Server certificate for TLS */
static char *server_key_pem = NULL;   /* Server private key for TLS */
static char *ca_cert_pem = NULL;      /* CA certificate for mTLS client verification */

/**
 * @brief Signal handler for graceful shutdown
 */
static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/**
 * @brief Read file contents into a malloc'd string
 * @param path File path
 * @return Allocated string with file contents, or NULL on error
 */
static char *read_file_to_string(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = malloc(len + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }

    size_t read_len = fread(content, 1, len, f);
    fclose(f);

    content[read_len] = '\0';
    return content;
}

/**
 * @brief Load server certificate, key, and CA certificate from CA directory for TLS
 * @param ca_directory CA directory path
 * @return 0 on success, -1 on error
 */
static int load_server_tls_credentials(const char *ca_directory) {
    char cert_path[PUPPET_CA_MAX_PATH];
    char key_path[PUPPET_CA_MAX_PATH];
    char ca_path[PUPPET_CA_MAX_PATH];

    /* Load server certificate for TLS */
    snprintf(cert_path, sizeof(cert_path), "%s/server_crt.pem", ca_directory);
    snprintf(key_path, sizeof(key_path), "%s/server_key.pem", ca_directory);
    snprintf(ca_path, sizeof(ca_path), "%s/ca_crt.pem", ca_directory);

    server_cert_pem = read_file_to_string(cert_path);
    if (!server_cert_pem) {
        fprintf(stderr, "Error: Failed to load server certificate from %s\n", cert_path);
        return -1;
    }

    server_key_pem = read_file_to_string(key_path);
    if (!server_key_pem) {
        fprintf(stderr, "Error: Failed to load server key from %s\n", key_path);
        free(server_cert_pem);
        server_cert_pem = NULL;
        return -1;
    }

    /* Load CA certificate for mTLS client verification */
    ca_cert_pem = read_file_to_string(ca_path);
    if (!ca_cert_pem) {
        fprintf(stderr, "Error: Failed to load CA certificate from %s\n", ca_path);
        free(server_cert_pem);
        free(server_key_pem);
        server_cert_pem = NULL;
        server_key_pem = NULL;
        return -1;
    }

    return 0;
}

/**
 * @brief Send a JSON response
 */
static enum MHD_Result send_json_response(struct MHD_Connection *connection,
                                          unsigned int status_code,
                                          const char *json) {
    struct MHD_Response *response;
    enum MHD_Result ret;

    response = MHD_create_response_from_buffer(strlen(json),
                                                (void *)json,
                                                MHD_RESPMEM_MUST_COPY);
    if (!response) return MHD_NO;

    MHD_add_response_header(response, "Content-Type", "application/json");

    ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);

    return ret;
}

/**
 * @brief Send an error response
 */
static enum MHD_Result send_error(struct MHD_Connection *connection,
                                  unsigned int status_code,
                                  const char *message) {
    /* Escape message for safe JSON embedding */
    char escaped[1024];
    size_t j = 0;
    for (size_t i = 0; message[i] && j < sizeof(escaped) - 2; i++) {
        if (message[i] == '"' || message[i] == '\\') {
            if (j + 2 >= sizeof(escaped) - 1) break;
            escaped[j++] = '\\';
        }
        escaped[j++] = message[i];
    }
    escaped[j] = '\0';

    char json[1536];
    snprintf(json, sizeof(json),
             "{\"error\": \"%s\", \"status\": %u}", escaped, status_code);
    return send_json_response(connection, status_code, json);
}

/**
 * @brief Send raw file content response
 */
static enum MHD_Result send_file_response(struct MHD_Connection *connection,
                                          unsigned int status_code,
                                          const char *content,
                                          size_t content_len) {
    struct MHD_Response *response;
    enum MHD_Result ret;

    response = MHD_create_response_from_buffer(content_len,
                                                (void *)content,
                                                MHD_RESPMEM_MUST_COPY);
    if (!response) return MHD_NO;

    MHD_add_response_header(response, "Content-Type", "application/octet-stream");

    ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);

    return ret;
}

/**
 * @brief Compute MD5 checksum of a file
 * @return Hex string (caller must free) or NULL on error
 */
static char *file_md5(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    MD5_CTX ctx;
    MD5_Init(&ctx);

    unsigned char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        MD5_Update(&ctx, buf, n);
    }
    fclose(fp);

    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_Final(digest, &ctx);

    char *hex = puppet_malloc(MD5_DIGEST_LENGTH * 2 + 1);
    if (!hex) return NULL;

    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    return hex;
}

/**
 * @brief Recursively scan a directory for files
 * @param base_path The base path (module's lib/)
 * @param rel_path Relative path within lib/
 * @param json_buf Buffer to append JSON to
 * @param buf_size Buffer size
 * @param buf_pos Current position in buffer
 * @param first Pointer to first element flag
 */
static void scan_lib_directory(const char *base_path, const char *rel_path,
                               char *json_buf, size_t buf_size, size_t *buf_pos,
                               int *first) {
    char full_path[4096];
    if (rel_path && strlen(rel_path) > 0) {
        snprintf(full_path, sizeof(full_path), "%s/%s", base_path, rel_path);
    } else {
        snprintf(full_path, sizeof(full_path), "%s", base_path);
    }

    DIR *dir = opendir(full_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;  /* Skip . and .. and hidden */

        char entry_path[4096];
        snprintf(entry_path, sizeof(entry_path), "%s/%s", full_path, entry->d_name);

        char entry_rel[4096];
        if (rel_path && strlen(rel_path) > 0) {
            snprintf(entry_rel, sizeof(entry_rel), "%s/%s", rel_path, entry->d_name);
        } else {
            snprintf(entry_rel, sizeof(entry_rel), "%s", entry->d_name);
        }

        struct stat st;
        if (stat(entry_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            /* Recurse into subdirectory */
            scan_lib_directory(base_path, entry_rel, json_buf, buf_size, buf_pos, first);
        } else if (S_ISREG(st.st_mode)) {
            /* Add file entry */
            char *checksum = file_md5(entry_path);
            if (checksum) {
                size_t needed = snprintf(NULL, 0,
                    "%s{\"path\":\"%s\",\"checksum\":{\"type\":\"md5\",\"value\":\"{md5}%s\"},\"type\":\"file\"}",
                    *first ? "" : ",", entry_rel, checksum);
                if (*buf_pos + needed < buf_size - 1) {
                    *buf_pos += sprintf(json_buf + *buf_pos,
                        "%s{\"path\":\"%s\",\"checksum\":{\"type\":\"md5\",\"value\":\"{md5}%s\"},\"type\":\"file\"}",
                        *first ? "" : ",", entry_rel, checksum);
                    *first = 0;
                }
                puppet_free(checksum);
            }
        }
    }
    closedir(dir);
}

/**
 * @brief Compile a catalog for the given request
 */
static char *compile_catalog(const char *certname, const char *environment,
                             json_value_t *facts_json) {
    puppet_program_t *program = NULL;
    puppet_loader_t *loader = NULL;
    char *catalog_json = NULL;

    /* Create loader for manifest directory */
    if (manifest_path) {
        loader = puppet_loader_create(manifest_path);
        if (!loader) {
            return puppet_strdup("{\"error\": \"Failed to create module loader\"}");
        }

        if (modules_path) {
            puppet_loader_set_modules_path(loader, modules_path);
        }

        /* Load site.pp */
        program = puppet_loader_load_site(loader);
    }

    if (!program) {
        if (loader) puppet_loader_destroy(loader);
        return puppet_strdup("{\"error\": \"No manifest found\"}");
    }

    /* Create environment */
    puppet_env_t *env = puppet_env_create();
    puppet_env_set_verbose(env, verbose);

    if (loader) {
        puppet_env_set_loader(env, loader);
    }

    /* Set PuppetDB for exported resources */
    if (pdb) {
        puppet_env_set_puppetdb(env, pdb);
    }

    /* Configure Hiera */
    if (hiera_datadir) {
        puppet_hiera_register_provider(env, hiera_datadir);
    }

    /* Set node name */
    puppet_env_set_node(env, certname);

    /* Enable catalog building */
    puppet_env_enable_catalog(env, certname, environment ? environment : DEFAULT_ENVIRONMENT);

    /* Set facts if provided */
    if (facts_json && facts_json->type == JSON_VALUE_OBJECT) {
        puppet_facts_db_t *facts_db = puppet_facts_db_create();
        if (puppet_facts_db_load_json(facts_db, certname, facts_json) == 0) {
            puppet_env_set_facts_db(env, facts_db);
            if (verbose) {
                fprintf(stderr, "[INFO] Loaded facts for node: %s\n", certname);
            }
        } else {
            puppet_facts_db_destroy(facts_db);
            if (verbose) {
                fprintf(stderr, "[WARN] Failed to load facts for node: %s\n", certname);
            }
        }
    }

    /* Execute program */
    puppet_exec_program(program, env);

    /* Check for compilation failure */
    if (env->compilation_failed) {
        char error_json[1024];
        snprintf(error_json, sizeof(error_json),
                 "{\"error\": \"Catalog compilation failed: %s\"}",
                 env->failure_message ? env->failure_message : "unknown error");
        catalog_json = puppet_strdup(error_json);
    } else {
        /* Get compiled catalog */
        puppet_catalog_t *catalog = puppet_env_get_catalog(env);
        if (catalog) {
            catalog_json = puppet_catalog_to_json(catalog);
            puppet_catalog_destroy(catalog);
        } else {
            catalog_json = puppet_strdup("{\"error\": \"No catalog generated\"}");
        }
    }

    /* Cleanup */
    puppet_env_destroy(env);
    puppet_program_destroy(program);
    if (loader) puppet_loader_destroy(loader);

    return catalog_json;
}


/**
 * @brief Handle GET /status
 */
static enum MHD_Result handle_status(struct MHD_Connection *connection) {
    const char *json = "{\"status\": \"ok\", \"version\": \"0.0.1\"}";
    return send_json_response(connection, MHD_HTTP_OK, json);
}

/**
 * @brief Handle GET /puppet/v3/file_metadatas/plugins
 */
static enum MHD_Result handle_file_metadatas_plugins(struct MHD_Connection *connection) {
    if (!modules_path) {
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "Modules path not configured");
    }

    /* Allocate buffer for JSON response */
    size_t buf_size = 1024 * 1024;  /* 1MB max */
    char *json_buf = puppet_malloc(buf_size);
    if (!json_buf) {
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "Memory allocation failed");
    }

    size_t buf_pos = 0;
    buf_pos = sprintf(json_buf, "[");
    int first = 1;

    /* Scan each module's lib/ directory */
    DIR *modules_dir = opendir(modules_path);
    if (!modules_dir) {
        puppet_free(json_buf);
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "Cannot open modules directory");
    }

    struct dirent *module_entry;
    while ((module_entry = readdir(modules_dir)) != NULL) {
        if (module_entry->d_name[0] == '.') continue;

        char module_path[4096];
        snprintf(module_path, sizeof(module_path), "%s/%s", modules_path, module_entry->d_name);

        struct stat st;
        if (stat(module_path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        /* Check for lib/ directory */
        char lib_path[4096];
        snprintf(lib_path, sizeof(lib_path), "%s/lib", module_path);
        if (stat(lib_path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        if (verbose) {
            fprintf(stderr, "[INFO] Scanning module lib: %s\n", lib_path);
        }

        /* Scan lib/ directory - prefix paths with module name for download */
        DIR *lib_dir = opendir(lib_path);
        if (!lib_dir) continue;

        struct dirent *lib_entry;
        while ((lib_entry = readdir(lib_dir)) != NULL) {
            if (lib_entry->d_name[0] == '.') continue;

            char entry_path[4096];
            snprintf(entry_path, sizeof(entry_path), "%s/%s", lib_path, lib_entry->d_name);

            if (stat(entry_path, &st) != 0) continue;

            /* Build relative path for agent: module_name/lib_subpath */
            char rel_prefix[512];
            snprintf(rel_prefix, sizeof(rel_prefix), "%s", module_entry->d_name);

            if (S_ISDIR(st.st_mode)) {
                /* Recursively scan subdirectory */
                char sub_rel[4096];
                snprintf(sub_rel, sizeof(sub_rel), "%s/%s", rel_prefix, lib_entry->d_name);
                scan_lib_directory(lib_path, lib_entry->d_name, json_buf, buf_size, &buf_pos, &first);
            } else if (S_ISREG(st.st_mode)) {
                /* Add file entry */
                char *checksum = file_md5(entry_path);
                if (checksum) {
                    size_t needed = snprintf(NULL, 0,
                        "%s{\"path\":\"%s/lib/%s\",\"checksum\":{\"type\":\"md5\",\"value\":\"{md5}%s\"},\"type\":\"file\"}",
                        first ? "" : ",", module_entry->d_name, lib_entry->d_name, checksum);
                    if (buf_pos + needed < buf_size - 1) {
                        buf_pos += sprintf(json_buf + buf_pos,
                            "%s{\"path\":\"%s/lib/%s\",\"checksum\":{\"type\":\"md5\",\"value\":\"{md5}%s\"},\"type\":\"file\"}",
                            first ? "" : ",", module_entry->d_name, lib_entry->d_name, checksum);
                        first = 0;
                    }
                    puppet_free(checksum);
                }
            }
        }
        closedir(lib_dir);
    }
    closedir(modules_dir);

    buf_pos += sprintf(json_buf + buf_pos, "]");

    if (verbose) {
        fprintf(stderr, "[INFO] Returning %zu bytes of plugin metadata\n", buf_pos);
    }

    enum MHD_Result ret = send_json_response(connection, MHD_HTTP_OK, json_buf);
    puppet_free(json_buf);
    return ret;
}

/**
 * @brief Handle GET /puppet/v3/file_content/plugins/<path>
 */
static enum MHD_Result handle_plugin_content(struct MHD_Connection *connection,
                                              const char *url) {
    const char *prefix = "/puppet/v3/file_content/plugins/";
    size_t prefix_len = strlen(prefix);

    if (strncmp(url, prefix, prefix_len) != 0) {
        return send_error(connection, MHD_HTTP_BAD_REQUEST,
                         "Invalid plugin URL format");
    }

    const char *rel_path = url + prefix_len;
    if (strlen(rel_path) == 0) {
        return send_error(connection, MHD_HTTP_BAD_REQUEST,
                         "Missing file path");
    }

    /* Security: reject paths with .. */
    if (strstr(rel_path, "..") != NULL) {
        return send_error(connection, MHD_HTTP_FORBIDDEN,
                         "Path traversal not allowed");
    }

    if (!modules_path) {
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "Modules path not configured");
    }

    /* Build full path: <modules_path>/<module>/lib/<subpath> */
    /* URL path is: <module>/lib/<subpath> */
    char full_path[4096];
    snprintf(full_path, sizeof(full_path), "%s/%s", modules_path, rel_path);

    if (verbose) {
        fprintf(stderr, "[INFO] Plugin content request: %s -> %s\n", url, full_path);
    }

    /* Read file */
    FILE *fp = fopen(full_path, "rb");
    if (!fp) {
        if (verbose) {
            fprintf(stderr, "[WARN] Plugin file not found: %s\n", full_path);
        }
        return send_error(connection, MHD_HTTP_NOT_FOUND, "File not found");
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < 0 || file_size > 10 * 1024 * 1024) { /* 10MB limit for plugins */
        fclose(fp);
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "File too large");
    }

    char *content = puppet_malloc(file_size + 1);
    if (!content) {
        fclose(fp);
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "Memory allocation failed");
    }

    size_t bytes_read = fread(content, 1, file_size, fp);
    fclose(fp);

    if (bytes_read != (size_t)file_size) {
        puppet_free(content);
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "Failed to read file");
    }

    enum MHD_Result ret = send_file_response(connection, MHD_HTTP_OK,
                                              content, bytes_read);
    puppet_free(content);
    return ret;
}

/**
 * @brief Handle GET /puppet/v3/file_content/modules/<module>/<path>
 */
static enum MHD_Result handle_file_content(struct MHD_Connection *connection,
                                           const char *url) {
    /* URL format: /puppet/v3/file_content/modules/<module>/<path> */
    const char *prefix = "/puppet/v3/file_content/modules/";
    size_t prefix_len = strlen(prefix);

    if (strncmp(url, prefix, prefix_len) != 0) {
        return send_error(connection, MHD_HTTP_BAD_REQUEST,
                         "Invalid file_content URL format");
    }

    const char *module_and_path = url + prefix_len;

    /* Extract module name (up to first /) */
    const char *slash = strchr(module_and_path, '/');
    if (!slash || slash == module_and_path) {
        return send_error(connection, MHD_HTTP_BAD_REQUEST,
                         "Missing module name or file path");
    }

    size_t module_len = slash - module_and_path;
    char module_name[256];
    if (module_len >= sizeof(module_name)) {
        return send_error(connection, MHD_HTTP_BAD_REQUEST,
                         "Module name too long");
    }
    strncpy(module_name, module_and_path, module_len);
    module_name[module_len] = '\0';

    const char *file_path = slash + 1;
    if (strlen(file_path) == 0) {
        return send_error(connection, MHD_HTTP_BAD_REQUEST,
                         "Missing file path");
    }

    /* Security: reject paths with .. */
    if (strstr(file_path, "..") != NULL || strstr(module_name, "..") != NULL) {
        return send_error(connection, MHD_HTTP_FORBIDDEN,
                         "Path traversal not allowed");
    }

    /* Build full path: <modules_path>/<module>/files/<path> */
    if (!modules_path) {
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "Modules path not configured");
    }

    char full_path[4096];
    snprintf(full_path, sizeof(full_path), "%s/%s/files/%s",
             modules_path, module_name, file_path);

    if (verbose) {
        fprintf(stderr, "[INFO] Fileserver request: %s -> %s\n", url, full_path);
    }

    /* Read file */
    FILE *fp = fopen(full_path, "rb");
    if (!fp) {
        if (verbose) {
            fprintf(stderr, "[WARN] File not found: %s\n", full_path);
        }
        return send_error(connection, MHD_HTTP_NOT_FOUND,
                         "File not found");
    }

    /* Get file size */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < 0 || file_size > 100 * 1024 * 1024) { /* 100MB limit */
        fclose(fp);
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "File too large or invalid");
    }

    /* Read content */
    char *content = puppet_malloc(file_size + 1);
    if (!content) {
        fclose(fp);
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "Memory allocation failed");
    }

    size_t bytes_read = fread(content, 1, file_size, fp);
    fclose(fp);

    if (bytes_read != (size_t)file_size) {
        puppet_free(content);
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "Failed to read file");
    }

    enum MHD_Result ret = send_file_response(connection, MHD_HTTP_OK,
                                              content, bytes_read);
    puppet_free(content);

    return ret;
}

/**
 * @brief Connection info for POST data accumulation
 */
struct connection_info {
    char *post_data;
    size_t post_data_size;
};

/**
 * @brief Handle POST /puppet/v4/catalog
 */
static enum MHD_Result handle_catalog(struct MHD_Connection *connection,
                                      const char *post_data) {
    /* Parse request JSON */
    if (!post_data || strlen(post_data) == 0) {
        return send_error(connection, MHD_HTTP_BAD_REQUEST,
                         "Missing request body");
    }

    /* Parse JSON to extract certname, environment, facts */
    json_parser_t *parser = json_parser_create(post_data);
    if (!parser) {
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "Failed to create JSON parser");
    }

    json_value_t *request = json_parse_value(parser);
    json_parser_destroy(parser);

    if (!request || request->type != JSON_VALUE_OBJECT) {
        if (request) json_value_destroy(request);
        return send_error(connection, MHD_HTTP_BAD_REQUEST,
                         "Invalid JSON in request body");
    }

    /* Extract certname (required) */
    json_value_t *certname_json = json_object_get(request, "certname");
    if (!certname_json || certname_json->type != JSON_VALUE_STRING) {
        json_value_destroy(request);
        return send_error(connection, MHD_HTTP_BAD_REQUEST,
                         "Missing or invalid 'certname' field");
    }
    const char *certname = certname_json->data.string_value;

    /* Extract environment (optional) */
    json_value_t *env_json = json_object_get(request, "environment");
    const char *environment = NULL;
    if (env_json && env_json->type == JSON_VALUE_STRING) {
        environment = env_json->data.string_value;
    }

    /* Extract facts (optional) */
    json_value_t *facts_json = json_object_get(request, "facts");

    if (verbose) {
        fprintf(stderr, "[INFO] Compiling catalog for node: %s (env: %s, facts: %s)\n",
                certname, environment ? environment : DEFAULT_ENVIRONMENT,
                facts_json ? "yes" : "no");
    }

    /* Compile catalog */
    char *catalog_json = compile_catalog(certname, environment, facts_json);

    if (!catalog_json) {
        json_value_destroy(request);
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "Failed to compile catalog");
    }

    /* Store facts and catalog in PuppetDB */
    if (pdb) {
        /* Store facts if we have them */
        if (facts_json) {
            char *facts_str = json_value_to_string(facts_json);
            if (facts_str) {
                if (puppetdb_store_facts(pdb, certname, facts_str) != 0) {
                    if (verbose) {
                        fprintf(stderr, "[WARN] Failed to store facts: %s\n",
                                puppetdb_error(pdb));
                    }
                } else if (verbose) {
                    fprintf(stderr, "[INFO] Stored facts for node: %s\n", certname);
                }
                puppet_free(facts_str);
            }
        }

        /* Store catalog */
        if (puppetdb_store_catalog(pdb, certname, catalog_json) != 0) {
            if (verbose) {
                fprintf(stderr, "[WARN] Failed to store catalog: %s\n",
                        puppetdb_error(pdb));
            }
        } else if (verbose) {
            fprintf(stderr, "[INFO] Stored catalog for node: %s\n", certname);
        }
    }

    json_value_destroy(request);

    enum MHD_Result ret = send_json_response(connection, MHD_HTTP_OK, catalog_json);
    puppet_free(catalog_json);

    return ret;
}

/**
 * @brief Handle GET /pdb/query/v4/nodes
 */
static enum MHD_Result handle_pdb_nodes(struct MHD_Connection *connection) {
    if (!pdb) {
        return send_error(connection, MHD_HTTP_SERVICE_UNAVAILABLE,
                         "PuppetDB not enabled");
    }

    char *nodes_json = puppetdb_list_nodes(pdb);
    if (!nodes_json) {
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         puppetdb_error(pdb));
    }

    enum MHD_Result ret = send_json_response(connection, MHD_HTTP_OK, nodes_json);
    puppet_free(nodes_json);
    return ret;
}

/**
 * @brief Handle GET /pdb/query/v4/facts/<certname>
 */
static enum MHD_Result handle_pdb_facts(struct MHD_Connection *connection,
                                        const char *certname) {
    if (!pdb) {
        return send_error(connection, MHD_HTTP_SERVICE_UNAVAILABLE,
                         "PuppetDB not enabled");
    }

    if (!certname || strlen(certname) == 0) {
        return send_error(connection, MHD_HTTP_BAD_REQUEST,
                         "Missing certname");
    }

    char *facts_json = puppetdb_get_facts(pdb, certname);
    if (!facts_json) {
        return send_error(connection, MHD_HTTP_NOT_FOUND,
                         "Node not found or no facts available");
    }

    enum MHD_Result ret = send_json_response(connection, MHD_HTTP_OK, facts_json);
    puppet_free(facts_json);
    return ret;
}

/**
 * @brief Handle GET /pdb/query/v4/catalogs/<certname>
 */
static enum MHD_Result handle_pdb_catalogs(struct MHD_Connection *connection,
                                           const char *certname) {
    if (!pdb) {
        return send_error(connection, MHD_HTTP_SERVICE_UNAVAILABLE,
                         "PuppetDB not enabled");
    }

    if (!certname || strlen(certname) == 0) {
        return send_error(connection, MHD_HTTP_BAD_REQUEST,
                         "Missing certname");
    }

    char *catalog_json = puppetdb_get_catalog(pdb, certname);
    if (!catalog_json) {
        return send_error(connection, MHD_HTTP_NOT_FOUND,
                         "Node not found or no catalog available");
    }

    enum MHD_Result ret = send_json_response(connection, MHD_HTTP_OK, catalog_json);
    puppet_free(catalog_json);
    return ret;
}

/**
 * @brief Handle GET /pdb/query/v4/resources/<type>
 */
static enum MHD_Result handle_pdb_resources(struct MHD_Connection *connection,
                                            const char *type) {
    if (!pdb) {
        return send_error(connection, MHD_HTTP_SERVICE_UNAVAILABLE,
                         "PuppetDB not enabled");
    }

    if (!type || strlen(type) == 0) {
        return send_error(connection, MHD_HTTP_BAD_REQUEST,
                         "Missing resource type");
    }

    char *resources_json = puppetdb_query_exported(pdb, type);
    if (!resources_json) {
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         puppetdb_error(pdb));
    }

    enum MHD_Result ret = send_json_response(connection, MHD_HTTP_OK, resources_json);
    puppet_free(resources_json);
    return ret;
}

/**
 * @brief Handle POST /puppet-ca/v1/certificate_request/:certname
 */
static enum MHD_Result handle_certificate_request(struct MHD_Connection *connection,
                                                   const char *certname,
                                                   const char *csr_pem) {
    if (!ca_ctx) {
        return send_error(connection, MHD_HTTP_SERVICE_UNAVAILABLE,
                         "CA not initialized");
    }

    if (!csr_pem || strlen(csr_pem) == 0) {
        return send_error(connection, MHD_HTTP_BAD_REQUEST,
                         "Missing CSR in request body");
    }

    if (!certname || strlen(certname) == 0) {
        return send_error(connection, MHD_HTTP_BAD_REQUEST,
                         "Missing certname");
    }

    if (verbose) {
        fprintf(stderr, "[INFO] Certificate request for: %s\n", certname);
    }

    /* Check if CSR should be auto-signed */
    if (autosign_config) {
        puppet_csr_info_t *csr_info = puppet_csr_info_create(certname, NULL, NULL, NULL);
        if (!csr_info) {
            return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                             "Failed to create CSR info");
        }

        bool should_sign = puppet_autosign_should_sign(autosign_config, csr_info);
        puppet_csr_info_free(csr_info);

        if (!should_sign) {
            const char *error_msg = puppet_autosign_get_error(autosign_config);
            if (verbose) {
                fprintf(stderr, "[INFO] CSR for %s not auto-signed: %s\n",
                        certname, error_msg ? error_msg : "policy denied");
            }

            /* Save CSR for manual signing */
            if (puppet_ca_save_csr(ca_ctx, certname, csr_pem) == 0) {
                if (verbose) {
                    fprintf(stderr, "[INFO] CSR for %s saved for manual signing\n", certname);
                }
                return send_error(connection, MHD_HTTP_ACCEPTED,
                                 "Certificate request saved. Awaiting manual approval.");
            } else {
                fprintf(stderr, "[ERROR] Failed to save CSR for %s: %s\n",
                        certname, puppet_ca_get_error(ca_ctx));
                return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                                 "Failed to save certificate request");
            }
        }

        if (verbose) {
            fprintf(stderr, "[INFO] CSR for %s approved for auto-signing\n", certname);
        }
    }

    /* Sign the CSR */
    char *signed_cert_pem = NULL;
    int result = puppet_ca_sign_csr(ca_ctx, csr_pem, certname,
                                     PUPPET_CA_CERT_VALIDITY_DAYS,
                                     &signed_cert_pem);

    if (result != 0 || !signed_cert_pem) {
        const char *error_msg = puppet_ca_get_error(ca_ctx);
        if (verbose) {
            fprintf(stderr, "[ERROR] Failed to sign CSR for %s: %s\n",
                    certname, error_msg ? error_msg : "unknown error");
        }
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         error_msg ? error_msg : "Failed to sign CSR");
    }

    if (verbose) {
        fprintf(stderr, "[INFO] Successfully signed certificate for: %s\n", certname);
    }

    /* Return signed certificate */
    enum MHD_Result ret = send_file_response(connection, MHD_HTTP_OK,
                                              signed_cert_pem,
                                              strlen(signed_cert_pem));
    puppet_free(signed_cert_pem);

    return ret;
}

/**
 * @brief Request handler callback
 */
static enum MHD_Result request_handler(void *cls,
                                        struct MHD_Connection *connection,
                                        const char *url,
                                        const char *method,
                                        const char *version,
                                        const char *upload_data,
                                        size_t *upload_data_size,
                                        void **con_cls) {
    (void)cls;
    (void)version;

    /* Handle connection setup */
    if (*con_cls == NULL) {
        struct connection_info *con_info = puppet_calloc(1, sizeof(struct connection_info));
        if (!con_info) return MHD_NO;
        *con_cls = con_info;
        return MHD_YES;
    }

    struct connection_info *con_info = *con_cls;

    /* Accumulate POST data (limit to 10MB to prevent DoS) */
    #define MAX_POST_DATA_SIZE (10 * 1024 * 1024)
    if (*upload_data_size > 0) {
        if (con_info->post_data_size + *upload_data_size > MAX_POST_DATA_SIZE) {
            return MHD_NO;
        }
        char *new_data = puppet_realloc(con_info->post_data,
                                  con_info->post_data_size + *upload_data_size + 1);
        if (!new_data) return MHD_NO;

        memcpy(new_data + con_info->post_data_size, upload_data, *upload_data_size);
        con_info->post_data_size += *upload_data_size;
        new_data[con_info->post_data_size] = '\0';
        con_info->post_data = new_data;

        *upload_data_size = 0;
        return MHD_YES;
    }

    /* Route request */
    enum MHD_Result ret;

    if (strcmp(method, "GET") == 0 && strcmp(url, "/status") == 0) {
        ret = handle_status(connection);
    } else if (strcmp(method, "GET") == 0 &&
               strcmp(url, "/puppet/v3/file_metadatas/plugins") == 0) {
        ret = handle_file_metadatas_plugins(connection);
    } else if (strcmp(method, "GET") == 0 &&
               strncmp(url, "/puppet/v3/file_content/plugins/", 32) == 0) {
        ret = handle_plugin_content(connection, url);
    } else if (strcmp(method, "GET") == 0 &&
               strncmp(url, "/puppet/v3/file_content/", 24) == 0) {
        ret = handle_file_content(connection, url);
    } else if (strcmp(method, "POST") == 0 &&
               strcmp(url, "/puppet/v4/catalog") == 0) {
        ret = handle_catalog(connection, con_info->post_data);
    } else if (strcmp(method, "POST") == 0 &&
               strncmp(url, "/puppet-ca/v1/certificate_request/", 34) == 0) {
        const char *certname = url + 34;
        ret = handle_certificate_request(connection, certname, con_info->post_data);
    } else if (strcmp(method, "GET") == 0 &&
               strcmp(url, "/pdb/query/v4/nodes") == 0) {
        ret = handle_pdb_nodes(connection);
    } else if (strcmp(method, "GET") == 0 &&
               strncmp(url, "/pdb/query/v4/facts/", 20) == 0) {
        const char *certname = url + 20;
        ret = handle_pdb_facts(connection, certname);
    } else if (strcmp(method, "GET") == 0 &&
               strncmp(url, "/pdb/query/v4/catalogs/", 23) == 0) {
        const char *certname = url + 23;
        ret = handle_pdb_catalogs(connection, certname);
    } else if (strcmp(method, "GET") == 0 &&
               strncmp(url, "/pdb/query/v4/resources/", 24) == 0) {
        const char *type = url + 24;
        ret = handle_pdb_resources(connection, type);
    } else if (strcmp(method, "OPTIONS") == 0) {
        /* CORS preflight */
        struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
    } else {
        ret = send_error(connection, MHD_HTTP_NOT_FOUND, "Not found");
    }

    return ret;
}

/**
 * @brief Cleanup callback for connection
 */
static void request_completed(void *cls,
                               struct MHD_Connection *connection,
                               void **con_cls,
                               enum MHD_RequestTerminationCode toe) {
    (void)cls;
    (void)connection;
    (void)toe;

    struct connection_info *con_info = *con_cls;
    if (con_info) {
        puppet_free(con_info->post_data);
        puppet_free(con_info);
    }
    *con_cls = NULL;
}

/**
 * @brief Print usage information
 */
static void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS] [manifest_directory]\n", program_name);
    printf("\nPuppet catalog compilation server with TLS support\n\n");
    printf("Options:\n");
    printf("  -c, --config FILE     Config file (default: %s)\n", DEFAULT_CONFIG_FILE);
    printf("  -p, --port PORT       Listen port (default: %d)\n", DEFAULT_PORT);
    printf("  -m, --modules PATH    Path to modules directory\n");
    printf("  -D, --hiera-data PATH Path to Hiera data directory\n");
    printf("  -P, --puppetdb PATH   Path to PuppetDB SQLite file\n");
    printf("                        (default: %s)\n", DEFAULT_PUPPETDB_PATH);
    printf("  -C, --ca-dir PATH     Path to CA directory for TLS\n");
    printf("                        (default: %s)\n", DEFAULT_CA_DIR);
    printf("  -v, --verbose         Enable verbose output\n");
    printf("  -h, --help            Show this help message\n");
    printf("\nConfig file sections:\n");
    printf("  [main]                Common settings\n");
    printf("  [server]              Server-specific settings\n");
    printf("\nAPI Endpoints:\n");
    printf("  GET  /status                                Health check\n");
    printf("  POST /puppet/v4/catalog                     Compile catalog\n");
    printf("  GET  /puppet/v3/file_content/modules/<m>/<p>  Serve file from module\n");
    printf("\nPluginsync Endpoints:\n");
    printf("  GET  /puppet/v3/file_metadatas/plugins      List all plugin files (lib/)\n");
    printf("  GET  /puppet/v3/file_content/plugins/<path> Download plugin file\n");
    printf("\nCertificate Authority Endpoints:\n");
    printf("  POST /puppet-ca/v1/certificate_request/<certname>  Submit CSR for signing\n");
    printf("\nPuppetDB Endpoints:\n");
    printf("  GET  /pdb/query/v4/nodes                    List all nodes\n");
    printf("  GET  /pdb/query/v4/facts/<certname>         Get facts for node\n");
    printf("  GET  /pdb/query/v4/catalogs/<certname>      Get catalog for node\n");
    printf("  GET  /pdb/query/v4/resources/<type>         Query exported resources\n");
    printf("\nExample:\n");
    printf("  %s -p 8140 /etc/puppet\n", program_name);
    printf("\n  curl -X POST https://localhost:8140/puppet/v4/catalog \\\n");
    printf("       -H 'Content-Type: application/json' \\\n");
    printf("       -d '{\"certname\": \"node1.example.com\"}'\n");
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    int opt;
    const char *config_file = NULL;
    config_file_t *file_config = NULL;

    static struct option long_options[] = {
        {"config", required_argument, 0, 'c'},
        {"port", required_argument, 0, 'p'},
        {"modules", required_argument, 0, 'm'},
        {"hiera-data", required_argument, 0, 'D'},
        {"puppetdb", required_argument, 0, 'P'},
        {"ca-dir", required_argument, 0, 'C'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    /* First pass: find config file option */
    optind = 1;
    while ((opt = getopt_long(argc, argv, "c:p:m:D:P:C:vh", long_options, NULL)) != -1) {
        if (opt == 'c') {
            config_file = optarg;
            break;
        } else if (opt == 'h') {
            print_usage(argv[0]);
            return 0;
        }
    }

    /* Load config file */
    if (config_file) {
        file_config = config_parse(config_file);
        if (!file_config) {
            fprintf(stderr, "Error: Cannot read config file: %s\n", config_file);
            return 1;
        }
    } else {
        /* Try default config file (silently ignore if missing) */
        file_config = config_parse(DEFAULT_CONFIG_FILE);
    }

    /* Apply config file values (lowest priority) */
    if (file_config) {
        const char *val;

        /* Port: try [server] then [main] */
        port = config_get_int(file_config, "server", "port",
               config_get_int(file_config, "main", "port", DEFAULT_PORT));

        /* Manifest path */
        val = config_get_string(file_config, "server", "manifest_dir", NULL);
        if (!val) val = config_get_string(file_config, "main", "manifest_dir", NULL);
        if (val) manifest_path = (char *)val;

        /* Modules path */
        val = config_get_string(file_config, "server", "modules_dir", NULL);
        if (!val) val = config_get_string(file_config, "main", "modules_dir", NULL);
        if (val) modules_path = (char *)val;

        /* Hiera data directory */
        val = config_get_string(file_config, "server", "hiera_datadir", NULL);
        if (!val) val = config_get_string(file_config, "main", "hiera_datadir", NULL);
        if (val) hiera_datadir = (char *)val;

        /* PuppetDB path */
        val = config_get_string(file_config, "server", "puppetdb_path", NULL);
        if (!val) val = config_get_string(file_config, "main", "puppetdb_path", NULL);
        if (val) puppetdb_path = (char *)val;

        /* CA directory */
        val = config_get_string(file_config, "server", "ca_dir", NULL);
        if (!val) val = config_get_string(file_config, "main", "ca_dir", NULL);
        if (val) ca_dir = (char *)val;

        /* Verbose */
        verbose = config_get_bool(file_config, "server", "verbose", 0);
    }

    /* Second pass: command-line options (highest priority) */
    optind = 1;
    while ((opt = getopt_long(argc, argv, "c:p:m:D:P:C:vh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                /* Already handled */
                break;
            case 'p':
                port = atoi(optarg);
                if (port <= 0 || port > 65535) {
                    fprintf(stderr, "Error: Invalid port number\n");
                    config_free(file_config);
                    return 1;
                }
                break;
            case 'm':
                modules_path = optarg;
                break;
            case 'D':
                hiera_datadir = optarg;
                break;
            case 'P':
                puppetdb_path = optarg;
                break;
            case 'C':
                ca_dir = optarg;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                config_free(file_config);
                return 0;
            default:
                print_usage(argv[0]);
                config_free(file_config);
                return 1;
        }
    }

    /* Manifest directory from command line (if provided) */
    if (optind < argc) {
        manifest_path = argv[optind];
    }

    /* Check required settings */
    if (!manifest_path) {
        fprintf(stderr, "Error: No manifest directory specified\n");
        fprintf(stderr, "       Set 'manifest_dir' in config file or provide as argument\n");
        print_usage(argv[0]);
        config_free(file_config);
        return 1;
    }

    /* Set default CA directory if not specified */
    if (!ca_dir) {
        ca_dir = DEFAULT_CA_DIR;
    }

    /* Initialize memory tracking */
    puppet_memory_init();

    /* Initialize OpenSSL */
    if (puppet_ssl_init() != 0) {
        fprintf(stderr, "Error: Failed to initialize OpenSSL\n");
        config_free(file_config);
        return 1;
    }

    /* Initialize PuppetDB */
    if (!puppetdb_path) {
        puppetdb_path = DEFAULT_PUPPETDB_PATH;
    }
    pdb = puppetdb_open(puppetdb_path);
    if (!pdb) {
        fprintf(stderr, "Warning: Failed to open PuppetDB at %s\n", puppetdb_path);
        fprintf(stderr, "         PuppetDB endpoints will be unavailable\n");
    }

    /* Initialize CA */
    ca_ctx = puppet_ca_init(ca_dir);
    if (!ca_ctx) {
        fprintf(stderr, "Warning: Failed to initialize CA at %s\n", ca_dir);
        fprintf(stderr, "         Certificate request endpoints will be unavailable\n");
    } else {
        /* Try to load existing CA or generate if needed */
        if (!puppet_ca_exists(ca_ctx)) {
            if (verbose) {
                fprintf(stderr, "[INFO] CA not found, generating new CA...\n");
            }
            char ca_subject[256];
            snprintf(ca_subject, sizeof(ca_subject), "Puppet CA: puppetc-server");
            if (puppet_ca_generate(ca_ctx, ca_subject, PUPPET_CA_VALIDITY_DAYS) != 0) {
                fprintf(stderr, "Warning: Failed to generate CA: %s\n",
                        puppet_ca_get_error(ca_ctx));
                puppet_ca_free(ca_ctx);
                ca_ctx = NULL;
            } else if (puppet_ca_save(ca_ctx) != 0) {
                fprintf(stderr, "Warning: Failed to save CA: %s\n",
                        puppet_ca_get_error(ca_ctx));
                puppet_ca_free(ca_ctx);
                ca_ctx = NULL;
            } else if (verbose) {
                fprintf(stderr, "[INFO] CA generated successfully\n");
            }
        } else {
            if (puppet_ca_load(ca_ctx) != 0) {
                fprintf(stderr, "Warning: Failed to load CA: %s\n",
                        puppet_ca_get_error(ca_ctx));
                puppet_ca_free(ca_ctx);
                ca_ctx = NULL;
            } else if (verbose) {
                fprintf(stderr, "[INFO] CA loaded successfully\n");
            }
        }
    }

    /* Generate server certificate for TLS if needed */
    if (ca_ctx && ca_dir) {
        if (!puppet_ca_server_cert_exists(ca_ctx)) {
            if (verbose) {
                fprintf(stderr, "[INFO] Generating server certificate for TLS...\n");
            }
            char hostname[256];
            if (gethostname(hostname, sizeof(hostname)) != 0) {
                strcpy(hostname, "localhost");
            }

            /* Get server IP addresses for certificate SAN */
            const char *ip_list[32];
            int ip_count = 0;
            struct ifaddrs *ifaddr, *ifa;
            if (getifaddrs(&ifaddr) == 0) {
                for (ifa = ifaddr; ifa != NULL && ip_count < 30; ifa = ifa->ifa_next) {
                    if (ifa->ifa_addr == NULL) continue;
                    if (ifa->ifa_addr->sa_family == AF_INET) {
                        struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                        char *ip_str = strdup(inet_ntoa(addr->sin_addr));
                        if (ip_str && strcmp(ip_str, "127.0.0.1") != 0) {
                            ip_list[ip_count++] = ip_str;
                            if (verbose) {
                                fprintf(stderr, "[INFO] Adding IP to server certificate: %s\n", ip_str);
                            }
                        } else {
                            free(ip_str);
                        }
                    }
                }
                freeifaddrs(ifaddr);
            }
            ip_list[ip_count] = NULL;

            if (puppet_ca_generate_server_cert(ca_ctx, hostname, ip_list, PUPPET_CA_CERT_VALIDITY_DAYS) != 0) {
                fprintf(stderr, "Warning: Failed to generate server certificate: %s\n",
                        puppet_ca_get_error(ca_ctx));
            } else if (verbose) {
                fprintf(stderr, "[INFO] Server certificate generated\n");
            }

            /* Free IP strings */
            for (int i = 0; i < ip_count; i++) {
                free((void *)ip_list[i]);
            }
        }

        /* Load server TLS credentials */
        if (load_server_tls_credentials(ca_dir) == 0) {
            if (verbose) {
                fprintf(stderr, "[INFO] TLS credentials loaded for HTTPS\n");
            }
        } else {
            fprintf(stderr, "Warning: Failed to load TLS credentials, server will use HTTP only\n");
        }
    }

    /* Initialize autosign configuration */
    const char *autosign_path = "/etc/puppetc/autosign.conf";
    autosign_config = puppet_autosign_init(autosign_path);
    if (!autosign_config) {
        if (verbose) {
            fprintf(stderr, "[WARNING] Failed to load autosign config, defaulting to manual signing\n");
        }
        /* Continue with manual-only mode */
    }
    if (autosign_config && verbose) {
        fprintf(stderr, "[INFO] Autosign mode: %s\n",
                puppet_autosign_mode_to_string(autosign_config->mode));
    }

    /* Set up signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Start server - HTTPS if TLS credentials available, HTTP otherwise */
    struct MHD_Daemon *daemon;
    int use_tls = (server_cert_pem && server_key_pem);

    if (use_tls) {
        /* Start HTTPS server with mTLS (mutual TLS)
         * - MHD_USE_TLS: Enable TLS
         * - MHD_OPTION_HTTPS_MEM_TRUST: CA certificate for verifying client certificates
         * Note: Client certs are optional for CSR submission endpoint
         */
        daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ERROR_LOG | MHD_USE_TLS,
                                   port,
                                   NULL, NULL,
                                   &request_handler, NULL,
                                   MHD_OPTION_HTTPS_MEM_KEY, server_key_pem,
                                   MHD_OPTION_HTTPS_MEM_CERT, server_cert_pem,
                                   MHD_OPTION_HTTPS_MEM_TRUST, ca_cert_pem,
                                   MHD_OPTION_NOTIFY_COMPLETED, &request_completed, NULL,
                                   MHD_OPTION_END);
    } else {
        daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY,
                                   port,
                                   NULL, NULL,
                                   &request_handler, NULL,
                                   MHD_OPTION_NOTIFY_COMPLETED, &request_completed, NULL,
                                   MHD_OPTION_END);
    }

    if (!daemon) {
        fprintf(stderr, "Error: Failed to start %s server on port %d\n",
                use_tls ? "HTTPS" : "HTTP", port);
        config_free(file_config);
        puppet_ssl_cleanup();
        return 1;
    }

    printf("puppetc-server started on port %d (%s)\n", port, use_tls ? "HTTPS" : "HTTP");
    printf("Manifest directory: %s\n", manifest_path);
    if (modules_path) printf("Modules directory: %s\n", modules_path);
    if (hiera_datadir) printf("Hiera data directory: %s\n", hiera_datadir);
    if (pdb) printf("PuppetDB: %s\n", puppetdb_path);
    printf("\nPress Ctrl+C to stop\n");

    /* Main loop */
    while (running) {
        sleep(1);
    }

    printf("\nShutting down...\n");

    MHD_stop_daemon(daemon);

    /* Cleanup */
    if (pdb) puppetdb_close(pdb);
    if (autosign_config) puppet_autosign_free(autosign_config);
    if (ca_ctx) puppet_ca_free(ca_ctx);
    if (server_cert_pem) free(server_cert_pem);
    if (server_key_pem) free(server_key_pem);
    if (ca_cert_pem) free(ca_cert_pem);
    config_free(file_config);
    puppet_ssl_cleanup();
    puppet_memory_shutdown();

    return 0;
}
