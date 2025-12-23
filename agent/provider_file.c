/**
 * @file provider_file.c
 * @brief File resource provider
 *
 * Manages file resources: create, modify content, set permissions, delete.
 * Generic implementation works on all platforms.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>

#include "puppet_provider.h"

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

static bool is_symlink(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return false;
    return S_ISLNK(st.st_mode);
}

static char *read_file_contents(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0 || size > 10 * 1024 * 1024) {  /* Limit 10MB */
        fclose(f);
        return NULL;
    }

    char *content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(content, 1, size, f);
    content[read] = '\0';
    fclose(f);

    return content;
}

static int write_file_contents(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    if (content) {
        size_t len = strlen(content);
        if (fwrite(content, 1, len, f) != len) {
            fclose(f);
            return -1;
        }
    }

    fclose(f);
    return 0;
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
        if (!file_exists(path)) return RESOURCE_STATE_ABSENT;
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
                printf("  Would remove: %s\n", path);
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
            printf("  Removed: %s\n", path);
            return APPLY_CHANGED;
        }
        return APPLY_NOOP;
    }

    /* Handle ensure => directory */
    if (strcmp(ensure, "directory") == 0) {
        if (!file_exists(path)) {
            if (ctx->noop) {
                printf("  Would create directory: %s\n", path);
                return APPLY_SKIPPED;
            }

            if (mkdir(path, 0755) != 0) {
                apply_context_set_error(ctx, "Failed to create directory %s: %s",
                                       path, strerror(errno));
                return APPLY_FAILED;
            }
            printf("  Created directory: %s\n", path);
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

        if (file_exists(path)) {
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
                printf("  Would create symlink: %s -> %s\n", path, target);
                return APPLY_SKIPPED;
            }

            if (symlink(target, path) != 0) {
                apply_context_set_error(ctx, "Failed to create symlink %s -> %s: %s",
                                       path, target, strerror(errno));
                return APPLY_FAILED;
            }
            printf("  Created symlink: %s -> %s\n", path, target);
            changed = true;
        }
    }
    /* Handle ensure => present/file */
    else {
        if (!file_exists(path)) {
            if (ctx->noop) {
                printf("  Would create file: %s\n", path);
                return APPLY_SKIPPED;
            }

            if (write_file_contents(path, content ? content : "") != 0) {
                apply_context_set_error(ctx, "Failed to create file %s: %s",
                                       path, strerror(errno));
                return APPLY_FAILED;
            }
            printf("  Created file: %s\n", path);
            changed = true;
        } else if (content) {
            /* Check if content needs updating */
            char *current = read_file_contents(path);
            if (current) {
                if (strcmp(current, content) != 0) {
                    if (ctx->noop) {
                        printf("  Would update content: %s\n", path);
                        free(current);
                        return APPLY_SKIPPED;
                    }

                    if (write_file_contents(path, content) != 0) {
                        apply_context_set_error(ctx, "Failed to update file %s: %s",
                                               path, strerror(errno));
                        free(current);
                        return APPLY_FAILED;
                    }
                    printf("  Updated content: %s\n", path);
                    changed = true;
                }
                free(current);
            }
        }

        /* Handle source parameter (copy from source) */
        if (source && !content) {
            char *source_content = read_file_contents(source);
            if (!source_content) {
                apply_context_set_error(ctx, "Cannot read source file: %s", source);
                return APPLY_FAILED;
            }

            char *current = read_file_contents(path);
            if (!current || strcmp(current, source_content) != 0) {
                if (ctx->noop) {
                    printf("  Would copy from: %s\n", source);
                    free(source_content);
                    free(current);
                    return APPLY_SKIPPED;
                }

                if (write_file_contents(path, source_content) != 0) {
                    apply_context_set_error(ctx, "Failed to copy to %s: %s",
                                           path, strerror(errno));
                    free(source_content);
                    free(current);
                    return APPLY_FAILED;
                }
                printf("  Copied from: %s\n", source);
                changed = true;
            }
            free(source_content);
            free(current);
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
                        printf("  Would change ownership: %s\n", path);
                    } else {
                        if (chown(path, uid, gid) != 0) {
                            apply_context_set_error(ctx, "Failed to chown %s: %s",
                                                   path, strerror(errno));
                            return APPLY_FAILED;
                        }
                        printf("  Changed ownership: %s\n", path);
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
                        printf("  Would change mode to %04o: %s\n", new_mode, path);
                    } else {
                        if (chmod(path, new_mode) != 0) {
                            apply_context_set_error(ctx, "Failed to chmod %s: %s",
                                                   path, strerror(errno));
                            return APPLY_FAILED;
                        }
                        printf("  Changed mode to %04o: %s\n", new_mode, path);
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
