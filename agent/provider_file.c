/**
 * @file provider_file.c
 * @brief File resource provider
 *
 * Manages file resources: create, modify content, set permissions, delete.
 * Generic implementation works on all platforms.
 *
 * Supports puppet:/// URLs for sourcing files from the Puppet server.
 */

#include <stdio.h>
#include <stdlib.h>
#include "puppet_memory.h"
#include "file_utils.h"
#include "color_output.h"
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <curl/curl.h>

#include "puppet_provider.h"

/* Server URL from environment or default */
static const char *get_puppet_server(void) {
    const char *server = getenv("PUPPET_SERVER");
    return server ? server : "http://localhost:8140";
}

/* ============================================================================
 * Puppet URL Handling (puppet:///modules/...)
 * ============================================================================ */

/**
 * @brief Buffer for HTTP response
 */
struct http_response_buffer {
    char *data;
    size_t size;
};

/**
 * @brief Curl write callback
 */
static size_t http_write_callback(void *contents, size_t size, size_t nmemb,
                                   void *userp) {
    size_t realsize = size * nmemb;
    struct http_response_buffer *buf = (struct http_response_buffer *)userp;

    char *ptr = puppet_realloc(buf->data, buf->size + realsize + 1);
    if (!ptr) return 0;

    buf->data = ptr;
    memcpy(&(buf->data[buf->size]), contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = '\0';

    return realsize;
}

/**
 * @brief Check if source is a puppet:/// URL
 */
static bool is_puppet_url(const char *source) {
    return source && strncmp(source, "puppet:///", 10) == 0;
}

/**
 * @brief Fetch file content from puppet:/// URL
 *
 * Converts puppet:///modules/<module>/<path> to
 * http://<server>/puppet/v3/file_content/modules/<module>/<path>
 *
 * @param source The puppet:/// URL
 * @param size_out Output: size of fetched content
 * @return Allocated buffer with content, or NULL on error
 */
static char *fetch_puppet_url(const char *source, size_t *size_out) {
    if (!source || strncmp(source, "puppet:///", 10) != 0) {
        return NULL;
    }

    /* Extract path after puppet:/// */
    const char *path = source + 10;  /* Skip "puppet:///" */

    /* Only support modules/ mount point */
    if (strncmp(path, "modules/", 8) != 0) {
        fprintf(stderr, "Error: Only puppet:///modules/ URLs are supported\n");
        return NULL;
    }

    /* Build HTTP URL */
    const char *server = get_puppet_server();
    char url[4096];
    snprintf(url, sizeof(url), "%s/puppet/v3/file_content/%s", server, path);

    /* Initialize curl */
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Error: Failed to initialize curl\n");
        return NULL;
    }

    struct http_response_buffer response = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "Error: Failed to fetch %s: %s\n",
                url, curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        puppet_free(response.data);
        return NULL;
    }

    /* Check HTTP status */
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code != 200) {
        fprintf(stderr, "Error: Server returned HTTP %ld for %s\n",
                http_code, url);
        puppet_free(response.data);
        return NULL;
    }

    if (size_out) {
        *size_out = response.size;
    }

    return response.data;
}

static uid_t get_uid(const char *owner) {
    if (!owner) return (uid_t)-1;

    /* Try numeric UID first */
    char *end;
    long uid = strtol(owner, &end, 10);
    if (*end == '\0') return (uid_t)uid;

    /* Look up by name */
    struct passwd *pw = getpwnam(owner);
    if (pw) return pw->pw_uid;

    return (uid_t)-1;
}

static gid_t get_gid(const char *group) {
    if (!group) return (gid_t)-1;

    /* Try numeric GID first */
    char *end;
    long gid = strtol(group, &end, 10);
    if (*end == '\0') return (gid_t)gid;

    /* Look up by name */
    struct group *gr = getgrnam(group);
    if (gr) return gr->gr_gid;

    return (gid_t)-1;
}

static mode_t parse_mode(const char *mode_str) {
    if (!mode_str) return 0;

    /* Parse octal mode string */
    return (mode_t)strtol(mode_str, NULL, 8);
}

/* ============================================================================
 * File Provider Implementation
 * ============================================================================ */

static resource_state_t file_check(const resource_t *resource, apply_context_t *ctx) {
    (void)ctx;

    const char *path = resource->title;
    const char *ensure = resource_get_param(resource, "ensure");

    if (!ensure) ensure = "present";

    if (strcmp(ensure, "absent") == 0) {
        return file_exists(path) ? RESOURCE_STATE_PRESENT : RESOURCE_STATE_ABSENT;
    }

    if (strcmp(ensure, "directory") == 0) {
        if (!file_exists(path)) return RESOURCE_STATE_ABSENT;
        return is_directory(path) ? RESOURCE_STATE_PRESENT : RESOURCE_STATE_ABSENT;
    }

    if (strcmp(ensure, "link") == 0) {
        if (!path_exists(path)) return RESOURCE_STATE_ABSENT;
        return is_symlink(path) ? RESOURCE_STATE_PRESENT : RESOURCE_STATE_ABSENT;
    }

    /* Default: file/present */
    return file_exists(path) ? RESOURCE_STATE_PRESENT : RESOURCE_STATE_ABSENT;
}

static apply_result_t file_apply(const resource_t *resource, apply_context_t *ctx) {
    const char *path = resource->title;
    const char *ensure = resource_get_param(resource, "ensure");
    const char *content = resource_get_param(resource, "content");
    const char *source = resource_get_param(resource, "source");
    const char *owner = resource_get_param(resource, "owner");
    const char *group = resource_get_param(resource, "group");
    const char *mode = resource_get_param(resource, "mode");
    const char *target = resource_get_param(resource, "target");

    if (!ensure) ensure = "present";

    bool changed = false;

    /* Handle ensure => absent */
    if (strcmp(ensure, "absent") == 0) {
        if (file_exists(path)) {
            if (ctx->noop) {
                print_resource_noop("File", path, "ensure", "would be removed");
                return APPLY_SKIPPED;
            }

            if (is_directory(path)) {
                if (rmdir(path) != 0) {
                    apply_context_set_error(ctx, "Failed to remove directory %s: %s",
                                           path, strerror(errno));
                    return APPLY_FAILED;
                }
            } else {
                if (unlink(path) != 0) {
                    apply_context_set_error(ctx, "Failed to remove file %s: %s",
                                           path, strerror(errno));
                    return APPLY_FAILED;
                }
            }
            print_resource_change("File", path, "ensure", "removed");
            return APPLY_CHANGED;
        }
        return APPLY_NOOP;
    }

    /* Handle ensure => directory */
    if (strcmp(ensure, "directory") == 0) {
        if (!file_exists(path)) {
            if (ctx->noop) {
                print_resource_noop("File", path, "ensure", "would be created as directory");
                return APPLY_SKIPPED;
            }

            if (mkdir(path, 0755) != 0) {
                apply_context_set_error(ctx, "Failed to create directory %s: %s",
                                       path, strerror(errno));
                return APPLY_FAILED;
            }
            print_resource_change("File", path, "ensure", "created as directory");
            changed = true;
        } else if (!is_directory(path)) {
            apply_context_set_error(ctx, "%s exists but is not a directory", path);
            return APPLY_FAILED;
        }
    }
    /* Handle ensure => link */
    else if (strcmp(ensure, "link") == 0) {
        if (!target) {
            apply_context_set_error(ctx, "Link resource requires 'target' parameter");
            return APPLY_FAILED;
        }

        bool needs_create = false;

        if (path_exists(path)) {
            if (is_symlink(path)) {
                char current_target[1024];
                ssize_t len = readlink(path, current_target, sizeof(current_target) - 1);
                if (len > 0) {
                    current_target[len] = '\0';
                    if (strcmp(current_target, target) != 0) {
                        /* Wrong target, need to recreate */
                        if (!ctx->noop) {
                            unlink(path);
                        }
                        needs_create = true;
                    }
                }
            } else {
                apply_context_set_error(ctx, "%s exists but is not a symlink", path);
                return APPLY_FAILED;
            }
        } else {
            needs_create = true;
        }

        if (needs_create) {
            if (ctx->noop) {
                print_resource_noop("File", path, "ensure", "would be created as symlink");
                return APPLY_SKIPPED;
            }

            if (symlink(target, path) != 0) {
                apply_context_set_error(ctx, "Failed to create symlink %s -> %s: %s",
                                       path, target, strerror(errno));
                return APPLY_FAILED;
            }
            print_resource_change("File", path, "ensure", "created as symlink -> %s", target);
            changed = true;
        }
    }
    /* Handle ensure => present/file */
    else {
        if (!file_exists(path)) {
            if (ctx->noop) {
                print_resource_noop("File", path, "ensure", "would be created");
                return APPLY_SKIPPED;
            }

            if (write_file_contents(path, content ? content : "") != 0) {
                apply_context_set_error(ctx, "Failed to create file %s: %s",
                                       path, strerror(errno));
                return APPLY_FAILED;
            }
            print_resource_change("File", path, "ensure", "created");
            changed = true;
        } else if (content) {
            /* Check if content needs updating */
            char *current = read_file_contents(path, 0);
            if (current) {
                if (strcmp(current, content) != 0) {
                    if (ctx->noop) {
                        print_resource_noop("File", path, "content", "would be updated");
                        puppet_free(current);
                        return APPLY_SKIPPED;
                    }

                    if (write_file_contents(path, content) != 0) {
                        apply_context_set_error(ctx, "Failed to update file %s: %s",
                                               path, strerror(errno));
                        puppet_free(current);
                        return APPLY_FAILED;
                    }
                    print_resource_change("File", path, "content", "updated");
                    changed = true;
                }
                puppet_free(current);
            }
        }

        /* Handle source parameter (copy from source or puppet:/// URL) */
        if (source && !content) {
            char *source_content = NULL;

            if (is_puppet_url(source)) {
                /* Fetch from puppet server */
                source_content = fetch_puppet_url(source, NULL);
                if (!source_content) {
                    apply_context_set_error(ctx, "Cannot fetch puppet URL: %s", source);
                    return APPLY_FAILED;
                }
            } else {
                /* Read from local file */
                source_content = read_file_contents(source, 0);
                if (!source_content) {
                    apply_context_set_error(ctx, "Cannot read source file: %s", source);
                    return APPLY_FAILED;
                }
            }

            char *current = read_file_contents(path, 0);
            if (!current || strcmp(current, source_content) != 0) {
                if (ctx->noop) {
                    print_resource_noop("File", path, "content", "would be copied from source");
                    puppet_free(source_content);
                    puppet_free(current);
                    return APPLY_SKIPPED;
                }

                if (write_file_contents(path, source_content) != 0) {
                    apply_context_set_error(ctx, "Failed to copy to %s: %s",
                                           path, strerror(errno));
                    puppet_free(source_content);
                    puppet_free(current);
                    return APPLY_FAILED;
                }
                print_resource_change("File", path, "content", "copied from %s", source);
                changed = true;
            }
            puppet_free(source_content);
            puppet_free(current);
        }
    }

    /* Handle ownership */
    if (owner || group) {
        uid_t uid = owner ? get_uid(owner) : (uid_t)-1;
        gid_t gid = group ? get_gid(group) : (gid_t)-1;

        if (uid != (uid_t)-1 || gid != (gid_t)-1) {
            struct stat st;
            if (stat(path, &st) == 0) {
                bool needs_chown = false;
                if (uid != (uid_t)-1 && st.st_uid != uid) needs_chown = true;
                if (gid != (gid_t)-1 && st.st_gid != gid) needs_chown = true;

                if (needs_chown) {
                    if (ctx->noop) {
                        print_resource_noop("File", path, "owner", "would be changed");
                    } else {
                        if (chown(path, uid, gid) != 0) {
                            apply_context_set_error(ctx, "Failed to chown %s: %s",
                                                   path, strerror(errno));
                            return APPLY_FAILED;
                        }
                        print_resource_change("File", path, "owner", "changed");
                        changed = true;
                    }
                }
            }
        }
    }

    /* Handle mode/permissions */
    if (mode) {
        mode_t new_mode = parse_mode(mode);
        if (new_mode != 0) {
            struct stat st;
            if (stat(path, &st) == 0) {
                mode_t current_mode = st.st_mode & 07777;
                if (current_mode != new_mode) {
                    if (ctx->noop) {
                        char msg[64];
                        snprintf(msg, sizeof(msg), "would be changed to %04o", new_mode);
                        print_resource_noop("File", path, "mode", msg);
                    } else {
                        if (chmod(path, new_mode) != 0) {
                            apply_context_set_error(ctx, "Failed to chmod %s: %s",
                                                   path, strerror(errno));
                            return APPLY_FAILED;
                        }
                        print_resource_change("File", path, "mode", "changed to %04o", new_mode);
                        changed = true;
                    }
                }
            }
        }
    }

    if (ctx->noop && changed) {
        return APPLY_SKIPPED;
    }

    return changed ? APPLY_CHANGED : APPLY_NOOP;
}

/* Provider definition */
static const provider_t file_provider = {
    .name = "file",
    .resource_type = "file",
    .os_family = 0,  /* Generic - works on all platforms */
    .apply = file_apply,
    .check = file_check,
    .cleanup = NULL
};

void provider_file_register(void) {
    provider_register(&file_provider);
}
