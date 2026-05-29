/**
 * @file puppet_pluginsync.c
 * @brief Pluginsync client implementation
 *
 * Downloads custom types, providers, and facts from the Puppet server.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <curl/curl.h>
#include <openssl/md5.h>

#include "puppet_pluginsync.h"
#include "puppet_memory.h"
#include "puppet_json_common.h"

/* Buffer for HTTP response */
struct response_buffer {
    char *data;
    size_t size;
};

/*
 * Apply TLS server-identity verification and mTLS to a curl handle, mirroring
 * the agent's main catalog path. Pluginsync downloads executable Ruby that the
 * agent then runs, so the server must be authenticated: VERIFYHOST=2 enforces
 * the server hostname against the cert, pinning the Puppet CA makes "trusted"
 * mean "signed by our CA" (not the system bundle), and the client cert enables
 * mutual TLS. Applied once to the shared handle, covering metadata + content.
 */
static void pluginsync_configure_tls(CURL *curl,
                                     const pluginsync_config_t *config) {
    if (!curl || !config) return;
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    if (config->ssl_ca_cert_path) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, config->ssl_ca_cert_path);
    }
    if (config->ssl_cert_path) {
        curl_easy_setopt(curl, CURLOPT_SSLCERT, config->ssl_cert_path);
    }
    if (config->ssl_key_path) {
        curl_easy_setopt(curl, CURLOPT_SSLKEY, config->ssl_key_path);
    }
}

/*
 * Reject server-supplied plugin paths that would escape libdir. The path is
 * concatenated onto libdir and written to disk, so a value like
 * "../../../etc/cron.d/x" or "/etc/passwd" from a malicious or compromised
 * server would otherwise let it write arbitrary files as the agent (often
 * root). Only accept a relative path with no absolute prefix and no ".."
 * component (checked per path component, so legitimate names like "foo..bar"
 * are still allowed).
 */
static bool pluginsync_path_is_safe(const char *rel) {
    if (!rel || rel[0] == '\0') return false;
    if (rel[0] == '/') return false;  /* absolute path */
    const char *p = rel;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if (len == 2 && p[0] == '.' && p[1] == '.') return false;  /* ".." */
        if (!slash) break;
        p = slash + 1;
    }
    return true;
}

/**
 * CURL write callback
 */
static size_t write_callback(void *contents, size_t size, size_t nmemb,
                             void *userp) {
    size_t realsize = size * nmemb;
    struct response_buffer *buf = (struct response_buffer *)userp;

    char *ptr = puppet_realloc(buf->data, buf->size + realsize + 1);
    if (!ptr) return 0;

    buf->data = ptr;
    memcpy(&(buf->data[buf->size]), contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = '\0';

    return realsize;
}

/**
 * Compute MD5 checksum of a file
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

bool pluginsync_file_matches(const char *path, const char *expected_md5) {
    if (!path || !expected_md5) return false;

    /* Check if file exists */
    struct stat st;
    if (stat(path, &st) != 0) return false;

    /* Compute actual checksum */
    char *actual_md5 = file_md5(path);
    if (!actual_md5) return false;

    /* Compare (expected may have {md5} prefix) */
    const char *expected = expected_md5;
    if (strncmp(expected, "{md5}", 5) == 0) {
        expected += 5;
    }

    bool matches = (strcasecmp(actual_md5, expected) == 0);
    puppet_free(actual_md5);

    return matches;
}

int pluginsync_mkdir_p(const char *path) {
    if (!path) return -1;

    /* Make a copy we can modify */
    char *tmp = puppet_strdup(path);
    if (!tmp) return -1;

    /* Find last slash to get parent directory */
    char *last_slash = strrchr(tmp, '/');
    if (!last_slash) {
        puppet_free(tmp);
        return 0;  /* No directory component */
    }
    *last_slash = '\0';

    /* Create directories recursively */
    char *p = tmp;
    if (*p == '/') p++;  /* Skip leading slash */

    while (*p) {
        /* Find next slash */
        char *slash = strchr(p, '/');
        if (slash) *slash = '\0';

        /* Create directory if needed */
        struct stat st;
        if (stat(tmp, &st) != 0) {
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                puppet_free(tmp);
                return -1;
            }
        }

        if (slash) {
            *slash = '/';
            p = slash + 1;
        } else {
            break;
        }
    }

    puppet_free(tmp);
    return 0;
}

/**
 * Download a single file from the server
 */
static int download_file(CURL *curl, const char *base_url,
                         const char *remote_path, const char *local_path,
                         bool verbose) {
    /* Create parent directories */
    if (pluginsync_mkdir_p(local_path) != 0) {
        if (verbose) {
            fprintf(stderr, "  Error: Cannot create directory for %s\n", local_path);
        }
        return -1;
    }

    /* Build URL */
    char url[4096];
    snprintf(url, sizeof(url), "%s/puppet/v3/file_content/plugins/%s",
             base_url, remote_path);

    struct response_buffer buf = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        if (verbose) {
            fprintf(stderr, "  Error: curl failed: %s\n", curl_easy_strerror(res));
        }
        puppet_free(buf.data);
        return -1;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        if (verbose) {
            fprintf(stderr, "  Error: HTTP %ld for %s\n", http_code, url);
        }
        puppet_free(buf.data);
        return -1;
    }

    /* Write to file */
    FILE *fp = fopen(local_path, "wb");
    if (!fp) {
        if (verbose) {
            fprintf(stderr, "  Error: Cannot write %s: %s\n",
                    local_path, strerror(errno));
        }
        puppet_free(buf.data);
        return -1;
    }

    size_t written = fwrite(buf.data, 1, buf.size, fp);
    fclose(fp);
    puppet_free(buf.data);

    if (written != buf.size) {
        if (verbose) {
            fprintf(stderr, "  Error: Write failed for %s\n", local_path);
        }
        return -1;
    }

    return 0;
}

int pluginsync_run(const pluginsync_config_t *config, pluginsync_result_t *result) {
    if (!config || !config->server_url || !config->libdir) {
        return -1;
    }

    /* Initialize result */
    pluginsync_result_t res = {0};

    if (config->verbose) {
        fprintf(stderr, "Info: Pluginsync from %s\n", config->server_url);
    }

    /* Initialize CURL */
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Error: Failed to initialize CURL\n");
        return -1;
    }

    /* Authenticate the server (host + Puppet CA) and present the client
     * cert before any request — this handle is reused for downloads. */
    pluginsync_configure_tls(curl, config);

    /* Fetch file metadata from server */
    char url[4096];
    snprintf(url, sizeof(url), "%s/puppet/v3/file_metadatas/plugins",
             config->server_url);

    struct response_buffer buf = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode curl_res = curl_easy_perform(curl);
    if (curl_res != CURLE_OK) {
        if (config->verbose) {
            fprintf(stderr, "Warning: Pluginsync failed: %s\n",
                    curl_easy_strerror(curl_res));
        }
        curl_easy_cleanup(curl);
        if (result) *result = res;
        return -1;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        if (config->verbose) {
            fprintf(stderr, "Warning: Pluginsync HTTP %ld\n", http_code);
        }
        puppet_free(buf.data);
        curl_easy_cleanup(curl);
        if (result) *result = res;
        return -1;
    }

    /* Parse JSON response */
    json_parser_t *parser = json_parser_create(buf.data);
    if (!parser) {
        puppet_free(buf.data);
        curl_easy_cleanup(curl);
        if (result) *result = res;
        return -1;
    }

    json_value_t *metadata = json_parse_value(parser);
    json_parser_destroy(parser);
    puppet_free(buf.data);

    if (!metadata || metadata->type != JSON_VALUE_ARRAY) {
        if (metadata) json_value_destroy(metadata);
        curl_easy_cleanup(curl);
        if (result) *result = res;
        return -1;
    }

    /* Process each file */
    size_t count = metadata->data.array.count;
    if (config->verbose) {
        fprintf(stderr, "Info: %zu plugin files available\n", count);
    }

    for (size_t i = 0; i < count; i++) {
        json_value_t *entry = metadata->data.array.elements[i];
        if (!entry || entry->type != JSON_VALUE_OBJECT) continue;

        res.files_checked++;

        /* Get path */
        json_value_t *path_val = json_object_get(entry, "path");
        if (!path_val || path_val->type != JSON_VALUE_STRING) continue;
        const char *remote_path = path_val->data.string_value;

        /* Refuse paths that would escape libdir (path traversal). Always
         * report it — it means the server is malicious or misbehaving. */
        if (!pluginsync_path_is_safe(remote_path)) {
            fprintf(stderr,
                    "Warning: pluginsync rejecting unsafe server path '%s'\n",
                    remote_path);
            res.errors++;
            continue;
        }

        /* Get checksum */
        json_value_t *checksum_obj = json_object_get(entry, "checksum");
        const char *checksum = NULL;
        if (checksum_obj && checksum_obj->type == JSON_VALUE_OBJECT) {
            json_value_t *value = json_object_get(checksum_obj, "value");
            if (value && value->type == JSON_VALUE_STRING) {
                checksum = value->data.string_value;
            }
        }

        /* Build local path */
        char local_path[4096];
        snprintf(local_path, sizeof(local_path), "%s/%s",
                 config->libdir, remote_path);

        /* Check if file needs update */
        if (checksum && pluginsync_file_matches(local_path, checksum)) {
            res.files_skipped++;
            continue;
        }

        /* Download file */
        if (config->verbose) {
            fprintf(stderr, "  Downloading: %s\n", remote_path);
        }

        if (download_file(curl, config->server_url, remote_path,
                          local_path, config->verbose) == 0) {
            res.files_downloaded++;
        } else {
            res.errors++;
        }
    }

    json_value_destroy(metadata);
    curl_easy_cleanup(curl);

    if (config->verbose) {
        fprintf(stderr, "Info: Pluginsync complete: %d downloaded, %d up-to-date, %d errors\n",
                res.files_downloaded, res.files_skipped, res.errors);
    }

    if (result) *result = res;
    return (res.errors > 0) ? -1 : 0;
}
