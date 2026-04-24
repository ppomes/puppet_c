/**
 * @file puppet_catalog_validate.c
 * @brief Post-compilation catalog validation passes
 *
 * Two validations that run after the catalog is fully built:
 *
 *  1. Relationship metaparameter refs (require/subscribe/before/notify)
 *     must point to resources that exist in the catalog. This is what
 *     the standard Puppet compiler checks at the finalize step and
 *     emits as "Could not find resource 'X[Y]' in parameter 'p'".
 *
 *  2. File source URLs of the form puppet:///modules/MOD/PATH must
 *     resolve to a real file under $modulepath/MOD/files/PATH. Catches
 *     dangling references that only fail at agent apply time
 *     (fileserver 404).
 */

#include "puppet_catalog.h"
#include "puppet_ast.h"
#include "puppet_stdlib.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---------- helpers ---------- */

static int type_eq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/* Does the catalog contain any resource matching {type, title}? */
static int catalog_has_resource(const puppet_catalog_t *cat,
                                const char *type, const char *title) {
    if (!cat || !type || !title) return 0;
    /* Class[foo::bar] isn't stored in resources[]; it lives in the
     * classes[] list populated by include/require. Match there too. */
    if (type_eq_ci(type, "class")) {
        const char *t = title;
        if (strncmp(t, "::", 2) == 0) t += 2;
        for (size_t i = 0; i < cat->class_count; i++) {
            const char *cn = cat->classes[i];
            if (!cn) continue;
            if (strncmp(cn, "::", 2) == 0) cn += 2;
            if (strcmp(cn, t) == 0) return 1;
        }
    }
    for (size_t i = 0; i < cat->resource_count; i++) {
        const puppet_catalog_resource_t *r = &cat->resources[i];
        if (type_eq_ci(r->type, type) && r->title && strcmp(r->title, title) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Parse "Type[title]" into type and title buffers.
 * Returns 0 on success. Skips leading :: if present on type. */
static int parse_ref(const char *ref,
                     char *type_out, size_t type_size,
                     char *title_out, size_t title_size) {
    if (!ref) return -1;
    const char *start = ref;
    if (strncmp(start, "::", 2) == 0) start += 2;
    const char *lb = strchr(start, '[');
    if (!lb || lb == start) return -1;
    const char *rb = strrchr(lb, ']');
    if (!rb || rb <= lb + 1) return -1;
    size_t tlen = (size_t)(lb - start);
    size_t nlen = (size_t)(rb - lb - 1);
    if (tlen == 0 || tlen >= type_size || nlen >= title_size) return -1;
    memcpy(type_out, start, tlen);  type_out[tlen] = '\0';
    memcpy(title_out, lb + 1, nlen); title_out[nlen] = '\0';
    /* A valid type starts with a letter — rules out false positives like
     * strings that just happen to contain brackets. */
    if (!isalpha((unsigned char)type_out[0])) return -1;
    return 0;
}

/* ---------- ref validation ---------- */

static void check_ref_string(const puppet_catalog_t *cat,
                             const puppet_catalog_resource_t *src,
                             const char *param_name,
                             const char *ref_str) {
    char type_buf[128];
    char title_buf[512];
    if (parse_ref(ref_str, type_buf, sizeof(type_buf),
                  title_buf, sizeof(title_buf)) != 0) {
        /* Not a recognizable Type[title] token — skip silently. Plain
         * strings are valid values for some of these params in edge cases. */
        return;
    }
    if (catalog_has_resource(cat, type_buf, title_buf)) return;

    puppet_location_t loc;
    loc.filename = src->file ? src->file : "";
    loc.line = src->line;
    loc.column = 0;
    puppet_error_at(loc,
        "Could not find resource '%s[%s]' in parameter '%s' on %s[%s]",
        type_buf, title_buf, param_name, src->type, src->title);
}

static void check_value_refs(const puppet_catalog_t *cat,
                             const puppet_catalog_resource_t *src,
                             const char *param_name,
                             const puppet_value_t *v) {
    if (!v) return;
    switch (v->type) {
    case PUPPET_VALUE_STRING:
        if (v->data.string.data) {
            check_ref_string(cat, src, param_name, v->data.string.data);
        }
        break;
    case PUPPET_VALUE_ARRAY:
        if (v->data.array) {
            for (size_t i = 0; i < v->data.array->count; i++) {
                check_value_refs(cat, src, param_name, v->data.array->items[i]);
            }
        }
        break;
    default:
        break;
    }
}

void puppet_catalog_validate_refs(puppet_catalog_t *catalog) {
    if (!catalog) return;
    static const char *rel_params[] = { "require", "subscribe", "before", "notify" };
    const size_t rel_count = sizeof(rel_params) / sizeof(rel_params[0]);

    for (size_t i = 0; i < catalog->resource_count; i++) {
        puppet_catalog_resource_t *r = &catalog->resources[i];
        for (size_t j = 0; j < r->param_count; j++) {
            const char *pname = r->parameters[j].name;
            if (!pname) continue;
            int is_rel = 0;
            for (size_t k = 0; k < rel_count; k++) {
                if (strcmp(pname, rel_params[k]) == 0) { is_rel = 1; break; }
            }
            if (!is_rel) continue;
            check_value_refs(catalog, r, pname, r->parameters[j].value);
        }
    }
}

/* ---------- source URL validation ---------- */

/* Return 1 if path exists as a regular file or directory.
 * File resources can source either — recurse-copying a directory
 * is a legitimate pattern. */
static int file_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode) || S_ISDIR(st.st_mode);
}

/* Try every entry in colon-separated modulepath and check whether
 * $entry/MOD/files/PATH exists. Return 1 if any match. */
static int source_url_resolvable(const char *modulepath,
                                 const char *module, const char *rel) {
    if (!modulepath || !module || !rel) return 0;
    size_t mplen = strlen(modulepath);
    /* tokenize on ':' without mutating the caller's string */
    const char *p = modulepath;
    while (p && *p) {
        const char *end = strchr(p, ':');
        size_t seg_len = end ? (size_t)(end - p) : strlen(p);
        if (seg_len > 0 && seg_len < 1024) {
            char candidate[2048];
            int n = snprintf(candidate, sizeof(candidate),
                             "%.*s/%s/files/%s",
                             (int)seg_len, p, module, rel);
            if (n > 0 && (size_t)n < sizeof(candidate) && file_exists(candidate)) {
                return 1;
            }
        }
        if (!end) break;
        p = end + 1;
    }
    (void)mplen;
    return 0;
}

static void check_source_string(const puppet_catalog_resource_t *src,
                                const char *modulepath,
                                const char *url) {
    /* We only understand puppet:///modules/MOD/PATH */
    if (!url) return;
    const char *prefix = "puppet:///modules/";
    size_t plen = strlen(prefix);
    if (strncmp(url, prefix, plen) != 0) return;
    const char *rest = url + plen;
    const char *slash = strchr(rest, '/');
    if (!slash || slash == rest) return;

    char module[256];
    size_t mlen = (size_t)(slash - rest);
    if (mlen >= sizeof(module)) return;
    memcpy(module, rest, mlen);
    module[mlen] = '\0';

    const char *rel = slash + 1;
    if (*rel == '\0') return;

    if (source_url_resolvable(modulepath, module, rel)) return;

    puppet_location_t loc;
    loc.filename = src->file ? src->file : "";
    loc.line = src->line;
    loc.column = 0;
    puppet_error_at(loc,
        "Could not retrieve information from environment production source(s) %s on %s[%s]",
        url, src->type, src->title);
}

static void check_value_sources(const puppet_catalog_resource_t *src,
                                const char *modulepath,
                                const puppet_value_t *v) {
    if (!v) return;
    switch (v->type) {
    case PUPPET_VALUE_STRING:
        if (v->data.string.data) {
            check_source_string(src, modulepath, v->data.string.data);
        }
        break;
    case PUPPET_VALUE_ARRAY:
        if (v->data.array) {
            for (size_t i = 0; i < v->data.array->count; i++) {
                check_value_sources(src, modulepath, v->data.array->items[i]);
            }
        }
        break;
    default:
        break;
    }
}

void puppet_catalog_validate_sources(puppet_catalog_t *catalog, const char *modulepath) {
    if (!catalog || !modulepath || !*modulepath) return;

    for (size_t i = 0; i < catalog->resource_count; i++) {
        puppet_catalog_resource_t *r = &catalog->resources[i];
        /* File sources are the only classical use. ssh_authorized_key and
         * similar types take content= strings but not puppet:/// URLs. */
        if (!r->type || !type_eq_ci(r->type, "File")) continue;
        for (size_t j = 0; j < r->param_count; j++) {
            const char *pname = r->parameters[j].name;
            if (!pname || strcmp(pname, "source") != 0) continue;
            check_value_sources(r, modulepath, r->parameters[j].value);
        }
    }
}
