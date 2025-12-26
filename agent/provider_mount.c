/**
 * @file provider_mount.c
 * @brief Mount resource provider - manages filesystem mounts
 *
 * Manages filesystem mounts via /etc/fstab and mount/umount commands.
 *
 * Supported parameters:
 *   - name: Mount point path (title)
 *   - device: Device to mount (e.g., /dev/sda1, UUID=..., LABEL=...)
 *   - fstype: Filesystem type (ext4, xfs, nfs, etc.)
 *   - options: Mount options (defaults, ro, noexec, etc.)
 *   - ensure: mounted, unmounted, present, absent, defined
 *   - dump: Dump field in fstab (default: 0)
 *   - pass: Pass field in fstab (default: 0)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <mntent.h>

#include "puppet_provider.h"
#include "puppet_memory.h"
#include "color_output.h"

#define FSTAB_PATH "/etc/fstab"
#define MTAB_PATH "/proc/mounts"

/* ============================================================================
 * Fstab Entry Structure
 * ============================================================================ */

typedef struct {
    char *device;
    char *mountpoint;
    char *fstype;
    char *options;
    int dump;
    int pass;
} fstab_entry_t;

typedef struct {
    fstab_entry_t *entries;
    size_t count;
    char **comments;       /* Lines that are comments or blank */
    size_t comment_count;
} fstab_t;

static void fstab_entry_free(fstab_entry_t *entry) {
    if (!entry) return;
    puppet_free(entry->device);
    puppet_free(entry->mountpoint);
    puppet_free(entry->fstype);
    puppet_free(entry->options);
}

static void fstab_free(fstab_t *fstab) {
    if (!fstab) return;

    for (size_t i = 0; i < fstab->count; i++) {
        fstab_entry_free(&fstab->entries[i]);
    }
    puppet_free(fstab->entries);

    for (size_t i = 0; i < fstab->comment_count; i++) {
        puppet_free(fstab->comments[i]);
    }
    puppet_free(fstab->comments);

    puppet_free(fstab);
}

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Read and parse /etc/fstab
 */
static fstab_t *read_fstab(void) {
    FILE *fp = setmntent(FSTAB_PATH, "r");
    if (!fp) return NULL;

    fstab_t *fstab = puppet_calloc(1, sizeof(fstab_t));
    if (!fstab) {
        endmntent(fp);
        return NULL;
    }

    struct mntent *mnt;
    while ((mnt = getmntent(fp)) != NULL) {
        fstab_entry_t *new_entries = puppet_realloc(fstab->entries,
                                              (fstab->count + 1) * sizeof(fstab_entry_t));
        if (!new_entries) break;

        fstab->entries = new_entries;
        fstab_entry_t *entry = &fstab->entries[fstab->count];

        entry->device = puppet_strdup(mnt->mnt_fsname);
        entry->mountpoint = puppet_strdup(mnt->mnt_dir);
        entry->fstype = puppet_strdup(mnt->mnt_type);
        entry->options = puppet_strdup(mnt->mnt_opts);
        entry->dump = mnt->mnt_freq;
        entry->pass = mnt->mnt_passno;

        fstab->count++;
    }

    endmntent(fp);
    return fstab;
}

/**
 * @brief Find an entry in fstab by mount point
 */
static fstab_entry_t *find_fstab_entry(fstab_t *fstab, const char *mountpoint) {
    if (!fstab || !mountpoint) return NULL;

    for (size_t i = 0; i < fstab->count; i++) {
        if (strcmp(fstab->entries[i].mountpoint, mountpoint) == 0) {
            return &fstab->entries[i];
        }
    }
    return NULL;
}

/**
 * @brief Write fstab entries back to file
 */
static int write_fstab(fstab_t *fstab) {
    if (!fstab) return -1;

    /* Read original file to preserve comments */
    FILE *fp = fopen(FSTAB_PATH, "r");
    char **original_lines = NULL;
    size_t line_count = 0;

    if (fp) {
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            char **new_lines = puppet_realloc(original_lines,
                                        (line_count + 1) * sizeof(char *));
            if (!new_lines) break;
            original_lines = new_lines;

            /* Check if line is a comment or blank */
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;

            if (*p == '#' || *p == '\n' || *p == '\0') {
                original_lines[line_count++] = puppet_strdup(line);
            } else {
                /* Entry line - we'll regenerate these */
                original_lines[line_count++] = NULL;
            }
        }
        fclose(fp);
    }

    /* Write the file */
    fp = fopen(FSTAB_PATH, "w");
    if (!fp) {
        for (size_t i = 0; i < line_count; i++) {
            puppet_free(original_lines[i]);
        }
        puppet_free(original_lines);
        return -1;
    }

    /* Write header if no original comments */
    if (line_count == 0) {
        fprintf(fp, "# /etc/fstab - filesystem table\n");
        fprintf(fp, "# <device>  <mountpoint>  <fstype>  <options>  <dump>  <pass>\n\n");
    }

    /* Write original comments and regenerate entries */
    size_t entry_idx = 0;
    for (size_t i = 0; i < line_count; i++) {
        if (original_lines[i]) {
            fputs(original_lines[i], fp);
            puppet_free(original_lines[i]);
        } else if (entry_idx < fstab->count) {
            fstab_entry_t *e = &fstab->entries[entry_idx++];
            fprintf(fp, "%s\t%s\t%s\t%s\t%d\t%d\n",
                    e->device, e->mountpoint, e->fstype,
                    e->options ? e->options : "defaults",
                    e->dump, e->pass);
        }
    }

    /* Write any remaining entries */
    while (entry_idx < fstab->count) {
        fstab_entry_t *e = &fstab->entries[entry_idx++];
        fprintf(fp, "%s\t%s\t%s\t%s\t%d\t%d\n",
                e->device, e->mountpoint, e->fstype,
                e->options ? e->options : "defaults",
                e->dump, e->pass);
    }

    puppet_free(original_lines);
    fclose(fp);
    return 0;
}

/**
 * @brief Add or update an entry in fstab
 */
static int fstab_add_or_update(fstab_t *fstab, const char *mountpoint,
                                const char *device, const char *fstype,
                                const char *options, int dump, int pass) {
    fstab_entry_t *existing = find_fstab_entry(fstab, mountpoint);

    if (existing) {
        /* Update existing entry */
        puppet_free(existing->device);
        puppet_free(existing->fstype);
        puppet_free(existing->options);

        existing->device = puppet_strdup(device);
        existing->fstype = puppet_strdup(fstype);
        existing->options = options ? puppet_strdup(options) : puppet_strdup("defaults");
        existing->dump = dump;
        existing->pass = pass;
    } else {
        /* Add new entry */
        fstab_entry_t *new_entries = puppet_realloc(fstab->entries,
                                              (fstab->count + 1) * sizeof(fstab_entry_t));
        if (!new_entries) return -1;

        fstab->entries = new_entries;
        fstab_entry_t *entry = &fstab->entries[fstab->count];

        entry->device = puppet_strdup(device);
        entry->mountpoint = puppet_strdup(mountpoint);
        entry->fstype = puppet_strdup(fstype);
        entry->options = options ? puppet_strdup(options) : puppet_strdup("defaults");
        entry->dump = dump;
        entry->pass = pass;

        fstab->count++;
    }

    return 0;
}

/**
 * @brief Remove an entry from fstab
 */
static int fstab_remove(fstab_t *fstab, const char *mountpoint) {
    for (size_t i = 0; i < fstab->count; i++) {
        if (strcmp(fstab->entries[i].mountpoint, mountpoint) == 0) {
            fstab_entry_free(&fstab->entries[i]);

            /* Shift remaining entries */
            for (size_t j = i; j < fstab->count - 1; j++) {
                fstab->entries[j] = fstab->entries[j + 1];
            }
            fstab->count--;
            return 0;
        }
    }
    return -1; /* Not found */
}

/**
 * @brief Check if a filesystem is currently mounted
 */
static int is_mounted(const char *mountpoint) {
    FILE *fp = setmntent(MTAB_PATH, "r");
    if (!fp) return 0;

    struct mntent *mnt;
    while ((mnt = getmntent(fp)) != NULL) {
        if (strcmp(mnt->mnt_dir, mountpoint) == 0) {
            endmntent(fp);
            return 1;
        }
    }

    endmntent(fp);
    return 0;
}

/**
 * @brief Mount a filesystem
 */
static int do_mount(const char *device, const char *mountpoint,
                    const char *fstype, const char *options) {
    char cmd[2048];

    /* Ensure mount point exists */
    struct stat st;
    if (stat(mountpoint, &st) != 0) {
        if (mkdir(mountpoint, 0755) != 0 && errno != EEXIST) {
            return -1;
        }
    }

    if (options && strlen(options) > 0 && strcmp(options, "defaults") != 0) {
        snprintf(cmd, sizeof(cmd), "mount -t %s -o %s %s %s 2>&1",
                 fstype, options, device, mountpoint);
    } else {
        snprintf(cmd, sizeof(cmd), "mount -t %s %s %s 2>&1",
                 fstype, device, mountpoint);
    }

    return system(cmd);
}

/**
 * @brief Unmount a filesystem
 */
static int do_umount(const char *mountpoint) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "umount %s 2>&1", mountpoint);
    return system(cmd);
}

/* ============================================================================
 * Provider Implementation
 * ============================================================================ */

static apply_result_t mount_apply(const resource_t *resource, apply_context_t *ctx) {
    const char *mountpoint = resource->title;
    const char *ensure = resource_get_param(resource, "ensure");
    const char *device = resource_get_param(resource, "device");
    const char *fstype = resource_get_param(resource, "fstype");
    const char *options = resource_get_param(resource, "options");
    const char *dump_str = resource_get_param(resource, "dump");
    const char *pass_str = resource_get_param(resource, "pass");

    /* Defaults */
    if (!ensure) ensure = "mounted";
    if (!options) options = "defaults";
    int dump = dump_str ? atoi(dump_str) : 0;
    int pass = pass_str ? atoi(pass_str) : 0;

    /* Read current fstab */
    fstab_t *fstab = read_fstab();
    if (!fstab) {
        fstab = puppet_calloc(1, sizeof(fstab_t));
        if (!fstab) {
            apply_context_set_error(ctx, "Failed to allocate fstab");
            return APPLY_FAILED;
        }
    }

    fstab_entry_t *current = find_fstab_entry(fstab, mountpoint);
    int currently_mounted = is_mounted(mountpoint);

    /* Handle ensure => absent or unmounted */
    if (strcmp(ensure, "absent") == 0) {
        /* Remove from fstab and unmount */
        if (!current && !currently_mounted) {
            fstab_free(fstab);
            return APPLY_SUCCESS;
        }

        if (ctx->noop) {
            if (currently_mounted) {
                print_resource_noop("Mount", mountpoint, "ensure", "would unmount");
            }
            if (current) {
                print_resource_noop("Mount", mountpoint, "fstab", "would remove from fstab");
            }
            fstab_free(fstab);
            return APPLY_SKIPPED;
        }

        /* Unmount first */
        if (currently_mounted) {
            if (do_umount(mountpoint) != 0) {
                apply_context_set_error(ctx, "Failed to unmount %s", mountpoint);
                fstab_free(fstab);
                return APPLY_FAILED;
            }
            print_resource_change("Mount", mountpoint, "ensure", "mounted", "unmounted");
        }

        /* Remove from fstab */
        if (current) {
            fstab_remove(fstab, mountpoint);
            if (write_fstab(fstab) != 0) {
                apply_context_set_error(ctx, "Failed to write fstab");
                fstab_free(fstab);
                return APPLY_FAILED;
            }
            print_resource_change("Mount", mountpoint, "fstab", "present", "absent");
        }

        fstab_free(fstab);
        return APPLY_CHANGED;
    }

    if (strcmp(ensure, "unmounted") == 0) {
        /* Keep in fstab but ensure unmounted */
        if (!currently_mounted) {
            fstab_free(fstab);
            return APPLY_SUCCESS;
        }

        if (ctx->noop) {
            print_resource_noop("Mount", mountpoint, "ensure", "would unmount");
            fstab_free(fstab);
            return APPLY_SKIPPED;
        }

        if (do_umount(mountpoint) != 0) {
            apply_context_set_error(ctx, "Failed to unmount %s", mountpoint);
            fstab_free(fstab);
            return APPLY_FAILED;
        }

        print_resource_change("Mount", mountpoint, "ensure", "mounted", "unmounted");
        fstab_free(fstab);
        return APPLY_CHANGED;
    }

    /* For mounted/present/defined, we need device and fstype */
    if (!device) {
        apply_context_set_error(ctx, "Mount resource requires 'device' parameter");
        fstab_free(fstab);
        return APPLY_FAILED;
    }

    if (!fstype) {
        apply_context_set_error(ctx, "Mount resource requires 'fstype' parameter");
        fstab_free(fstab);
        return APPLY_FAILED;
    }

    /* Handle ensure => defined (only fstab, don't mount) */
    if (strcmp(ensure, "defined") == 0 || strcmp(ensure, "present") == 0) {
        /* Check if fstab needs updating */
        int needs_update = 0;

        if (!current) {
            needs_update = 1;
        } else if (strcmp(current->device, device) != 0 ||
                   strcmp(current->fstype, fstype) != 0 ||
                   strcmp(current->options ? current->options : "defaults", options) != 0 ||
                   current->dump != dump || current->pass != pass) {
            needs_update = 1;
        }

        if (!needs_update) {
            fstab_free(fstab);
            return APPLY_SUCCESS;
        }

        if (ctx->noop) {
            print_resource_noop("Mount", mountpoint, "fstab",
                              current ? "would update in fstab" : "would add to fstab");
            fstab_free(fstab);
            return APPLY_SKIPPED;
        }

        fstab_add_or_update(fstab, mountpoint, device, fstype, options, dump, pass);
        if (write_fstab(fstab) != 0) {
            apply_context_set_error(ctx, "Failed to write fstab");
            fstab_free(fstab);
            return APPLY_FAILED;
        }

        print_resource_change("Mount", mountpoint, "fstab",
                            current ? "outdated" : "absent", "configured");
        fstab_free(fstab);
        return APPLY_CHANGED;
    }

    /* ensure => mounted (default) */
    int fstab_changed = 0;
    int mount_changed = 0;

    /* Check if fstab needs updating */
    if (!current ||
        strcmp(current->device, device) != 0 ||
        strcmp(current->fstype, fstype) != 0 ||
        strcmp(current->options ? current->options : "defaults", options) != 0 ||
        current->dump != dump || current->pass != pass) {

        if (ctx->noop) {
            print_resource_noop("Mount", mountpoint, "fstab",
                              current ? "would update" : "would add");
        } else {
            fstab_add_or_update(fstab, mountpoint, device, fstype, options, dump, pass);
            if (write_fstab(fstab) != 0) {
                apply_context_set_error(ctx, "Failed to write fstab");
                fstab_free(fstab);
                return APPLY_FAILED;
            }
            fstab_changed = 1;
        }
    }

    /* Check if needs mounting */
    if (!currently_mounted) {
        if (ctx->noop) {
            print_resource_noop("Mount", mountpoint, "ensure", "would mount");
            fstab_free(fstab);
            return APPLY_SKIPPED;
        }

        if (do_mount(device, mountpoint, fstype, options) != 0) {
            apply_context_set_error(ctx, "Failed to mount %s", mountpoint);
            fstab_free(fstab);
            return APPLY_FAILED;
        }
        mount_changed = 1;
    }

    fstab_free(fstab);

    if (fstab_changed || mount_changed) {
        if (fstab_changed && mount_changed) {
            print_resource_change("Mount", mountpoint, "ensure", "absent", "mounted");
        } else if (fstab_changed) {
            print_resource_change("Mount", mountpoint, "fstab", "outdated", "configured");
        } else {
            print_resource_change("Mount", mountpoint, "ensure", "unmounted", "mounted");
        }
        return APPLY_CHANGED;
    }

    return APPLY_SUCCESS;
}

/* Provider definition */
static const provider_t mount_provider = {
    .name = "mount",
    .resource_type = "mount",
    .os_family = 0, /* Generic - works on all Linux */
    .apply = mount_apply,
    .cleanup = NULL
};

void provider_mount_register(void) {
    provider_register(&mount_provider);
}
