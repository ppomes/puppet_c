/**
 * @file puppet_deadcode.c
 * @brief Dead-code tracker implementation.
 *
 * Inventory is built by line-scanning .pp files for `class NAME`, `define NAME`,
 * and `function NAME` headers; templates and ruby functions are discovered by
 * filesystem walk. Hooks fed during compilation mark each item "used"; items
 * still unseen at the end are reported as dead.
 */
#include "puppet_deadcode.h"
#include "puppet_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

/* ---------- small set helpers ---------- */

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const*)a, *(const char *const*)b);
}

static void set_grow(puppet_deadcode_set_t *s) {
    if (s->count < s->capacity) return;
    size_t new_cap = s->capacity ? s->capacity * 2 : 32;
    s->items = puppet_realloc(s->items, new_cap * sizeof(char*));
    s->capacity = new_cap;
}

/* For declared sets (insertion-time, no sort yet, no dedup). */
static void set_add_decl(puppet_deadcode_set_t *s, char ***parallel_files, const char *name, const char *file) {
    set_grow(s);
    s->items[s->count] = puppet_strdup(name);
    if (parallel_files) {
        *parallel_files = puppet_realloc(*parallel_files, s->capacity * sizeof(char*));
        (*parallel_files)[s->count] = puppet_strdup(file);
    }
    s->count++;
}

static void set_sort(puppet_deadcode_set_t *s) {
    if (s->count) qsort(s->items, s->count, sizeof(char*), cmp_str);
}

static bool set_contains(puppet_deadcode_set_t *s, const char *name) {
    if (!s->count) return false;
    /* Linear for small sets, binary for sorted. Declared sets are sorted; used sets are small-ish. */
    size_t lo = 0, hi = s->count;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(s->items[mid], name);
        if (c == 0) return true;
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return false;
}

/* Thread-safe insertion for used sets (keeps sorted, deduped). */
static void set_add_used(puppet_deadcode_set_t *s, const char *name) {
    size_t lo = 0, hi = s->count;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(s->items[mid], name);
        if (c == 0) return; /* dup */
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    set_grow(s);
    memmove(&s->items[lo + 1], &s->items[lo], (s->count - lo) * sizeof(char*));
    s->items[lo] = puppet_strdup(name);
    s->count++;
}

/* ---------- file walking ---------- */

static void walk_pp_file(puppet_deadcode_t *dc, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        const char *kind = NULL;
        size_t klen = 0;
        if (strncmp(p, "class ", 6) == 0)           { kind = "class";    klen = 6; }
        else if (strncmp(p, "define ", 7) == 0)     { kind = "define";   klen = 7; }
        else if (strncmp(p, "function ", 9) == 0)   { kind = "function"; klen = 9; }
        else continue;
        p += klen;
        while (*p == ' ' || *p == '\t') p++;
        /* Extract identifier: [A-Za-z_][\w:]* */
        const char *start = p;
        while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == ':')) p++;
        if (p == start) continue;
        /* Peek the next non-space char to confirm header form */
        const char *q = p;
        while (*q == ' ' || *q == '\t') q++;
        if (*q != '(' && *q != '{' && strncmp(q, "inherits", 8) != 0) continue;
        char *name = puppet_malloc(p - start + 1);
        memcpy(name, start, p - start);
        name[p - start] = '\0';
        /* Strip leading :: if any */
        const char *nm = (strncmp(name, "::", 2) == 0) ? name + 2 : name;
        if (kind[0] == 'c')      set_add_decl(&dc->declared_classes,   &dc->class_files,    nm, path);
        else if (kind[0] == 'd') set_add_decl(&dc->declared_defines,   &dc->define_files,   nm, path);
        else                     set_add_decl(&dc->declared_pp_funcs,  &dc->pp_func_files,  nm, path);
        puppet_free(name);
    }
    fclose(f);
}

static void walk_pp_dir(puppet_deadcode_t *dc, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        char path[2048];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) walk_pp_dir(dc, path);
        else if (S_ISREG(st.st_mode)) {
            size_t n = strlen(ent->d_name);
            if (n >= 3 && strcmp(ent->d_name + n - 3, ".pp") == 0)
                walk_pp_file(dc, path);
        }
    }
    closedir(d);
}

static void walk_templates_dir(puppet_deadcode_t *dc, const char *mod_name, const char *dir, const char *rel_prefix) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        char path[2048], rel[2048];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (rel_prefix && *rel_prefix)
            snprintf(rel, sizeof(rel), "%s/%s", rel_prefix, ent->d_name);
        else
            snprintf(rel, sizeof(rel), "%s", ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            walk_templates_dir(dc, mod_name, path, rel);
        } else if (S_ISREG(st.st_mode)) {
            size_t n = strlen(ent->d_name);
            if (n >= 4 && (strcmp(ent->d_name + n - 4, ".erb") == 0 ||
                           strcmp(ent->d_name + n - 4, ".epp") == 0)) {
                char qname[2560];
                snprintf(qname, sizeof(qname), "%s/%s", mod_name, rel);
                set_add_decl(&dc->declared_templates, &dc->template_files, qname, path);
            }
        }
    }
    closedir(d);
}

static void walk_rb_funcs_dir(puppet_deadcode_t *dc, const char *dir, const char *ns_prefix) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        char path[2048];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            /* Namespaced functions: <module>/<name>.rb */
            char sub_prefix[512];
            if (ns_prefix && *ns_prefix)
                snprintf(sub_prefix, sizeof(sub_prefix), "%s::%s", ns_prefix, ent->d_name);
            else
                snprintf(sub_prefix, sizeof(sub_prefix), "%s", ent->d_name);
            walk_rb_funcs_dir(dc, path, sub_prefix);
        } else if (S_ISREG(st.st_mode)) {
            size_t n = strlen(ent->d_name);
            if (n >= 3 && strcmp(ent->d_name + n - 3, ".rb") == 0) {
                char name[512];
                size_t base_len = n - 3;
                if (base_len >= sizeof(name)) continue;
                memcpy(name, ent->d_name, base_len);
                name[base_len] = '\0';
                char full[1024];
                if (ns_prefix && *ns_prefix)
                    snprintf(full, sizeof(full), "%s::%s", ns_prefix, name);
                else
                    snprintf(full, sizeof(full), "%s", name);
                set_add_decl(&dc->declared_rb_funcs, &dc->rb_func_files, full, path);
            }
        }
    }
    closedir(d);
}

puppet_deadcode_t *puppet_deadcode_create(const char *modules_path) {
    if (!modules_path) return NULL;
    puppet_deadcode_t *dc = puppet_calloc(1, sizeof(*dc));
    pthread_mutex_init(&dc->mutex, NULL);

    DIR *d = opendir(modules_path);
    if (!d) return dc;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        char mod_root[2048];
        snprintf(mod_root, sizeof(mod_root), "%s/%s", modules_path, ent->d_name);
        struct stat st;
        if (stat(mod_root, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        char manifests[3072], templates[3072], old_funcs[3072], new_funcs[3072];
        snprintf(manifests, sizeof(manifests), "%s/manifests", mod_root);
        snprintf(templates, sizeof(templates), "%s/templates", mod_root);
        snprintf(old_funcs, sizeof(old_funcs), "%s/lib/puppet/parser/functions", mod_root);
        snprintf(new_funcs, sizeof(new_funcs), "%s/lib/puppet/functions", mod_root);
        walk_pp_dir(dc, manifests);
        walk_templates_dir(dc, ent->d_name, templates, "");
        walk_rb_funcs_dir(dc, old_funcs, "");
        walk_rb_funcs_dir(dc, new_funcs, "");
    }
    closedir(d);

    /* Sort declared sets (keeping parallel file arrays aligned). */
    /* Since we inserted in directory-walk order, re-sort via a single pass using an index array. */
    #define SORT_DECL(set, files) do {                                                   \
        if ((set).count < 2) break;                                                      \
        size_t *idx = puppet_malloc((set).count * sizeof(size_t));                       \
        for (size_t i = 0; i < (set).count; i++) idx[i] = i;                             \
        /* insertion sort on idx by items[idx[i]] */                                     \
        for (size_t i = 1; i < (set).count; i++) {                                       \
            size_t j = i;                                                                \
            while (j > 0 && strcmp((set).items[idx[j-1]], (set).items[idx[j]]) > 0) {    \
                size_t tmp = idx[j-1]; idx[j-1] = idx[j]; idx[j] = tmp; j--;             \
            }                                                                            \
        }                                                                                \
        char **ni = puppet_malloc((set).count * sizeof(char*));                          \
        char **nf = (files) ? puppet_malloc((set).count * sizeof(char*)) : NULL;         \
        for (size_t i = 0; i < (set).count; i++) {                                       \
            ni[i] = (set).items[idx[i]];                                                 \
            if (nf) nf[i] = (files)[idx[i]];                                             \
        }                                                                                \
        puppet_free((set).items); (set).items = ni;                                      \
        if (nf) { puppet_free((files)); (files) = nf; }                                  \
        puppet_free(idx);                                                                \
    } while (0)
    SORT_DECL(dc->declared_classes,   dc->class_files);
    SORT_DECL(dc->declared_defines,   dc->define_files);
    SORT_DECL(dc->declared_pp_funcs,  dc->pp_func_files);
    SORT_DECL(dc->declared_rb_funcs,  dc->rb_func_files);
    SORT_DECL(dc->declared_templates, dc->template_files);
    #undef SORT_DECL

    return dc;
}

static void free_set(puppet_deadcode_set_t *s) {
    for (size_t i = 0; i < s->count; i++) puppet_free(s->items[i]);
    puppet_free(s->items);
    s->items = NULL; s->count = s->capacity = 0;
}

static void free_file_array(char **arr, size_t n) {
    if (!arr) return;
    for (size_t i = 0; i < n; i++) puppet_free(arr[i]);
    puppet_free(arr);
}

void puppet_deadcode_destroy(puppet_deadcode_t *dc) {
    if (!dc) return;
    size_t nc = dc->declared_classes.count, nd = dc->declared_defines.count;
    size_t np = dc->declared_pp_funcs.count, nr = dc->declared_rb_funcs.count;
    size_t nt = dc->declared_templates.count;
    free_set(&dc->declared_classes);
    free_set(&dc->declared_defines);
    free_set(&dc->declared_pp_funcs);
    free_set(&dc->declared_rb_funcs);
    free_set(&dc->declared_templates);
    free_set(&dc->used_classes);
    free_set(&dc->used_defines);
    free_set(&dc->used_functions);
    free_set(&dc->used_templates);
    free_file_array(dc->class_files, nc);
    free_file_array(dc->define_files, nd);
    free_file_array(dc->pp_func_files, np);
    free_file_array(dc->rb_func_files, nr);
    free_file_array(dc->template_files, nt);
    pthread_mutex_destroy(&dc->mutex);
    puppet_free(dc);
}

/* ---------- mark-used API ---------- */

static const char *strip_cc(const char *name) {
    return (name && strncmp(name, "::", 2) == 0) ? name + 2 : name;
}

void puppet_deadcode_mark_class_used(puppet_deadcode_t *dc, const char *name) {
    if (!dc || !name) return;
    name = strip_cc(name);
    pthread_mutex_lock(&dc->mutex);
    set_add_used(&dc->used_classes, name);
    pthread_mutex_unlock(&dc->mutex);
}
void puppet_deadcode_mark_define_used(puppet_deadcode_t *dc, const char *name) {
    if (!dc || !name) return;
    name = strip_cc(name);
    pthread_mutex_lock(&dc->mutex);
    set_add_used(&dc->used_defines, name);
    pthread_mutex_unlock(&dc->mutex);
}
void puppet_deadcode_mark_function_used(puppet_deadcode_t *dc, const char *name) {
    if (!dc || !name) return;
    name = strip_cc(name);
    pthread_mutex_lock(&dc->mutex);
    set_add_used(&dc->used_functions, name);
    pthread_mutex_unlock(&dc->mutex);
}
void puppet_deadcode_mark_template_used(puppet_deadcode_t *dc, const char *name) {
    if (!dc || !name) return;
    pthread_mutex_lock(&dc->mutex);
    set_add_used(&dc->used_templates, name);
    pthread_mutex_unlock(&dc->mutex);
}

/* ---------- report ---------- */

static void report_section(FILE *out, const char *label,
                           puppet_deadcode_set_t *decl, char **files,
                           puppet_deadcode_set_t *used)
{
    size_t dead = 0;
    for (size_t i = 0; i < decl->count; i++) {
        if (!set_contains(used, decl->items[i])) dead++;
    }
    fprintf(out, "=== %s: %zu declared, %zu unused ===\n", label, decl->count, dead);
    for (size_t i = 0; i < decl->count; i++) {
        if (!set_contains(used, decl->items[i])) {
            fprintf(out, "  %s\t(%s)\n", decl->items[i], files ? files[i] : "");
        }
    }
}

void puppet_deadcode_report(puppet_deadcode_t *dc, FILE *out) {
    if (!dc || !out) return;
    fprintf(out, "\n=== Dead-code report ===\n");
    report_section(out, "Classes",        &dc->declared_classes,   dc->class_files,    &dc->used_classes);
    report_section(out, "Defines",        &dc->declared_defines,   dc->define_files,   &dc->used_defines);
    report_section(out, "Puppet functions", &dc->declared_pp_funcs, dc->pp_func_files, &dc->used_functions);
    report_section(out, "Ruby functions",   &dc->declared_rb_funcs, dc->rb_func_files, &dc->used_functions);
    report_section(out, "Templates",      &dc->declared_templates, dc->template_files, &dc->used_templates);
}
