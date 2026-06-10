#include "puppet_interpreter.h"
#include "puppet_program_state.h"
#include "puppet_erb.h"
#include "puppet_stdlib.h"
#include "puppet_loader.h"
#include "puppet_memory.h"
#include "puppet_deadcode.h"
#include "puppet_hiera.h"
#include "puppet_json_parser.h"
#include "puppet_json.h"
#include "puppet_json_common.h"
#include "puppetdb.h"
#include "facter.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <regex.h>
#include <limits.h>
#include <unistd.h>
#include <sys/wait.h>
/* Note: Do NOT include ruby.h here - it redefines snprintf to ruby_snprintf
 * which is not thread-safe. Ruby is only needed in puppet_erb.c */

/* Global verbose flag */
bool puppet_verbose = false;

/* Wrapper around regcomp() that translates Ruby/PCRE idioms into POSIX ERE:
 *   (?i:...), (?m:...), (?i)... — inline flag groups → POSIX flags
 *   \/                          — Ruby-escaped slash → "/"
 *   inside [...]                — strip redundant backslash escapes; collect
 *                                 literal "-" and emit at end of class so it
 *                                 doesn't form an invalid range
 * POSIX cannot express per-group flags, so flag groups promote to whole-pattern.
 * On success returns 0 and the caller must regfree(rx). */
static int puppet_regcomp(regex_t *rx, const char *pattern, int cflags) {
    if (!pattern) return REG_BADPAT;

    int extra_flags = 0;
    size_t len = strlen(pattern);
    /* Worst-case: extra byte per class for literal "-" appended */
    char *buf = (char *)puppet_malloc(len * 2 + 2);
    char *dst = buf;
    const char *src = pattern;
    bool in_class = false;
    bool literal_dash = false;

    while (*src) {
        /* Inline flag groups (outside char class) */
        if (!in_class && src[0] == '(' && src[1] == '?' &&
            (src[2] == 'i' || src[2] == 'm' || src[2] == 'x' || src[2] == '-')) {
            const char *q = src + 2;
            int local_icase = 0, local_newline = 0;
            bool negating = false, valid = true;
            while (*q && *q != ':' && *q != ')') {
                switch (*q) {
                    case 'i': if (!negating) local_icase = 1; break;
                    case 'm': if (!negating) local_newline = 1; break;
                    case 'x': break;
                    case '-': negating = true; break;
                    default: valid = false; break;
                }
                if (!valid) break;
                q++;
            }
            if (valid && *q == ':') {
                if (local_icase) extra_flags |= REG_ICASE;
                if (local_newline) extra_flags |= REG_NEWLINE;
                *dst++ = '(';
                src = q + 1;
                continue;
            }
            if (valid && *q == ')') {
                if (local_icase) extra_flags |= REG_ICASE;
                if (local_newline) extra_flags |= REG_NEWLINE;
                src = q + 1;
                continue;
            }
        }

        /* Enter character class */
        if (!in_class && *src == '[') {
            in_class = true;
            literal_dash = false;
            *dst++ = *src++;
            /* A leading "^" or "]" inside [] is literal; emit verbatim */
            if (*src == '^') *dst++ = *src++;
            if (*src == ']') *dst++ = *src++;
            continue;
        }

        /* Close character class — append collected literal dash, then "]" */
        if (in_class && *src == ']') {
            if (literal_dash) *dst++ = '-';
            in_class = false;
            literal_dash = false;
            *dst++ = *src++;
            continue;
        }

        /* Backslash handling */
        if (*src == '\\' && src[1]) {
            char esc = src[1];
            if (esc == '/') {
                /* Ruby-specific \/ — always literal slash */
                *dst++ = '/';
                src += 2;
                continue;
            }
            /* Ruby/PCRE string anchors that POSIX regex doesn't know.
             * Outside a character class, \A is start-of-string and \z / \Z are
             * end-of-string; map them to POSIX ^ and $. (\Z also matches before
             * a trailing newline in Ruby — $ is close enough here.) Without
             * this, glibc treats \A as a literal 'A', so e.g. Stdlib::Filemode
             * (Pattern[/\A([0-7]{1,4})\z/]) never matches '0644' — 248 false
             * positives on the real tree once item 18 made the aliases resolve. */
            if (!in_class && esc == 'A') {
                *dst++ = '^';
                src += 2;
                continue;
            }
            if (!in_class && (esc == 'z' || esc == 'Z')) {
                *dst++ = '$';
                src += 2;
                continue;
            }
            /* Ruby control-character escapes that POSIX doesn't interpret (it
             * would treat \n as a literal 'n'). Emit the actual byte, in or out
             * of a character class. This is what makes the stdlib path types,
             * e.g. Stdlib::Unixpath = Pattern[/\A\/([^\n\/\0]+\/*)*\z/], match a
             * real path containing the letter 'n' (…/keyrings). */
            if (esc == 'n') { *dst++ = '\n'; src += 2; continue; }
            if (esc == 't') { *dst++ = '\t'; src += 2; continue; }
            if (esc == 'r') { *dst++ = '\r'; src += 2; continue; }
            if (esc == 'f') { *dst++ = '\f'; src += 2; continue; }
            if (esc == 'v') { *dst++ = '\v'; src += 2; continue; }
            if (esc == '0' && !(src[2] >= '0' && src[2] <= '7')) {
                /* \0 = NUL. A NUL can't appear in the C strings we match and
                 * would truncate the pattern buffer, so just drop it from the
                 * (character-class) pattern. */
                src += 2;
                continue;
            }
            if (in_class) {
                if (esc == '-') {
                    /* Defer literal dash to end of class */
                    literal_dash = true;
                    src += 2;
                    continue;
                }
                if (esc == '.' || esc == '+' || esc == '?' || esc == '*' ||
                    esc == '(' || esc == ')' || esc == '|' || esc == '{' ||
                    esc == '}' || esc == '$' || esc == '^' || esc == '/') {
                    /* Redundant escape in char class — emit bare char */
                    *dst++ = esc;
                    src += 2;
                    continue;
                }
            }
            /* Preserve other escapes */
            *dst++ = *src++;
            *dst++ = *src++;
            continue;
        }

        *dst++ = *src++;
    }
    *dst = '\0';

    int ret = regcomp(rx, buf, cflags | extra_flags);
    puppet_free(buf);
    return ret;
}

/* Forward declarations for node definition management */
static int puppet_register_node_def(puppet_env_t *env, puppet_stmt_t *node_def);
static puppet_stmt_t *puppet_find_matching_node(puppet_env_t *env, const char *certname);
void puppet_exec_nodes_parallel(puppet_env_t *env, size_t node_count);
static void puppet_exec_node_for_certname(puppet_stmt_t *node_stmt, const char *certname, puppet_env_t *env);
static void puppet_exec_deferred_define(puppet_deferred_define_t *deferred, puppet_env_t *env);
static bool puppet_type_is_known(puppet_env_t *env, const char *type_lower);

#include <dirent.h>
#include <sys/stat.h>

/* ---------------------------------------------------------------------
 * Known resource type check: builtin list + Ruby types scan + defines.
 * Used to catch typos like 'fiel' / 'packge' at catalog time.
 * --------------------------------------------------------------------- */

/* Puppet built-in resource types that don't have a module/Ruby file
 * and that the interpreter must accept without any autoload. Kept in
 * lowercase; all lookups normalize. */
static const char *const puppet_builtin_types[] = {
    "augeas", "class", "component", "computer", "cron", "exec",
    "file", "filebucket", "file_line", "group", "host", "k5login",
    "macauthorization", "mailalias", "maillist", "mcx", "mount",
    "nagios_command", "nagios_contact", "nagios_contactgroup",
    "nagios_host", "nagios_hostdependency", "nagios_hostescalation",
    "nagios_hostextinfo", "nagios_hostgroup", "nagios_service",
    "nagios_servicedependency", "nagios_serviceescalation",
    "nagios_serviceextinfo", "nagios_servicegroup",
    "nagios_timeperiod", "node", "notify", "package", "resources",
    "router", "schedule", "scheduled_task", "selboolean",
    "selmodule", "service", "ssh_authorized_key",
    "sshkey", "stage", "tidy", "user", "vlan", "yumrepo", "zfs",
    "zone", "zpool",
    NULL
};

static bool puppet_type_is_builtin(const char *type_lower) {
    for (size_t i = 0; puppet_builtin_types[i]; i++) {
        if (strcmp(puppet_builtin_types[i], type_lower) == 0) return true;
    }
    return false;
}

/* Scan a lib/puppet/type directory for Ruby type files and add each
 * Puppet::Type.newtype(:name) to env->prog->ruby_types. */
static void puppet_scan_ruby_types_dir(puppet_env_t *env, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        size_t n = strlen(ent->d_name);
        if (n < 4 || strcmp(ent->d_name + n - 3, ".rb") != 0) continue;
        char path[2048];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        /* Open first, then fstat — avoids the TOCTOU window between
         * stat() and fopen() (CodeQL: cpp/toctou-race-condition). */
        FILE *f = fopen(path, "r");
        if (!f) continue;
        struct stat st;
        if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
            fclose(f);
            continue;
        }

        /* Item 3: warn on the Ruby keyword-argument shorthand `def foo(arg:, …)`
         * (bare `key:` followed by ',' or ')', no default) — valid on Ruby 3.1+
         * but unparseable on the legacy puppetserver Ruby. Separate full-file
         * pass (the type-name loop below breaks early), then rewind. Plain
         * `key: value` keeps a value so it doesn't match; `:symbol` is colon-first. */
        {
            char sline[2048];
            int ln = 0;
            while (fgets(sline, sizeof(sline), f)) {
                ln++;
                if (!strstr(sline, "def ")) continue;
                const char *q = sline;
                while (*q) {
                    if (isalpha((unsigned char)*q) || *q == '_') {
                        const char *s = q;
                        while (isalnum((unsigned char)*q) || *q == '_') q++;
                        if (*q == ':' && q[1] != ':') {
                            const char *r = q + 1;
                            while (*r == ' ' || *r == '\t') r++;
                            if (*r == ',' || *r == ')') {
                                int idlen = (int)(q - s);
                                char idn[128];
                                snprintf(idn, sizeof(idn), "%.*s",
                                         idlen < 127 ? idlen : 127, s);
                                puppet_location_t loc = { path, ln, 0 };
                                puppet_warning_at(loc,
                                    "Ruby keyword-argument shorthand '%s:' in def is not supported on the legacy puppetserver Ruby; give it a default value",
                                    idn);
                                break;  /* one warning per line */
                            }
                        }
                    } else {
                        q++;
                    }
                }
            }
            rewind(f);
        }

        char line[2048];
        bool in_resource_api = false;
        const char *type_start = NULL;
        size_t type_len = 0;
        char ra_buf[256];
        while (fgets(line, sizeof(line), f)) {
            /* Legacy: Puppet::Type.newtype(:name) on a single line. */
            const char *p = strstr(line, "Puppet::Type.newtype");
            if (p) {
                p = strchr(p, '(');
                if (!p) continue;
                p++;
                while (*p == ' ' || *p == '\t') p++;
                if (*p != ':') continue;
                p++;
                const char *start = p;
                while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
                if (p > start) {
                    type_start = start;
                    type_len = (size_t)(p - start);
                }
                break;
            }
            /* Modern Resource API:
             *   Puppet::ResourceApi.register_type(
             *     name: 'firewall',
             *     ...
             *   )
             * The opening call and the `name:` key are on separate
             * lines. Set a flag when we see register_type, then look
             * for `name:` in subsequent lines. */
            if (strstr(line, "Puppet::ResourceApi.register_type")) {
                in_resource_api = true;
            }
            if (in_resource_api) {
                const char *np = strstr(line, "name:");
                if (np) {
                    np += 5;
                    while (*np == ' ' || *np == '\t') np++;
                    char quote = *np;
                    if (quote == '\'' || quote == '"') {
                        np++;
                        const char *start = np;
                        while (*np && *np != quote) np++;
                        if (np > start && (size_t)(np - start) < sizeof(ra_buf)) {
                            type_len = (size_t)(np - start);
                            memcpy(ra_buf, start, type_len);
                            ra_buf[type_len] = '\0';
                            type_start = ra_buf;
                            break;
                        }
                    }
                }
            }
        }
        if (type_start && type_len > 0) {
            char *name = puppet_malloc(type_len + 1);
            for (size_t i = 0; i < type_len; i++) name[i] = tolower((unsigned char)type_start[i]);
            name[type_len] = '\0';
            /* Store with true marker. Duplicates just overwrite. */
            puppet_value_t *marker = puppet_value_create_bool(true);
            puppet_hash_set(env->prog->ruby_types, name, type_len, marker);
            puppet_free(name);
        }
        fclose(f);
    }
    closedir(d);
}

/* For each module dir under modulepath, scan MOD/lib/puppet/type/ */
static void puppet_scan_ruby_types_modulepath(puppet_env_t *env, const char *mp) {
    if (!mp || !*mp) return;
    const char *p = mp;
    while (p && *p) {
        const char *end = strchr(p, ':');
        size_t seg_len = end ? (size_t)(end - p) : strlen(p);
        if (seg_len > 0 && seg_len < 1024) {
            char seg[1024];
            memcpy(seg, p, seg_len); seg[seg_len] = '\0';
            DIR *d = opendir(seg);
            if (d) {
                struct dirent *ent;
                while ((ent = readdir(d))) {
                    if (ent->d_name[0] == '.') continue;
                    char types_dir[2048];
                    snprintf(types_dir, sizeof(types_dir),
                             "%s/%s/lib/puppet/type", seg, ent->d_name);
                    puppet_scan_ruby_types_dir(env, types_dir);
                }
                closedir(d);
            }
        }
        if (!end) break;
        p = end + 1;
    }
}

static void puppet_init_ruby_types(puppet_env_t *env) {
    if (!env || !env->prog) return;
    /* Fast path: already initialised. */
    if (env->prog->ruby_types_initialized) return;
    /* Slow path: take the registry mutex and re-check, then scan.
     * Without the lock, two parallel workers could both see
     * ruby_types_initialized=false and race on populating the same
     * shared hash, leaving it partially filled or corrupt. */
    pthread_mutex_lock(&env->prog->reg_mutex);
    if (!env->prog->ruby_types_initialized) {
        if (env->prog->loader && env->prog->loader->modules_path) {
            puppet_scan_ruby_types_modulepath(env, env->prog->loader->modules_path);
        }
        env->prog->ruby_types_initialized = true;
    }
    pthread_mutex_unlock(&env->prog->reg_mutex);
}

static bool puppet_type_is_known(puppet_env_t *env, const char *type_lower) {
    if (!type_lower || !*type_lower) return false;
    /* 1. Built-in? */
    if (puppet_type_is_builtin(type_lower)) return true;
    /* 2. User-defined? (check both original and lowercase in case of
     *    case-insensitive registration in define_types) */
    if (env && env->define_types) {
        if (puppet_hash_get(env->define_types, type_lower, strlen(type_lower))) return true;
    }
    /* 3. Ruby type under modulepath (lazy init)? */
    if (env) {
        puppet_init_ruby_types(env);
        if (env->prog->ruby_types &&
            puppet_hash_get(env->prog->ruby_types, type_lower, strlen(type_lower))) {
            return true;
        }
    }
    return false;
}

/**
 * Automatic Parameter Lookup (APL) for class parameters.
 * Looks up class_name::param_name in Hiera data providers and module-specific data.
 * Returns the found value or NULL if not found.
 */
puppet_value_t *puppet_apl_lookup(const char *class_name, const char *param_name, puppet_env_t *env) {
    if (!class_name || !param_name || !env) return NULL;

    /* Build the lookup key: classname::paramname */
    size_t key_len = strlen(class_name) + 2 + strlen(param_name) + 1;
    char *key = puppet_malloc(key_len);
    snprintf(key, key_len, "%s::%s", class_name, param_name);

    /* Look up in data providers (Hiera) */
    puppet_value_t *result = NULL;
    for (size_t i = 0; i < env->prog->data_provider_count; i++) {
        puppet_data_provider_t *provider = env->prog->data_providers[i];
        if (provider && provider->lookup) {
            result = provider->lookup(key, env, provider->data);
            if (result) {
                puppet_debug("APL: Found %s via provider", key);
                break;
            }
        }
    }

    /* If not found, try module-specific hieradata files */
    if (!result) {
        /* Extract module name (first component of class_name) */
        const char *sep = strchr(class_name, ':');
        char *module_name;
        if (sep) {
            size_t len = sep - class_name;
            module_name = puppet_malloc(len + 1);
            strncpy(module_name, class_name, len);
            module_name[len] = '\0';
        } else {
            module_name = puppet_strdup(class_name);
        }

        /* Get environment name from scope (try jbossenv, then env).
         * Use puppet_variable_lookup_chain so we cross class/node boundaries
         * and reach the top scope where these variables are typically set. */
        const char *env_name = NULL;
        puppet_value_t *env_val = puppet_variable_lookup_chain(env, "jbossenv");
        if (!env_val) env_val = puppet_variable_lookup_chain(env, "::jbossenv");
        if (env_val && env_val->type == PUPPET_VALUE_STRING) {
            env_name = env_val->data.string.data;
        }
        if (!env_name) {
            env_val = puppet_variable_lookup_chain(env, "environment");
            if (!env_val) env_val = puppet_variable_lookup_chain(env, "::environment");
            if (env_val && env_val->type == PUPPET_VALUE_STRING) {
                env_name = env_val->data.string.data;
            }
        }

        /* Try various hieradata paths - including preprod-relative paths */
        char path[1024];
        const char *hieradata_dirs[] = {
            "hieradata", "data", "hieralocal", "hieradata/local",
            "preprod/hieradata", "preprod/hieralocal", "preprod/hieradata/local"
        };

        for (size_t i = 0; i < sizeof(hieradata_dirs)/sizeof(hieradata_dirs[0]) && !result; i++) {
            /* Try module/env.yaml */
            if (env_name) {
                snprintf(path, sizeof(path), "%s/%s/%s.yaml", hieradata_dirs[i], module_name, env_name);
                puppet_value_t *data = puppet_hiera_load_yaml(path);
                if (data && data->type == PUPPET_VALUE_HASH) {
                    result = puppet_hash_get(data->data.hash, key, strlen(key));
                    if (result) {
                        result = puppet_value_copy(result);
                        puppet_debug("APL: Found %s in %s", key, path);
                    }
                    puppet_value_destroy(data);
                }
            }

            /* Try module/global.yaml */
            if (!result) {
                snprintf(path, sizeof(path), "%s/%s/global.yaml", hieradata_dirs[i], module_name);
                puppet_value_t *data = puppet_hiera_load_yaml(path);
                if (data && data->type == PUPPET_VALUE_HASH) {
                    result = puppet_hash_get(data->data.hash, key, strlen(key));
                    if (result) {
                        result = puppet_value_copy(result);
                        puppet_debug("APL: Found %s in %s", key, path);
                    }
                    puppet_value_destroy(data);
                }
            }
        }

        /* Item 33: module data layer — modules/<mod>/hiera.yaml + data/.
         * Lowest priority, after global providers and the site fallback
         * (real Puppet's global → environment → module lookup order). */
        if (!result) {
            const char *mod = module_name;
            if (strncmp(mod, "::", 2) == 0) mod += 2;
            result = puppet_hiera_module_lookup(env, mod, key);
            if (result) {
                puppet_debug("APL: Found %s via module-layer hiera", key);
            }
        }

        puppet_free(module_name);
    }

    puppet_free(key);
    return result;
}

/**
 * Apply current scope tags to a resource in the catalog.
 * Called after adding a resource to copy tags from env->current_tags.
 */
void puppet_apply_current_tags(puppet_env_t *env, const char *type, const char *title) {
    if (!env || !env->catalog || !env->current_tags || !type || !title) return;
    if (env->current_tags->type != PUPPET_VALUE_ARRAY) return;

    puppet_array_t *tags = env->current_tags->data.array;
    if (!tags || tags->count == 0) return;

    /* Build array of tag strings */
    const char **tag_strs = puppet_calloc(tags->count, sizeof(char *));
    size_t tag_count = 0;

    for (size_t i = 0; i < tags->count; i++) {
        puppet_value_t *tag = tags->items[i];
        if (tag && tag->type == PUPPET_VALUE_STRING && tag->data.string.data) {
            tag_strs[tag_count++] = tag->data.string.data;
        }
    }

    if (tag_count > 0) {
        puppet_catalog_add_resource_tags(env->catalog, type, title, tag_strs, tag_count);
    }

    puppet_free(tag_strs);
}

// Helper function to convert value to string
/* Forward declaration for recursive use */
static void puppet_value_to_string_buffer(puppet_value_t *value, char *buf, size_t *pos, size_t max_len);

static const char *puppet_value_to_string(puppet_value_t *value) {
    if (!value) return "";

    static __thread char buffer[4096];  // Thread-local buffer for thread safety

    switch (value->type) {
        case PUPPET_VALUE_STRING:
            return value->data.string.data;
        case PUPPET_VALUE_NUMBER:
            snprintf(buffer, sizeof(buffer), "%g", value->data.number);
            return buffer;
        case PUPPET_VALUE_BOOL:
            return value->data.boolean ? "true" : "false";
        case PUPPET_VALUE_UNDEF:
            return "";
        case PUPPET_VALUE_ARRAY:
        case PUPPET_VALUE_HASH: {
            size_t pos = 0;
            puppet_value_to_string_buffer(value, buffer, &pos, sizeof(buffer) - 1);
            buffer[pos] = '\0';
            return buffer;
        }
        default:
            return "";
    }
}

/* Helper function to build string representation of complex values */
static void puppet_value_to_string_buffer(puppet_value_t *value, char *buf, size_t *pos, size_t max_len) {
    if (!value || *pos >= max_len) return;

    switch (value->type) {
        case PUPPET_VALUE_STRING: {
            size_t len = value->data.string.len;
            if (*pos + len > max_len) len = max_len - *pos;
            memcpy(buf + *pos, value->data.string.data, len);
            *pos += len;
            break;
        }
        case PUPPET_VALUE_NUMBER: {
            int written = snprintf(buf + *pos, max_len - *pos, "%g", value->data.number);
            if (written > 0) *pos += (size_t)written;
            break;
        }
        case PUPPET_VALUE_BOOL:
            if (value->data.boolean) {
                if (*pos + 4 <= max_len) { memcpy(buf + *pos, "true", 4); *pos += 4; }
            } else {
                if (*pos + 5 <= max_len) { memcpy(buf + *pos, "false", 5); *pos += 5; }
            }
            break;
        case PUPPET_VALUE_UNDEF:
            break;
        case PUPPET_VALUE_ARRAY: {
            if (*pos < max_len) buf[(*pos)++] = '[';
            if (value->data.array) {
                for (size_t i = 0; i < value->data.array->count && *pos < max_len; i++) {
                    if (i > 0) {
                        if (*pos + 2 <= max_len) { memcpy(buf + *pos, ", ", 2); *pos += 2; }
                    }
                    puppet_value_to_string_buffer(value->data.array->items[i], buf, pos, max_len);
                }
            }
            if (*pos < max_len) buf[(*pos)++] = ']';
            break;
        }
        case PUPPET_VALUE_HASH: {
            if (*pos < max_len) buf[(*pos)++] = '{';
            if (value->data.hash) {
                bool first = true;
                for (size_t i = 0; i < value->data.hash->bucket_count && *pos < max_len; i++) {
                    puppet_hash_entry_t *entry = value->data.hash->buckets[i];
                    while (entry && *pos < max_len) {
                        if (!first) {
                            if (*pos + 2 <= max_len) { memcpy(buf + *pos, ", ", 2); *pos += 2; }
                        }
                        first = false;
                        /* Key */
                        size_t key_len = strlen(entry->key.data);
                        if (*pos + key_len > max_len) key_len = max_len - *pos;
                        memcpy(buf + *pos, entry->key.data, key_len);
                        *pos += key_len;
                        /* Arrow */
                        if (*pos + 4 <= max_len) { memcpy(buf + *pos, " => ", 4); *pos += 4; }
                        /* Value */
                        puppet_value_to_string_buffer(entry->value, buf, pos, max_len);
                        entry = entry->next;
                    }
                }
            }
            if (*pos < max_len) buf[(*pos)++] = '}';
            break;
        }
        default:
            break;
    }
}

puppet_env_t *puppet_env_create(void) {
    puppet_env_t *env = puppet_calloc(1, sizeof(puppet_env_t));

    /* Allocate the shared program-state container. The "creator" env
     * owns it; worker envs (clone_for_node) just borrow the pointer.
     * As individual fields migrate from env->X to env->prog->X, the
     * creator initialises them here and workers see them through
     * the shared pointer. */
    env->prog = puppet_program_state_create();
    env->owns_prog = true;

    env->global_scope = puppet_scope_create(NULL, "global");
    env->current_scope = env->global_scope;
    env->stack_capacity = 16;
    env->scope_stack = puppet_calloc(env->stack_capacity, sizeof(puppet_scope_t*));
    env->stack_depth = 0;
    env->prog->loader = NULL;  /* Loader is optional, set separately */
    env->node_name = NULL;  /* No node filtering by default */
    env->execute_all_nodes = false;
    env->node_matched = false;
    env->default_node = NULL;
    
    /* Initialize enhanced variable system */
    env->prog->data_provider_capacity = 4;
    env->prog->data_providers = puppet_calloc(env->prog->data_provider_capacity, sizeof(puppet_data_provider_t*));
    env->prog->data_provider_count = 0;
    env->node_scope = puppet_scope_create(env->global_scope, "node");
    env->class_scope = NULL;  /* Set when entering class context */
    
    /* Initialize class definition registry */
    env->class_def_capacity = 4;
    env->class_definitions = puppet_calloc(env->class_def_capacity, sizeof(puppet_stmt_t*));
    env->class_def_count = 0;

    /* Initialize node definition registry (for facts_db iteration mode) */
    env->node_def_capacity = 4;
    env->node_definitions = puppet_calloc(env->node_def_capacity, sizeof(puppet_stmt_t*));
    env->node_def_count = 0;
    env->defer_node_execution = false;

    /* Initialize class scope registry for $class::var lookups */
    env->class_scopes = puppet_calloc(1, sizeof(puppet_hash_t));
    env->class_scopes->bucket_count = 32;
    env->class_scopes->buckets = puppet_calloc(env->class_scopes->bucket_count, sizeof(puppet_hash_entry_t*));

    /* Per-node set of modules whose metadata.json puppet requirement has been
     * checked (dedups the Puppet 8 incompatibility error to once per node). */
    env->modules_p8_checked = puppet_calloc(1, sizeof(puppet_hash_t));
    env->modules_p8_checked->bucket_count = 16;
    env->modules_p8_checked->buckets = puppet_calloc(env->modules_p8_checked->bucket_count, sizeof(puppet_hash_entry_t*));

    /* Initialize resource-style class declarations tracking */
    env->class_resource_decls = puppet_calloc(1, sizeof(puppet_hash_t));
    env->class_resource_decls->bucket_count = 32;
    env->class_resource_decls->buckets = puppet_calloc(env->class_resource_decls->bucket_count, sizeof(puppet_hash_entry_t*));

    /* Initialize facts database */
    env->prog->facts_db = NULL;
    
    /* Initialize resource catalog for duplicate detection */
    env->resource_catalog = puppet_calloc(1, sizeof(puppet_hash_t));
    env->resource_catalog->bucket_count = 64;  /* Start with reasonable size */
    env->resource_catalog->buckets = puppet_calloc(env->resource_catalog->bucket_count, sizeof(puppet_hash_entry_t*));

    /* Initialize class re-execution tracking */
    env->classes_being_reexecuted = puppet_calloc(1, sizeof(puppet_hash_t));
    env->classes_being_reexecuted->bucket_count = 32;
    env->classes_being_reexecuted->buckets = puppet_calloc(env->classes_being_reexecuted->bucket_count, sizeof(puppet_hash_entry_t*));

    /* Initialize virtual resources storage */
    env->virtual_resources = puppet_calloc(1, sizeof(puppet_hash_t));
    env->virtual_resources->bucket_count = 64;
    env->virtual_resources->buckets = puppet_calloc(env->virtual_resources->bucket_count, sizeof(puppet_hash_entry_t*));

    /* Ruby types registry — populated lazily on first unknown-type check */
    env->prog->ruby_types = puppet_calloc(1, sizeof(puppet_hash_t));
    env->prog->ruby_types->bucket_count = 64;
    env->prog->ruby_types->buckets = puppet_calloc(env->prog->ruby_types->bucket_count, sizeof(puppet_hash_entry_t*));
    env->prog->ruby_types_initialized = false;

    /* Initialize defined types registry */
    env->define_types = puppet_calloc(1, sizeof(puppet_hash_t));
    env->define_types->bucket_count = 64;
    env->define_types->buckets = puppet_calloc(env->define_types->bucket_count, sizeof(puppet_hash_entry_t*));

    /* Initialize user-defined function registry */
    env->user_functions = puppet_calloc(1, sizeof(puppet_hash_t));
    env->user_functions->bucket_count = 64;
    env->user_functions->buckets = puppet_calloc(env->user_functions->bucket_count, sizeof(puppet_hash_entry_t*));

    /* Initialize user-defined type-alias registry (type Foo::Bar = <type>) */
    env->type_aliases = puppet_calloc(1, sizeof(puppet_hash_t));
    env->type_aliases->bucket_count = 64;
    env->type_aliases->buckets = puppet_calloc(env->type_aliases->bucket_count, sizeof(puppet_hash_entry_t*));

    /* Initialize template output */
    env->template_output_target = NULL;
    env->template_output_found = false;
    
    /* Initialize core function support */
    env->defined_resources = puppet_calloc(1, sizeof(puppet_hash_t));
    env->defined_resources->bucket_count = 64;
    env->defined_resources->buckets = puppet_calloc(env->defined_resources->bucket_count, sizeof(puppet_hash_entry_t*));
    env->current_tags = NULL;  /* Initialized when first tag is added */
    env->compilation_failed = false;
    env->failure_message = NULL;

    /* Initialize output control */
    env->prog->verbose = puppet_verbose;  /* Inherit from global flag */

    /* Initialize catalog building (disabled by default) */
    env->catalog = NULL;
    env->build_catalog = false;

    /* Initialize CI validation tracking */
    env->nodes_processed = 0;
    env->nodes_failed = 0;
    env->nodes_skipped_regex = 0;
    env->errors_count = 0;
    env->warnings_count = 0;
    env->current_node_certname = NULL;
    env->current_node_failed = false;
    env->stop_on_error = false;

    /* Initialize parallel processing */
    env->parallel_nodes = false;
    env->prog->skip_erb = false;
    env->stats_mutex = NULL;  /* Allocated only when needed */

    /* Initialize deferred define execution */
    env->deferred_defines = NULL;
    env->deferred_define_count = 0;
    env->deferred_define_capacity = 0;

    /* Initialize pending realizes */
    env->pending_realizes = puppet_calloc(1, sizeof(puppet_hash_t));
    env->pending_realizes->bucket_count = 16;
    env->pending_realizes->buckets = puppet_calloc(16, sizeof(puppet_hash_entry_t*));

    /* Initialize exported resources (PuppetDB integration) */
    env->puppetdb = NULL;
    env->exported_resources = puppet_calloc(1, sizeof(puppet_hash_t));
    env->exported_resources->bucket_count = 64;
    env->exported_resources->buckets = puppet_calloc(env->exported_resources->bucket_count, sizeof(puppet_hash_entry_t*));

    /* Register Hiera data provider */
    puppet_hiera_register_provider(env, "data");

    return env;
}

void puppet_env_set_verbose(puppet_env_t *env, bool verbose) {
    if (env) {
        env->prog->verbose = verbose;
    }
    puppet_verbose = verbose;  /* Also set global flag */
}

void puppet_env_set_strict_erb(puppet_env_t *env, bool strict_erb) {
    if (env) {
        env->prog->strict_erb = strict_erb;
    }
}

void puppet_env_set_puppetdb(puppet_env_t *env, puppetdb_t *pdb) {
    if (env) {
        env->puppetdb = pdb;
    }
}

void puppet_env_destroy(puppet_env_t *env) {
    if (!env) return;

    if (env->prog->deadcode) {
        puppet_deadcode_destroy(env->prog->deadcode);
        env->prog->deadcode = NULL;
    }

    // Clean up scope stack
    while (env->stack_depth > 0) {
        puppet_scope_pop(env);
    }
    
    // Clean up scopes
    puppet_scope_destroy(env->global_scope);
    if (env->node_scope) puppet_scope_destroy(env->node_scope);
    if (env->class_scope) puppet_scope_destroy(env->class_scope);
    
    // Clean up data providers
    for (size_t i = 0; i < env->prog->data_provider_count; i++) {
        puppet_data_provider_t *provider = env->prog->data_providers[i];
        if (provider && provider->cleanup) {
            provider->cleanup(provider->data);
        }
        puppet_free(provider->name);
        puppet_free(provider);
    }
    puppet_free(env->prog->data_providers);
    
    // Clean up class definition registry (don't destroy statements, they're owned by AST)
    puppet_free(env->class_definitions);

    // Clean up node definition registry (don't destroy statements, they're owned by AST)
    puppet_free(env->node_definitions);

    // Clean up class scopes registry
    if (env->class_scopes) {
        for (size_t i = 0; i < env->class_scopes->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->class_scopes->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                // Destroy the stored scope
                puppet_scope_destroy((puppet_scope_t *)entry->value);
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->class_scopes->buckets);
        puppet_free(env->class_scopes);
    }

    // Clean up per-node module-metadata-checked set
    if (env->modules_p8_checked) {
        for (size_t i = 0; i < env->modules_p8_checked->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->modules_p8_checked->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->modules_p8_checked->buckets);
        puppet_free(env->modules_p8_checked);
    }

    // Clean up resource-style class declarations
    if (env->class_resource_decls) {
        for (size_t i = 0; i < env->class_resource_decls->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->class_resource_decls->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->class_resource_decls->buckets);
        puppet_free(env->class_resource_decls);
    }

    // Clean up facts database
    if (env->prog->facts_db) {
        puppet_facts_db_destroy(env->prog->facts_db);
    }
    
    // Clean up resource catalog
    if (env->resource_catalog) {
        /* Clean up resource catalog */
        for (size_t i = 0; i < env->resource_catalog->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->resource_catalog->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->resource_catalog->buckets);
        puppet_free(env->resource_catalog);
    }

    // Clean up virtual resources
    if (env->virtual_resources) {
        for (size_t i = 0; i < env->virtual_resources->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->virtual_resources->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                /* Value stores a puppet_virtual_resource_t* via type-punning */
                if (entry->value && entry->value->data.string.data) {
                    puppet_virtual_resource_t *vres = (puppet_virtual_resource_t *)entry->value->data.string.data;
                    puppet_free(vres->type);
                    puppet_free(vres->title);
                    for (size_t j = 0; j < vres->attr_count; j++) {
                        puppet_free(vres->attrs[j].name);
                        puppet_value_destroy(vres->attrs[j].value);
                    }
                    puppet_free(vres->attrs);
                    puppet_free(vres);
                }
                puppet_free(entry->value);
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->virtual_resources->buckets);
        puppet_free(env->virtual_resources);
    }

    // Clean up exported resources cache
    if (env->exported_resources) {
        for (size_t i = 0; i < env->exported_resources->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->exported_resources->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                /* Value stores a puppet_virtual_resource_t* via type-punning */
                if (entry->value && entry->value->data.string.data) {
                    puppet_virtual_resource_t *vres = (puppet_virtual_resource_t *)entry->value->data.string.data;
                    puppet_free(vres->type);
                    puppet_free(vres->title);
                    for (size_t j = 0; j < vres->attr_count; j++) {
                        puppet_free(vres->attrs[j].name);
                        puppet_value_destroy(vres->attrs[j].value);
                    }
                    puppet_free(vres->attrs);
                    puppet_free(vres);
                }
                puppet_free(entry->value);
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->exported_resources->buckets);
        puppet_free(env->exported_resources);
    }

    /* Clean up deferred defines array and each entry's owned data */
    if (env->deferred_defines) {
        for (size_t i = 0; i < env->deferred_define_count; i++) {
            puppet_free(env->deferred_defines[i].type_name);
            puppet_free(env->deferred_defines[i].title);
            puppet_free(env->deferred_defines[i].resource_id);
            if (env->deferred_defines[i].override_attrs) {
                for (size_t b = 0; b < env->deferred_defines[i].override_attrs->bucket_count; b++) {
                    puppet_hash_entry_t *entry = env->deferred_defines[i].override_attrs->buckets[b];
                    while (entry) {
                        puppet_hash_entry_t *next = entry->next;
                        puppet_free(entry->key.data);
                        puppet_value_destroy(entry->value);
                        puppet_free(entry);
                        entry = next;
                    }
                }
                puppet_free(env->deferred_defines[i].override_attrs->buckets);
                puppet_free(env->deferred_defines[i].override_attrs);
            }
        }
        puppet_free(env->deferred_defines);
    }

    /* Clean up pending realizes */
    if (env->pending_realizes) {
        for (size_t i = 0; i < env->pending_realizes->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->pending_realizes->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->pending_realizes->buckets);
        puppet_free(env->pending_realizes);
    }

    /* Clean up defined types hash */
    if (env->define_types) {
        for (size_t i = 0; i < env->define_types->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->define_types->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                /* Note: value points to AST stmt, don't free it here */
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->define_types->buckets);
        puppet_free(env->define_types);
    }

    /* Clean up user-defined function registry */
    if (env->user_functions) {
        for (size_t i = 0; i < env->user_functions->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->user_functions->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                /* value is a wrapper holding a borrowed AST stmt pointer */
                puppet_free(entry->value);
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->user_functions->buckets);
        puppet_free(env->user_functions);
    }

    /* Clean up type-alias registry (values are owned string wrappers) */
    if (env->type_aliases) {
        for (size_t i = 0; i < env->type_aliases->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->type_aliases->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->type_aliases->buckets);
        puppet_free(env->type_aliases);
    }

    puppet_free(env->scope_stack);
    puppet_free(env->node_name);
    puppet_free(env->template_output_target);
    
    /* Clean up defined resources hash */
    if (env->defined_resources) {
        for (size_t i = 0; i < env->defined_resources->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->defined_resources->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->defined_resources->buckets);
        puppet_free(env->defined_resources);
    }
    
    /* Clean up tags */
    if (env->current_tags) {
        puppet_value_destroy(env->current_tags);
    }
    
    /* Clean up failure message */
    puppet_free(env->failure_message);

    /* Clean up class re-execution tracking */
    if (env->classes_being_reexecuted) {
        for (size_t i = 0; i < env->classes_being_reexecuted->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->classes_being_reexecuted->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->classes_being_reexecuted->buckets);
        puppet_free(env->classes_being_reexecuted);
    }

    /* Clean up current_node_certname if allocated */
    puppet_free(env->current_node_certname);

    /* Destroy catalog if it was never retrieved by the caller */
    if (env->catalog) {
        puppet_catalog_destroy(env->catalog);
    }

    /* Destroy the shared program-state last — earlier cleanup paths
     * still need to read env->prog (e.g. facts_db). Only the original
     * creator env owns it; worker clones leave this NULL via
     * owns_prog=false. */
    if (env->owns_prog && env->prog) {
        puppet_program_state_destroy(env->prog);
        env->prog = NULL;
    }

    puppet_free(env);
}

puppet_scope_t *puppet_scope_create(puppet_scope_t *parent, const char *name) {
    puppet_scope_t *scope = puppet_calloc(1, sizeof(puppet_scope_t));
    scope->parent = parent;
    scope->name = puppet_string_create(name ? name : "");
    scope->variables = puppet_calloc(1, sizeof(puppet_hash_t));
    scope->variables->bucket_count = 16;
    scope->variables->buckets = puppet_calloc(scope->variables->bucket_count, sizeof(puppet_hash_entry_t*));
    return scope;
}

void puppet_scope_destroy(puppet_scope_t *scope) {
    if (!scope) return;
    
    // Clean up variables hash table
    for (size_t i = 0; i < scope->variables->bucket_count; i++) {
        puppet_hash_entry_t *entry = scope->variables->buckets[i];
        while (entry) {
            puppet_hash_entry_t *next = entry->next;
            puppet_string_free(entry->key);
            puppet_value_destroy(entry->value);
            puppet_free(entry);
            entry = next;
        }
    }
    puppet_free(scope->variables->buckets);
    puppet_free(scope->variables);
    puppet_string_free(scope->name);
    puppet_free(scope);
}

void puppet_scope_push(puppet_env_t *env, puppet_scope_t *scope) {
    if (env->stack_depth >= env->stack_capacity) {
        env->stack_capacity *= 2;
        env->scope_stack = puppet_realloc(env->scope_stack, env->stack_capacity * sizeof(puppet_scope_t*));
    }
    
    env->scope_stack[env->stack_depth++] = env->current_scope;
    env->current_scope = scope;
}

puppet_scope_t *puppet_scope_pop(puppet_env_t *env) {
    if (env->stack_depth == 0) {
        return env->current_scope;
    }
    
    puppet_scope_t *old_scope = env->current_scope;
    env->current_scope = env->scope_stack[--env->stack_depth];
    return old_scope;
}

void puppet_env_set_var(puppet_env_t *env, const char *name, puppet_value_t *value) {
    puppet_scope_set_var(env->current_scope, name, value);
}

puppet_value_t *puppet_env_get_var(puppet_env_t *env, const char *name) {
    return puppet_scope_get_var(env->current_scope, name, true);
}

void puppet_scope_set_var(puppet_scope_t *scope, const char *name, puppet_value_t *value) {
    puppet_hash_set(scope->variables, name, strlen(name), value);
}

puppet_value_t *puppet_scope_get_var(puppet_scope_t *scope, const char *name, bool recursive) {
    if (!scope || !scope->variables) return NULL;

    puppet_value_t *value = puppet_hash_get(scope->variables, name, strlen(name));

    if (!value && recursive && scope->parent) {
        return puppet_scope_get_var(scope->parent, name, true);
    }

    return value;
}

/* Forward declaration for type-match helpers defined below */
static bool value_matches_type(puppet_value_t *val, const char *type_name,
                                puppet_expr_t *title_expr, puppet_env_t *env);
static bool value_matches_type_str(puppet_value_t *val, const char *type_str,
                                   puppet_env_t *env);
/* Resolve a named type alias (type X = …). Returns 1 if base is an alias and
 * val matches, 0 if it's an alias and val does not match, -1 if not an alias. */
static int match_type_alias(puppet_value_t *val, const char *base, puppet_env_t *env);
extern puppet_stmt_list_t *puppet_ts_parse_file(const char *filename);
/* Item 30 — opt-in per-tree resource policy (.puppetc-policy.json). */
static void puppet_policy_check_resource(puppet_env_t *env, const char *type,
                                         const char *title, puppet_location_t loc);

/* Guards runaway recursion in user-defined functions (e.g. f() calls f()). */
#define PUPPET_MAX_CALL_DEPTH 256

/*
 * Call a user-defined Puppet function. Arguments are positional: each is
 * evaluated in the caller's scope and bound to the corresponding parameter in
 * a fresh scope (missing trailing params fall back to their default). The
 * return value is the value of the body's last expression. NOTE: argument and
 * return TYPE checking is deliberately not done here — that is item 8.
 */
static puppet_value_t *puppet_call_user_function(puppet_stmt_t *fn_stmt,
                                                 puppet_expr_t *call_expr,
                                                 puppet_env_t *env) {
    const char *fname = fn_stmt->data.function_def.name.data;

    if (env->func_call_depth >= PUPPET_MAX_CALL_DEPTH) {
        puppet_error_at(call_expr->loc,
                        "Maximum function call depth exceeded calling '%s'", fname);
        puppet_env_increment_error(env);
        return puppet_value_create_undef();
    }

    puppet_param_list_t *params = &fn_stmt->data.function_def.params;
    size_t argc = call_expr->data.funcall.args.count;

    if (argc > params->count) {
        puppet_error_at(call_expr->loc,
                        "Function '%s' takes at most %zu argument(s), got %zu",
                        fname, params->count, argc);
        puppet_env_increment_error(env);
        return puppet_value_create_undef();
    }

    /* Evaluate positional arguments in the CALLER's scope. */
    puppet_value_t **argv = NULL;
    if (argc > 0) {
        argv = puppet_calloc(argc, sizeof(puppet_value_t *));
        for (size_t i = 0; i < argc; i++) {
            argv[i] = puppet_eval_expr(call_expr->data.funcall.args.exprs[i], env);
        }
    }

    /* Fresh scope for the body; bind parameters positionally. */
    puppet_scope_t *fn_scope = puppet_scope_create(env->current_scope, fname);
    puppet_scope_push(env, fn_scope);
    env->func_call_depth++;

    for (size_t i = 0; i < params->count; i++) {
        const char *pname = params->params[i].name.data;
        if (!pname) continue;
        puppet_value_t *val;
        bool have_value = true;
        if (i < argc) {
            val = argv[i] ? argv[i] : puppet_value_create_undef();
        } else if (params->params[i].default_value) {
            val = puppet_eval_expr(params->params[i].default_value, env);
        } else {
            puppet_error_at(call_expr->loc,
                            "Function '%s' missing required argument '%s'", fname, pname);
            puppet_env_increment_error(env);
            val = puppet_value_create_undef();
            have_value = false;  /* already reported; don't double-flag as a type error */
        }
        /* Type-check the bound value against the declared type (item 8).
         * Mirrors the class/define parameter check; only typed params are
         * checked, and value_matches_type_str accepts unknown/parametric
         * types silently to avoid false positives. */
        if (have_value && params->params[i].type_expr &&
            !value_matches_type_str(val, params->params[i].type_str.data, env)) {
            char typestr[128];
            snprintf(typestr, sizeof(typestr), "%s",
                     params->params[i].type_str.data ? params->params[i].type_str.data : "?");
            puppet_error_at(call_expr->loc,
                "Function '%s' parameter $%s: expected %s, got incompatible value",
                fname, pname, typestr);
            puppet_env_increment_error(env);
        }
        /* scope takes ownership of val (same as class/define parameter binding) */
        puppet_scope_set_var(fn_scope, pname, val);
    }
    puppet_free(argv);  /* the values are now owned by the scope */

    /* The function's value is its body's last expression. */
    puppet_value_t *result = NULL;
    puppet_stmt_list_t *body = &fn_stmt->data.function_def.body;
    for (size_t i = 0; i < body->count; i++) {
        puppet_stmt_t *s = body->stmts[i];
        bool last = (i + 1 == body->count);
        if (last && s && (s->type == PUPPET_STMT_EXPRESSION ||
                          s->type == PUPPET_STMT_FUNCTION_CALL)) {
            result = puppet_eval_expr(s->data.expr, env);
        } else {
            puppet_exec_stmt(s, env);
        }
    }
    if (!result) result = puppet_value_create_undef();

    env->func_call_depth--;
    puppet_scope_t *popped = puppet_scope_pop(env);
    puppet_scope_destroy(popped);

    return result;
}

puppet_value_t *puppet_eval_expr(puppet_expr_t *expr, puppet_env_t *env) {
    if (!expr) {
        return puppet_value_create_undef();
    }
    
    switch (expr->type) {
        case PUPPET_EXPR_VALUE:
            // Return a copy of the value to avoid double-free issues
            switch (expr->data.value->type) {
                case PUPPET_VALUE_UNDEF:
                    return puppet_value_create_undef();
                case PUPPET_VALUE_BOOL:
                    return puppet_value_create_bool(expr->data.value->data.boolean);
                case PUPPET_VALUE_NUMBER:
                    return puppet_value_create_number(expr->data.value->data.number);
                case PUPPET_VALUE_STRING:
                    return puppet_value_create_string(expr->data.value->data.string.data,
                                                    expr->data.value->data.string.len);
                case PUPPET_VALUE_ARRAY:
                case PUPPET_VALUE_HASH:
                    return puppet_value_copy(expr->data.value);
                case PUPPET_VALUE_REGEXP: {
                    // Create a copy of the regexp value
                    puppet_value_t *val = puppet_calloc(1, sizeof(puppet_value_t));
                    val->type = PUPPET_VALUE_REGEXP;
                    val->data.regexp.len = expr->data.value->data.regexp.len;
                    val->data.regexp.data = puppet_strdup(expr->data.value->data.regexp.data);
                    return val;
                }
                default:
                    return puppet_value_create_undef();
            }
            
        case PUPPET_EXPR_VARIABLE:
            return puppet_eval_variable(expr->data.variable.data, expr->loc, env);
            
        case PUPPET_EXPR_BINOP: {
            /* Intercept type-match: $value =~ TypeRef (e.g. Array, Pattern[/x/]) */
            if ((expr->data.binop.op == PUPPET_OP_MATCH ||
                 expr->data.binop.op == PUPPET_OP_NOT_MATCH) &&
                expr->data.binop.right &&
                expr->data.binop.right->type == PUPPET_EXPR_RESOURCE_REF) {
                puppet_expr_t *rref = expr->data.binop.right;
                const char *tname = rref->data.resource_ref.type.data;
                /* Skip regular resource-ref titles like File['x'], User['y'] etc.
                 * by only matching capitalized builtin type names. A resource ref
                 * with a string title is a regular resource ref. A bare type or a
                 * type[param] is what we handle here. Heuristic: if title is a
                 * string literal value, treat as resource ref; otherwise as type. */
                bool is_type_ref = true;
                if (rref->data.resource_ref.title) {
                    puppet_expr_t *t = rref->data.resource_ref.title;
                    /* A string literal title means regular resource ref */
                    if (t->type == PUPPET_EXPR_VALUE && t->data.value &&
                        t->data.value->type == PUPPET_VALUE_STRING) {
                        is_type_ref = false;
                    }
                }
                if (is_type_ref) {
                    puppet_value_t *lval = puppet_eval_expr(expr->data.binop.left, env);
                    bool matched = value_matches_type(lval, tname,
                                                      rref->data.resource_ref.title, env);
                    if (expr->data.binop.op == PUPPET_OP_NOT_MATCH) matched = !matched;
                    puppet_value_destroy(lval);
                    return puppet_value_create_bool(matched);
                }
            }
            /* and / or evaluate their operands purely for truthiness —
             * propagate the no-warn-on-undef context to them. */
            bool boolean_op = (expr->data.binop.op == PUPPET_OP_AND ||
                               expr->data.binop.op == PUPPET_OP_OR);
            if (boolean_op) env->in_truthiness_check++;
            puppet_value_t *left = puppet_eval_expr(expr->data.binop.left, env);
            puppet_value_t *right = puppet_eval_expr(expr->data.binop.right, env);
            if (boolean_op) env->in_truthiness_check--;
            puppet_value_t *result = puppet_eval_binop(expr->data.binop.op, left, right);
            puppet_value_destroy(left);
            puppet_value_destroy(right);
            return result;
        }

        case PUPPET_EXPR_UNOP: {
            /* `!x` reads x for truthiness only. */
            bool boolean_op = (expr->data.unop.op == PUPPET_UNOP_NOT);
            if (boolean_op) env->in_truthiness_check++;
            puppet_value_t *operand = puppet_eval_expr(expr->data.unop.expr, env);
            if (boolean_op) env->in_truthiness_check--;
            puppet_value_t *result = puppet_eval_unop(expr->data.unop.op, operand);
            puppet_value_destroy(operand);
            return result;
        }
            
        case PUPPET_EXPR_FUNCALL: {
            // Handle built-in functions
            const char *func_name = expr->data.funcall.name.data;

            // Guard against NULL function names
            if (!func_name) {
                puppet_error_at(expr->loc, "Function call with NULL name");
                return puppet_value_create_undef();
            }

            puppet_deadcode_mark_function_used(env->prog->deadcode, func_name);

            // Track template/epp target path for dead-code mode
            if (env->prog->deadcode && expr->data.funcall.args.count >= 1 &&
                (strcmp(func_name, "template") == 0 ||
                 strcmp(func_name, "epp") == 0 ||
                 strcmp(func_name, "stdlib::deferrable_epp") == 0)) {
                puppet_value_t *pv = puppet_eval_expr(expr->data.funcall.args.exprs[0], env);
                if (pv && pv->type == PUPPET_VALUE_STRING && pv->data.string.data) {
                    puppet_deadcode_mark_template_used(env->prog->deadcode, pv->data.string.data);
                }
                if (pv) puppet_value_destroy(pv);
            }

            // Template function
            if (strcmp(func_name, "template") == 0) {
                return puppet_func_template(&expr->data.funcall.args, env);
            }
            // EPP template function
            else if (strcmp(func_name, "epp") == 0) {
                return puppet_func_epp(&expr->data.funcall.args, env);
            }
            // stdlib::deferrable_epp - same as epp() for compile-time rendering
            else if (strcmp(func_name, "stdlib::deferrable_epp") == 0) {
                return puppet_func_epp(&expr->data.funcall.args, env);
            }
            // Sensitive() - wraps value to mark as sensitive (pass-through for compiler)
            else if (strcmp(func_name, "Sensitive") == 0) {
                if (expr->data.funcall.args.count >= 1) {
                    return puppet_eval_expr(expr->data.funcall.args.exprs[0], env);
                }
                return puppet_value_create_undef();
            }
            // Deferred() - creates a deferred function call for agent-side evaluation
            else if (strcmp(func_name, "Deferred") == 0) {
                if (expr->data.funcall.args.count < 1) {
                    puppet_error_at(expr->loc, "Deferred() requires at least 1 argument (function name)");
                    return puppet_value_create_undef();
                }
                /* First argument is the function name */
                puppet_value_t *name_val = puppet_eval_expr(expr->data.funcall.args.exprs[0], env);
                if (name_val->type != PUPPET_VALUE_STRING) {
                    puppet_error_at(expr->loc, "Deferred() first argument must be a string");
                    puppet_value_destroy(name_val);
                    return puppet_value_create_undef();
                }
                const char *deferred_func_name = name_val->data.string.data;

                /* Second argument (optional) is an array of arguments */
                puppet_array_t *args = NULL;
                if (expr->data.funcall.args.count >= 2) {
                    puppet_value_t *args_val = puppet_eval_expr(expr->data.funcall.args.exprs[1], env);
                    if (args_val->type == PUPPET_VALUE_ARRAY) {
                        /* Copy the array for the deferred value */
                        args = puppet_calloc(1, sizeof(puppet_array_t));
                        args->capacity = args_val->data.array->count;
                        args->count = args_val->data.array->count;
                        args->items = puppet_calloc(args->count, sizeof(puppet_value_t *));
                        for (size_t i = 0; i < args->count; i++) {
                            args->items[i] = puppet_value_copy(args_val->data.array->items[i]);
                        }
                    }
                    puppet_value_destroy(args_val);
                }

                puppet_value_t *result = puppet_value_create_deferred(deferred_func_name, args);
                puppet_value_destroy(name_val);
                return result;
            }
            // Core logging functions
            else if (strcmp(func_name, "fail") == 0) {
                return puppet_func_fail(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "notice") == 0) {
                return puppet_func_notice(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "info") == 0) {
                return puppet_func_info(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "warning") == 0) {
                return puppet_func_warning(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "err") == 0) {
                return puppet_func_err(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "crit") == 0) {
                return puppet_func_crit(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "debug") == 0) {
                return puppet_func_debug(&expr->data.funcall.args, env);
            }
            // Resource functions
            else if (strcmp(func_name, "defined") == 0) {
                return puppet_func_defined(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "realize") == 0) {
                return puppet_func_realize(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "tag") == 0) {
                return puppet_func_tag(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "tagged") == 0) {
                return puppet_func_tagged(&expr->data.funcall.args, env);
            }
            // Data lookup functions
            else if (strcmp(func_name, "lookup") == 0) {
                return puppet_func_lookup(&expr->data.funcall.args, env);
            }
            // String manipulation functions
            else if (strcmp(func_name, "split") == 0) {
                return puppet_func_split(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "join") == 0) {
                return puppet_func_join(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "downcase") == 0) {
                return puppet_func_downcase(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "upcase") == 0) {
                return puppet_func_upcase(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "strip") == 0) {
                return puppet_func_strip(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "lstrip") == 0) {
                return puppet_func_lstrip(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "rstrip") == 0) {
                return puppet_func_rstrip(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "chomp") == 0) {
                return puppet_func_chomp(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "chop") == 0) {
                return puppet_func_chop(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "capitalize") == 0) {
                return puppet_func_capitalize(&expr->data.funcall.args, env);
            }
            // Inspection functions
            else if (strcmp(func_name, "size") == 0 || strcmp(func_name, "length") == 0) {
                return puppet_func_size(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "empty") == 0) {
                return puppet_func_empty(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "keys") == 0) {
                return puppet_func_keys(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "values") == 0) {
                return puppet_func_values(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "has_key") == 0) {
                return puppet_func_has_key(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "member") == 0) {
                return puppet_func_member(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "reverse") == 0) {
                return puppet_func_reverse(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "unique") == 0) {
                return puppet_func_unique(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "sort") == 0) {
                return puppet_func_sort(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "flatten") == 0) {
                return puppet_func_flatten(&expr->data.funcall.args, env);
            }
            // Array functions
            else if (strcmp(func_name, "concat") == 0) {
                return puppet_func_concat(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "delete") == 0) {
                return puppet_func_delete(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "delete_at") == 0) {
                return puppet_func_delete_at(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "first") == 0) {
                return puppet_func_first(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "last") == 0) {
                return puppet_func_last(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "range") == 0) {
                return puppet_func_range(&expr->data.funcall.args, env);
            }
            // Hash functions
            else if (strcmp(func_name, "merge") == 0) {
                return puppet_func_merge(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "deep_merge") == 0) {
                return puppet_func_deep_merge(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "delete_undef_values") == 0) {
                return puppet_func_delete_undef_values(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "mysql::normalise_and_deepmerge") == 0) {
                return puppet_func_mysql_normalise_and_deepmerge(&expr->data.funcall.args, env);
            }
            // Array set operations
            else if (strcmp(func_name, "prefix") == 0) {
                return puppet_func_prefix(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "suffix") == 0) {
                return puppet_func_suffix(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "union") == 0) {
                return puppet_func_union(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "intersection") == 0) {
                return puppet_func_intersection(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "difference") == 0) {
                return puppet_func_difference(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "zip") == 0) {
                return puppet_func_zip(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "count") == 0) {
                return puppet_func_count(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "shuffle") == 0) {
                return puppet_func_shuffle(&expr->data.funcall.args, env);
            }
            // Shell/string functions
            else if (strcmp(func_name, "stdlib::shell_escape") == 0 ||
                     strcmp(func_name, "shell_escape") == 0) {
                return puppet_func_shell_escape(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "stdlib::shell_join") == 0 ||
                     strcmp(func_name, "shell_join") == 0) {
                return puppet_func_shell_join(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "stdlib::shell_split") == 0 ||
                     strcmp(func_name, "shell_split") == 0) {
                return puppet_func_shell_split(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "swapcase") == 0) {
                return puppet_func_swapcase(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "squeeze") == 0) {
                return puppet_func_squeeze(&expr->data.funcall.args, env);
            }
            // Numeric functions
            else if (strcmp(func_name, "clamp") == 0) {
                return puppet_func_clamp(&expr->data.funcall.args, env);
            }
            // Type conversion functions
            else if (strcmp(func_name, "any2bool") == 0) {
                return puppet_func_any2bool(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "bool2num") == 0) {
                return puppet_func_bool2num(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "num2bool") == 0) {
                return puppet_func_num2bool(&expr->data.funcall.args, env);
            }
            // Type checking functions
            else if (strcmp(func_name, "is_string") == 0) {
                return puppet_func_is_string(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "is_array") == 0) {
                return puppet_func_is_array(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "is_hash") == 0) {
                return puppet_func_is_hash(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "is_numeric") == 0 || strcmp(func_name, "is_integer") == 0 || strcmp(func_name, "is_float") == 0) {
                return puppet_func_is_numeric(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "is_bool") == 0 || strcmp(func_name, "is_boolean") == 0) {
                return puppet_func_is_bool(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "abs") == 0) {
                return puppet_func_abs(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "min") == 0) {
                return puppet_func_min(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "max") == 0) {
                return puppet_func_max(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "floor") == 0) {
                return puppet_func_floor(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "ceil") == 0 || strcmp(func_name, "ceiling") == 0) {
                return puppet_func_ceil(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "round") == 0) {
                return puppet_func_round(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "sqrt") == 0) {
                return puppet_func_sqrt(&expr->data.funcall.args, env);
            }
            // Path functions
            else if (strcmp(func_name, "basename") == 0) {
                return puppet_func_basename(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "dirname") == 0) {
                return puppet_func_dirname(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "extname") == 0) {
                return puppet_func_extname(&expr->data.funcall.args, env);
            }
            // Regex functions
            else if (strcmp(func_name, "regsubst") == 0) {
                return puppet_func_regsubst(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "match") == 0) {
                return puppet_func_match(&expr->data.funcall.args, env);
            }
            // Crypto functions
            else if (strcmp(func_name, "sha1") == 0) {
                return puppet_func_sha1(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "md5") == 0) {
                return puppet_func_md5(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "base64") == 0) {
                return puppet_func_base64(&expr->data.funcall.args, env);
            }
            // Iterator functions
            else if (strcmp(func_name, "each") == 0) {
                return puppet_func_each(expr, env);
            }
            else if (strcmp(func_name, "map") == 0) {
                return puppet_func_map(expr, env);
            }
            else if (strcmp(func_name, "filter") == 0 || strcmp(func_name, "select") == 0) {
                return puppet_func_filter(expr, env);
            }
            else if (strcmp(func_name, "reduce") == 0) {
                return puppet_func_reduce(expr, env);
            }

            // Validation functions (legacy stdlib)
            else if (strcmp(func_name, "validate_re") == 0) {
                return puppet_func_validate_re(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "validate_hash") == 0) {
                return puppet_func_validate_hash(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "validate_string") == 0) {
                return puppet_func_validate_string(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "validate_array") == 0) {
                return puppet_func_validate_array(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "validate_bool") == 0) {
                return puppet_func_validate_bool(&expr->data.funcall.args, env);
            }

            // Version comparison
            else if (strcmp(func_name, "versioncmp") == 0) {
                return puppet_func_versioncmp(&expr->data.funcall.args, env);
            }

            // Domain/IP validation
            else if (strcmp(func_name, "is_domain_name") == 0) {
                return puppet_func_is_domain_name(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "is_ip_address") == 0) {
                return puppet_func_is_ip_address(&expr->data.funcall.args, env);
            }

            // Resource creation
            else if (strcmp(func_name, "create_resources") == 0) {
                /* create_resources('type', hash) invokes 'type' per hash entry.
                 * Mark the target type as used so the dead-code tracker sees it
                 * (the interpreter resolves it dynamically, not via the static
                 * resource-declaration path the tracker hooks into). */
                if (env->prog->deadcode && expr->data.funcall.args.count >= 1) {
                    puppet_value_t *tv = puppet_eval_expr(expr->data.funcall.args.exprs[0], env);
                    if (tv && tv->type == PUPPET_VALUE_STRING && tv->data.string.data) {
                        const char *t = tv->data.string.data;
                        if (strncmp(t, "::", 2) == 0) t += 2;
                        if (strcmp(t, "class") == 0 && expr->data.funcall.args.count >= 2) {
                            /* create_resources('class', {'foo::bar' => {...}}) — keys are class names */
                            puppet_value_t *hv = puppet_eval_expr(expr->data.funcall.args.exprs[1], env);
                            if (hv && hv->type == PUPPET_VALUE_HASH) {
                                for (size_t bi = 0; bi < hv->data.hash->bucket_count; bi++) {
                                    puppet_hash_entry_t *e = hv->data.hash->buckets[bi];
                                    while (e) {
                                        puppet_deadcode_mark_class_used(env->prog->deadcode, e->key.data);
                                        e = e->next;
                                    }
                                }
                            }
                            if (hv) puppet_value_destroy(hv);
                        } else {
                            puppet_deadcode_mark_define_used(env->prog->deadcode, t);
                        }
                    }
                    if (tv) puppet_value_destroy(tv);
                }
                return puppet_func_create_resources(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "ensure_resource") == 0 ||
                     strcmp(func_name, "stdlib::ensure_resource") == 0) {
                return puppet_func_ensure_resource(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "ensure_packages") == 0 ||
                     strcmp(func_name, "stdlib::ensure_packages") == 0) {
                return puppet_func_ensure_packages(&expr->data.funcall.args, env);
            }

            // Conversion functions
            else if (strcmp(func_name, "any2array") == 0) {
                return puppet_func_any2array(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "str2bool") == 0) {
                return puppet_func_str2bool(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "bool2str") == 0) {
                return puppet_func_bool2str(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "type") == 0 || strcmp(func_name, "type_of") == 0) {
                return puppet_func_type(&expr->data.funcall.args, env);
            }

            // Random functions
            else if (strcmp(func_name, "fqdn_rand") == 0) {
                return puppet_func_fqdn_rand(&expr->data.funcall.args, env);
            }

            // Type assertion
            else if (strcmp(func_name, "assert_type") == 0) {
                return puppet_func_assert_type(&expr->data.funcall.args, env);
            }

            // Data access
            else if (strcmp(func_name, "dig") == 0) {
                return puppet_func_dig(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "fact") == 0) {
                return puppet_func_fact(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "pick") == 0) {
                return puppet_func_pick(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "pick_default") == 0) {
                return puppet_func_pick_default(&expr->data.funcall.args, env);
            }

            // Hiera lookup functions
            else if (strcmp(func_name, "hiera") == 0) {
                return puppet_func_hiera(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "lookup") == 0) {
                return puppet_func_lookup(&expr->data.funcall.args, env);
            }

            // Variable/file access functions
            else if (strcmp(func_name, "getvar") == 0) {
                return puppet_func_getvar(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "file") == 0) {
                return puppet_func_file(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "inline_template") == 0) {
                return puppet_func_inline_template(&expr->data.funcall.args, env);
            }

            else {
                /* User-defined Puppet function? (function name(...) >> T { }) */
                puppet_value_t *fn_wrap = func_name ?
                    puppet_hash_get(env->user_functions, func_name, strlen(func_name)) : NULL;
                if (fn_wrap) {
                    puppet_stmt_t *fn_stmt = (puppet_stmt_t *)fn_wrap->data.string.data;
                    return puppet_call_user_function(fn_stmt, expr, env);
                }

                /* Check if it's a custom Ruby function in a module */
                if (env->prog->loader && puppet_loader_has_custom_function(env->prog->loader, func_name)) {
                    /* Custom function found - return placeholder for catalog */
                    puppet_debug("Custom function %s() - returning placeholder", func_name);

                    /* Build a placeholder string showing the function call */
                    char placeholder[256];
                    snprintf(placeholder, sizeof(placeholder), "<%s(...)>", func_name);
                    return puppet_value_create_string(placeholder, strlen(placeholder));
                }

                puppet_error_at(expr->loc, "Unknown function: %s", func_name);
                puppet_env_increment_error(env);
                return puppet_value_create_undef();
            }
        }
            
        case PUPPET_EXPR_INTERPOLATED_STRING: {
            // Build interpolated string
            size_t total_len = 0;
            char **parts = puppet_calloc(expr->data.interpolated.count * 2 + 1, sizeof(char*));
            size_t part_count = 0;

            // Evaluate all parts
            for (size_t i = 0; i < expr->data.interpolated.count; i++) {
                // Add literal part if present
                if (expr->data.interpolated.parts && expr->data.interpolated.parts[i].data) {
                    parts[part_count] = puppet_strdup(expr->data.interpolated.parts[i].data);
                    total_len += expr->data.interpolated.parts[i].len;
                    part_count++;
                }

                // Evaluate expression if present
                if (expr->data.interpolated.exprs && expr->data.interpolated.exprs[i]) {
                    puppet_value_t *val = puppet_eval_expr(expr->data.interpolated.exprs[i], env);
                    const char *str = puppet_value_to_string(val);
                    if (str && *str) {
                        // Make a copy immediately since puppet_value_to_string may use static buffer
                        size_t str_len = strlen(str);
                        parts[part_count] = puppet_malloc(str_len + 1);
                        memcpy(parts[part_count], str, str_len + 1);
                        total_len += str_len;
                        part_count++;
                    }
                    puppet_value_destroy(val);
                }
            }

            // Add trailing literal part (stored in parts[count])
            if (expr->data.interpolated.parts &&
                expr->data.interpolated.parts[expr->data.interpolated.count].data &&
                expr->data.interpolated.parts[expr->data.interpolated.count].len > 0) {
                parts[part_count] = puppet_strdup(expr->data.interpolated.parts[expr->data.interpolated.count].data);
                total_len += expr->data.interpolated.parts[expr->data.interpolated.count].len;
                part_count++;
            }
            
            // Build final string
            char *result = puppet_malloc(total_len + 1);
            size_t pos = 0;
            for (size_t i = 0; i < part_count; i++) {
                if (parts[i]) {
                    size_t len = strlen(parts[i]);
                    memcpy(result + pos, parts[i], len);
                    pos += len;
                    puppet_free(parts[i]);
                }
            }
            result[total_len] = '\0';
            puppet_free(parts);
            
            puppet_value_t *ret = puppet_value_create_string(result, total_len);
            puppet_free(result);
            return ret;
        }

        case PUPPET_EXPR_CONDITIONAL: {
            // Ternary conditional: condition ? then_expr : else_expr
            puppet_value_t *cond = puppet_eval_expr(expr->data.conditional.condition, env);
            bool is_true = false;

            if (cond) {
                if (cond->type == PUPPET_VALUE_BOOL) {
                    is_true = cond->data.boolean;
                } else if (cond->type == PUPPET_VALUE_UNDEF) {
                    is_true = false;
                } else {
                    is_true = true;  // Non-undef, non-false values are truthy
                }
                puppet_value_destroy(cond);
            }

            if (is_true) {
                return puppet_eval_expr(expr->data.conditional.then_expr, env);
            } else {
                return puppet_eval_expr(expr->data.conditional.else_expr, env);
            }
        }

        case PUPPET_EXPR_SELECTOR: {
            // Selector expression: control ? { match1 => val1, match2 => val2, default => valN }
            puppet_value_t *control = puppet_eval_expr(expr->data.selector.control, env);
            puppet_value_t *result = NULL;

            // Check each case for a match
            for (size_t i = 0; i < expr->data.selector.case_count && !result; i++) {
                puppet_value_t *match = puppet_eval_expr(expr->data.selector.cases[i].match, env);

                // Compare control value with match value
                bool is_match = false;
                if (control && match) {
                    // String =~ Regexp selector case
                    if (control->type == PUPPET_VALUE_STRING &&
                        match->type == PUPPET_VALUE_REGEXP) {
                        regex_t rx;
                        int ret = puppet_regcomp(&rx, match->data.regexp.data,
                                                 REG_EXTENDED | REG_NOSUB);
                        if (ret == 0) {
                            is_match = (regexec(&rx, control->data.string.data,
                                                0, NULL, 0) == 0);
                            regfree(&rx);
                        }
                    } else if (control->type == match->type) {
                        switch (control->type) {
                            case PUPPET_VALUE_STRING:
                                is_match = (strcmp(control->data.string.data,
                                                  match->data.string.data) == 0);
                                break;
                            case PUPPET_VALUE_NUMBER:
                                is_match = (control->data.number == match->data.number);
                                break;
                            case PUPPET_VALUE_BOOL:
                                is_match = (control->data.boolean == match->data.boolean);
                                break;
                            default:
                                break;
                        }
                    }
                }
                puppet_value_destroy(match);

                if (is_match) {
                    result = puppet_eval_expr(expr->data.selector.cases[i].value, env);
                }
            }

            // If no match found, try default case
            if (!result && expr->data.selector.default_value) {
                result = puppet_eval_expr(expr->data.selector.default_value, env);
            }

            puppet_value_destroy(control);
            return result ? result : puppet_value_create_undef();
        }

        case PUPPET_EXPR_INDEX: {
            /* Array/hash indexing: obj[key] */
            puppet_value_t *obj = puppet_eval_expr(expr->data.index.object, env);
            puppet_value_t *key = puppet_eval_expr(expr->data.index.index, env);
            puppet_value_t *result = NULL;

            /* Strict-undef chained access: indexing the undef result of a PRIOR
             * index (e.g. $h['a']['b']['c'] where $h['a']['b'] is missing) is an
             * error in Puppet 8 — this is the silent-undef bug class. A single
             * index that yields undef (or indexing an undef variable directly) is
             * left alone; only a chain whose intermediate vanished is flagged. */
            if (obj && obj->type == PUPPET_VALUE_UNDEF &&
                expr->data.index.object->type == PUPPET_EXPR_INDEX) {
                /* Item 34: in -a mode the top-level statements are first
                 * executed once with NO node bound (a registration pre-pass;
                 * $facts is undef there), then re-executed per node with the
                 * real facts. Real Puppet has no such context — site.pp only
                 * ever evaluates with facts present — so suppress the strict
                 * error in that pre-pass; the per-node evaluation performs
                 * the real check. puppet_error_at already counts the error —
                 * no explicit increment (it double-counted; item 34 bug 2). */
                if (!(env->execute_all_nodes && !env->current_node_certname)) {
                    puppet_error_at(expr->loc,
                                    "Operator '[]' is not applicable to an Undef Value");
                }
                puppet_value_destroy(obj);
                puppet_value_destroy(key);
                return puppet_value_create_undef();
            }

            if (obj && key) {
                if (obj->type == PUPPET_VALUE_HASH && key->type == PUPPET_VALUE_STRING) {
                    /* Hash access */
                    puppet_value_t *val = puppet_hash_get(obj->data.hash,
                        key->data.string.data, key->data.string.len);
                    result = val ? puppet_value_copy(val) : puppet_value_create_undef();
                } else if (obj->type == PUPPET_VALUE_ARRAY && key->type == PUPPET_VALUE_NUMBER) {
                    /* Array access - support negative indexing ($arr[-1] is last element) */
                    int index = (int)key->data.number;
                    size_t idx;
                    if (index < 0) {
                        int adjusted = (int)obj->data.array->count + index;
                        idx = adjusted >= 0 ? (size_t)adjusted : obj->data.array->count; /* force OOB */
                    } else {
                        idx = (size_t)index;
                    }
                    if (idx < obj->data.array->count) {
                        result = puppet_value_copy(obj->data.array->items[idx]);
                    } else {
                        result = puppet_value_create_undef();
                    }
                } else {
                    result = puppet_value_create_undef();
                }
            } else {
                result = puppet_value_create_undef();
            }

            puppet_value_destroy(obj);
            puppet_value_destroy(key);
            return result;
        }

        case PUPPET_EXPR_RESOURCE_REF: {
            /* Resource reference: Type['title'] -> "Type[title]" string */
            puppet_value_t *title_val = puppet_eval_expr(expr->data.resource_ref.title, env);
            int type_len = (int)expr->data.resource_ref.type.len;
            const char *type_data = expr->data.resource_ref.type.data;

            /* Multi-title reference: Type[$array] -> [Type[a], Type[b], ...] so
             * each element resolves as its own reference (real Puppet semantics).
             * This is the same array-of-refs shape as `[Type['a'], Type['b']]`,
             * which require/before/notify validation already handles per-element. */
            if (title_val && title_val->type == PUPPET_VALUE_ARRAY && title_val->data.array) {
                puppet_array_t *src = title_val->data.array;
                puppet_array_t *arr = puppet_calloc(1, sizeof(puppet_array_t));
                arr->capacity = src->count > 0 ? src->count : 1;
                arr->items = puppet_calloc(arr->capacity, sizeof(puppet_value_t *));
                arr->count = 0;
                for (size_t i = 0; i < src->count; i++) {
                    const char *elem = puppet_value_to_string(src->items[i]);
                    size_t len = (size_t)type_len + 2 + (elem ? strlen(elem) : 0);
                    char *ref = puppet_malloc(len + 1);
                    snprintf(ref, len + 1, "%.*s[%s]", type_len, type_data, elem ? elem : "");
                    arr->items[arr->count++] = puppet_value_create_string(ref, strlen(ref));
                    puppet_free(ref);
                }
                puppet_value_t *result = puppet_calloc(1, sizeof(puppet_value_t));
                result->type = PUPPET_VALUE_ARRAY;
                result->data.array = arr;
                puppet_value_destroy(title_val);
                return result;
            }

            /* Scalar title: single "Type[title]" string. */
            /* Note: puppet_value_to_string returns internal pointer, don't free it */
            const char *title_str = puppet_value_to_string(title_val);
            size_t title_len = title_str ? strlen(title_str) : 0;
            size_t ref_len = (size_t)type_len + 1 + title_len + 1; /* Type[title] */

            char *ref_str = puppet_malloc(ref_len + 1);
            snprintf(ref_str, ref_len + 1, "%.*s[%s]",
                     type_len, type_data, title_str ? title_str : "");

            puppet_value_t *result = puppet_value_create_string(ref_str, strlen(ref_str));

            puppet_free(ref_str);
            puppet_value_destroy(title_val);
            return result;
        }

        case PUPPET_EXPR_HASH: {
            /* Hash with dynamic values - evaluate each key/value pair */
            puppet_value_t *result = puppet_value_create_hash();

            for (size_t i = 0; i < expr->data.hash_entries.count; i++) {
                puppet_value_t *key = puppet_eval_expr(expr->data.hash_entries.keys[i], env);
                puppet_value_t *val = puppet_eval_expr(expr->data.hash_entries.values[i], env);

                if (key && key->type == PUPPET_VALUE_STRING && val) {
                    puppet_hash_set(result->data.hash,
                                   key->data.string.data,
                                   key->data.string.len,
                                   puppet_value_copy(val));
                }

                if (key) puppet_value_destroy(key);
                if (val) puppet_value_destroy(val);
            }

            return result;
        }

        case PUPPET_EXPR_ARRAY: {
            /* Array with dynamic values - evaluate each item */
            puppet_array_t *arr = puppet_calloc(1, sizeof(puppet_array_t));
            arr->capacity = expr->data.array_items.count > 0 ? expr->data.array_items.count : 4;
            arr->items = puppet_calloc(arr->capacity, sizeof(puppet_value_t*));
            arr->count = 0;

            for (size_t i = 0; i < expr->data.array_items.count; i++) {
                puppet_value_t *item = puppet_eval_expr(expr->data.array_items.items[i], env);
                if (item) {
                    arr->items[arr->count++] = puppet_value_copy(item);
                    puppet_value_destroy(item);
                }
            }

            puppet_value_t *result = puppet_calloc(1, sizeof(puppet_value_t));
            result->type = PUPPET_VALUE_ARRAY;
            result->data.array = arr;
            return result;
        }

        default:
            puppet_warn("Unimplemented expression type: %d", expr->type);
            return puppet_value_create_undef();
    }
}

puppet_value_t *puppet_eval_variable(const char *name, puppet_location_t loc, puppet_env_t *env) {
    // Use enhanced lookup chain instead of simple scope lookup
    puppet_value_t *value = puppet_variable_lookup_chain(env, name);

    if (!value) {
        /* Suppress the warning during the deferred top-level pre-pass:
         * site.pp is executed once with no current node (just to register
         * node/class definitions), so $facts and other per-node bindings
         * are legitimately absent. Real lookups happen again when each
         * node is compiled — that's where genuinely missing variables
         * surface.
         *
         * Also suppress in a truthiness context (`if $x`, `unless $x`,
         * `$x and $y`, `! $x`): Puppet semantics treat undef as false
         * there, so it's not a real bug — just a defensive check. */
        if (!env->defer_node_execution && env->in_truthiness_check == 0) {
            puppet_warning_at(loc, "Undefined variable: %s", name);
        }
        return puppet_value_create_undef();
    }

    // Return a copy to avoid double-free (handles all types including arrays/hashes)
    return puppet_value_copy(value);
}

/**
 * Compare two Puppet values for equality (deep comparison for hashes/arrays)
 */
static bool puppet_values_equal(puppet_value_t *left, puppet_value_t *right) {
    if (!left && !right) return true;
    if (!left || !right) return false;
    if (left->type != right->type) return false;

    switch (left->type) {
        case PUPPET_VALUE_UNDEF:
            return true;
        case PUPPET_VALUE_BOOL:
            return left->data.boolean == right->data.boolean;
        case PUPPET_VALUE_NUMBER:
            return left->data.number == right->data.number;
        case PUPPET_VALUE_STRING:
            return left->data.string.len == right->data.string.len &&
                   strcmp(left->data.string.data, right->data.string.data) == 0;
        case PUPPET_VALUE_ARRAY:
            if (!left->data.array || !right->data.array) {
                return left->data.array == right->data.array;
            }
            if (left->data.array->count != right->data.array->count) return false;
            for (size_t i = 0; i < left->data.array->count; i++) {
                if (!puppet_values_equal(left->data.array->items[i], right->data.array->items[i])) {
                    return false;
                }
            }
            return true;
        case PUPPET_VALUE_HASH:
            /* Compare hash sizes first */
            if (!left->data.hash || !right->data.hash) {
                return left->data.hash == right->data.hash;
            }
            /* Count entries in left hash and verify all exist in right with same values */
            {
                size_t left_count = 0;
                for (size_t i = 0; i < left->data.hash->bucket_count; i++) {
                    for (puppet_hash_entry_t *e = left->data.hash->buckets[i]; e; e = e->next) {
                        left_count++;
                        puppet_value_t *right_val = puppet_hash_get(right->data.hash,
                            e->key.data, e->key.len);
                        if (!right_val || !puppet_values_equal(e->value, right_val)) {
                            return false;
                        }
                    }
                }
                /* Count entries in right hash to ensure same size */
                size_t right_count = 0;
                for (size_t i = 0; i < right->data.hash->bucket_count; i++) {
                    for (puppet_hash_entry_t *e = right->data.hash->buckets[i]; e; e = e->next) {
                        right_count++;
                    }
                }
                return left_count == right_count;
            }
        default:
            return false;
    }
}

/* Check if a Puppet value matches a Puppet type name (bare or parametric).
 * type_name: the type identifier (e.g. "Array", "String", "Pattern", "Variant", "Optional")
 * title_expr: the parameter expression inside brackets (NULL for bare types)
 * env: evaluation environment (needed to evaluate title_expr for parametric types)
 */
static bool value_matches_type_impl(puppet_value_t *val, const char *type_name,
                                     puppet_expr_t *title_expr, puppet_env_t *env) {
    if (!val || !type_name) return false;

    /* Any / Data / Scalar / NotUndef */
    if (strcmp(type_name, "Any") == 0) return true;
    if (strcmp(type_name, "NotUndef") == 0) return val->type != PUPPET_VALUE_UNDEF;
    if (strcmp(type_name, "Data") == 0) {
        /* Data = Scalar, Undef, Array[Data], Hash[String, Data] */
        return val->type != PUPPET_VALUE_TYPE && val->type != PUPPET_VALUE_DEFERRED;
    }
    if (strcmp(type_name, "Scalar") == 0) {
        return val->type == PUPPET_VALUE_STRING || val->type == PUPPET_VALUE_NUMBER ||
               val->type == PUPPET_VALUE_BOOL || val->type == PUPPET_VALUE_REGEXP;
    }
    if (strcmp(type_name, "Collection") == 0) {
        return val->type == PUPPET_VALUE_ARRAY || val->type == PUPPET_VALUE_HASH;
    }

    /* Bare simple types */
    if (strcmp(type_name, "Undef") == 0) return val->type == PUPPET_VALUE_UNDEF;
    if (strcmp(type_name, "Boolean") == 0) return val->type == PUPPET_VALUE_BOOL;
    if (strcmp(type_name, "Numeric") == 0) return val->type == PUPPET_VALUE_NUMBER;
    if (strcmp(type_name, "Integer") == 0) {
        return val->type == PUPPET_VALUE_NUMBER &&
               val->data.number == (double)(long long)val->data.number;
    }
    if (strcmp(type_name, "Float") == 0) {
        return val->type == PUPPET_VALUE_NUMBER &&
               val->data.number != (double)(long long)val->data.number;
    }
    if (strcmp(type_name, "Regexp") == 0) return val->type == PUPPET_VALUE_REGEXP;

    if (strcmp(type_name, "String") == 0) {
        if (val->type != PUPPET_VALUE_STRING) return false;
        /* String[min, max] - check length bounds if title given */
        /* For simplicity, bare String[N] means min length N */
        return true;  /* Most usages are bare String or String[1]; we accept all strings */
    }

    if (strcmp(type_name, "Array") == 0) {
        return val->type == PUPPET_VALUE_ARRAY;
    }
    if (strcmp(type_name, "Hash") == 0) {
        return val->type == PUPPET_VALUE_HASH;
    }

    /* Parametric types requiring title evaluation */
    if (strcmp(type_name, "Pattern") == 0) {
        if (val->type != PUPPET_VALUE_STRING || !title_expr) return false;
        puppet_value_t *pat = puppet_eval_expr(title_expr, env);
        bool matched = false;
        const char *pattern_str = NULL;
        if (pat) {
            if (pat->type == PUPPET_VALUE_REGEXP) pattern_str = pat->data.regexp.data;
            else if (pat->type == PUPPET_VALUE_STRING) pattern_str = pat->data.string.data;
        }
        if (pattern_str) {
            regex_t rx;
            if (puppet_regcomp(&rx, pattern_str, REG_EXTENDED | REG_NOSUB) == 0) {
                matched = (regexec(&rx, val->data.string.data, 0, NULL, 0) == 0);
                regfree(&rx);
            }
        }
        if (pat) puppet_value_destroy(pat);
        return matched;
    }

    if (strcmp(type_name, "Enum") == 0) {
        if (val->type != PUPPET_VALUE_STRING || !title_expr) return false;
        puppet_value_t *list = puppet_eval_expr(title_expr, env);
        bool matched = false;
        if (list && list->type == PUPPET_VALUE_ARRAY) {
            for (size_t i = 0; i < list->data.array->count && !matched; i++) {
                puppet_value_t *item = list->data.array->items[i];
                if (item && item->type == PUPPET_VALUE_STRING &&
                    strcmp(val->data.string.data, item->data.string.data) == 0) {
                    matched = true;
                }
            }
        } else if (list && list->type == PUPPET_VALUE_STRING) {
            matched = (strcmp(val->data.string.data, list->data.string.data) == 0);
        }
        if (list) puppet_value_destroy(list);
        return matched;
    }

    if (strcmp(type_name, "Optional") == 0) {
        if (val->type == PUPPET_VALUE_UNDEF) return true;
        if (!title_expr) return true;
        /* title_expr is the inner type; if it's a resource_ref, recurse */
        if (title_expr->type == PUPPET_EXPR_RESOURCE_REF) {
            const char *inner_type = title_expr->data.resource_ref.type.data;
            return value_matches_type(val, inner_type,
                                       title_expr->data.resource_ref.title, env);
        }
        return true;
    }

    if (strcmp(type_name, "Variant") == 0) {
        if (!title_expr) return false;
        /* title_expr should be an array of types, but tree-sitter may give us
         * a single type or a sequence. Evaluate and try each. */
        if (title_expr->type == PUPPET_EXPR_ARRAY) {
            for (size_t i = 0; i < title_expr->data.array_items.count; i++) {
                puppet_expr_t *item = title_expr->data.array_items.items[i];
                if (item && item->type == PUPPET_EXPR_RESOURCE_REF) {
                    const char *inner = item->data.resource_ref.type.data;
                    if (value_matches_type(val, inner,
                                            item->data.resource_ref.title, env)) {
                        return true;
                    }
                }
            }
            return false;
        }
        if (title_expr->type == PUPPET_EXPR_RESOURCE_REF) {
            return value_matches_type(val, title_expr->data.resource_ref.type.data,
                                       title_expr->data.resource_ref.title, env);
        }
        return false;
    }

    if (strcmp(type_name, "Type") == 0) {
        /* Type[X] - match if val is the type X */
        if (val->type != PUPPET_VALUE_STRING || !title_expr) return false;
        if (title_expr->type == PUPPET_EXPR_RESOURCE_REF) {
            return strcmp(val->data.string.data,
                          title_expr->data.resource_ref.type.data) == 0;
        }
        return false;
    }

    /* Named user-defined type alias (e.g. Stdlib::Fqdn)? Resolve and match. */
    {
        int r = match_type_alias(val, type_name, env);
        if (r >= 0) return r == 1;
    }

    /* Unknown type name - be conservative and return false */
    return false;
}

static bool value_matches_type(puppet_value_t *val, const char *type_name,
                                puppet_expr_t *title_expr, puppet_env_t *env) {
    return value_matches_type_impl(val, type_name, title_expr, env);
}

/**
 * Check a runtime value against a Puppet type constraint, working
 * straight off the raw source text the parser captured (e.g.
 * "Hash", "String[1]", "Pattern[/^a$/]", "Enum['a','b']",
 * "Variant[String, Integer]").
 *
 * Strategy:
 *  - Split off the base name (before the first '['). Bare type → use
 *    value_matches_type directly.
 *  - For parametric types we recognise (String[min[,max]],
 *    Integer[min[,max]], Array[type], Optional[type]) implement
 *    just enough custom logic to get the common Puppet-8 cases
 *    right.
 *  - For anything else (Pattern[/.../], Enum['a','b'], Variant[…],
 *    Stdlib::Absolutepath, …) we deliberately accept; the runtime's
 *    title-aware path can't be fed easily from a raw string, and we
 *    prefer false-negatives to false-positives.
 *
 * Returns true if val matches; false on a mismatch we can prove.
 * NULL constraint or unknown shape → true (be conservative).
 */
static bool value_matches_type_str(puppet_value_t *val,
                                    const char *type_str,
                                    puppet_env_t *env) {
    if (!type_str || !*type_str || !val) return true;

    /* Split base name and bracket content. */
    char base[64] = {0};
    const char *bracket = strchr(type_str, '[');
    size_t blen = bracket ? (size_t)(bracket - type_str) : strlen(type_str);
    if (blen >= sizeof(base)) blen = sizeof(base) - 1;
    memcpy(base, type_str, blen);
    /* Trim trailing whitespace. */
    while (blen > 0 && (base[blen-1] == ' ' || base[blen-1] == '\t')) base[--blen] = '\0';

    if (!bracket) {
        /* Plain bare type. Only return false when we know the
         * mismatch for sure; accept user-defined types
         * (Stdlib::Absolutepath, Stdlib::Filemode, …) silently. */
        if (strcmp(base, "Hash") == 0)    return val->type == PUPPET_VALUE_HASH;
        if (strcmp(base, "Array") == 0)   return val->type == PUPPET_VALUE_ARRAY;
        if (strcmp(base, "Boolean") == 0) return val->type == PUPPET_VALUE_BOOL;
        if (strcmp(base, "Numeric") == 0) return val->type == PUPPET_VALUE_NUMBER;
        if (strcmp(base, "Integer") == 0) {
            return val->type == PUPPET_VALUE_NUMBER &&
                   val->data.number == (double)(long long)val->data.number;
        }
        if (strcmp(base, "String") == 0)  return val->type == PUPPET_VALUE_STRING;
        if (strcmp(base, "Regexp") == 0)  return val->type == PUPPET_VALUE_REGEXP;
        if (strcmp(base, "Undef") == 0)   return val->type == PUPPET_VALUE_UNDEF;
        if (strcmp(base, "NotUndef") == 0) return val->type != PUPPET_VALUE_UNDEF;
        /* A registered/loadable named alias (e.g. Stdlib::Fqdn)? Resolve it. */
        {
            int r = match_type_alias(val, base, env);
            if (r >= 0) return r == 1;
        }
        /* Any, Data, Scalar, unresolved custom types — accept silently. */
        return true;
    }

    /* String[N] / String[min, max]: length-constrained string. */
    if (strcmp(base, "String") == 0) {
        if (val->type != PUPPET_VALUE_STRING) return false;
        long min = 0, max = -1;
        /* Try "[min, max]" first, then "[min]". */
        if (sscanf(bracket, " [ %ld , %ld ]", &min, &max) < 2) {
            sscanf(bracket, " [ %ld ]", &min);
        }
        size_t len = val->data.string.len;
        if ((long)len < min) return false;
        if (max >= 0 && (long)len > max) return false;
        return true;
    }

    /* Integer[min[,max]]: numeric range. */
    if (strcmp(base, "Integer") == 0) {
        if (val->type != PUPPET_VALUE_NUMBER) return false;
        double n = val->data.number;
        if (n != (double)(long long)n) return false;
        long min = LONG_MIN, max = LONG_MAX;
        sscanf(bracket, " [ %ld , %ld ]", &min, &max);
        long ni = (long)n;
        return ni >= min && ni <= max;
    }

    /* Optional[X]: undef OR matches X. */
    if (strcmp(base, "Optional") == 0) {
        if (val->type == PUPPET_VALUE_UNDEF) return true;
        /* Extract inner without the surrounding brackets. */
        const char *inner = bracket + 1;
        const char *end = strrchr(inner, ']');
        if (!end) return true;
        size_t inner_len = (size_t)(end - inner);
        char inner_buf[128] = {0};
        if (inner_len >= sizeof(inner_buf)) inner_len = sizeof(inner_buf) - 1;
        memcpy(inner_buf, inner, inner_len);
        /* Trim. */
        while (*inner_buf && (*inner_buf == ' ' || *inner_buf == '\t')) {
            memmove(inner_buf, inner_buf + 1, strlen(inner_buf));
        }
        return value_matches_type_str(val, inner_buf, env);
    }

    /* Enum['a','b',…]: a String equal to one of the listed literals. */
    if (strcmp(base, "Enum") == 0) {
        if (val->type != PUPPET_VALUE_STRING) return false;
        bool any = false, matched = false;
        for (const char *p = bracket; *p; p++) {
            if (*p == '\'' || *p == '"') {
                char q = *p++;
                const char *s = p;
                while (*p && *p != q) p++;
                size_t llen = (size_t)(p - s);
                any = true;
                if (llen == val->data.string.len &&
                    memcmp(s, val->data.string.data, llen) == 0) matched = true;
                if (*p != q) break;  /* unterminated literal */
            }
        }
        return any ? matched : true;  /* couldn't parse any literal → accept */
    }

    /* Pattern[/re/, …]: a String matching any listed regex literal. */
    if (strcmp(base, "Pattern") == 0) {
        if (val->type != PUPPET_VALUE_STRING) return false;
        bool any = false, matched = false;
        const char *p = bracket;
        while (*p && !matched) {
            const char *s = NULL, *e = NULL;
            if (*p == '/') {                       /* /regex/ */
                s = ++p;
                while (*p && *p != '/') { if (*p == '\\' && p[1]) p++; p++; }
                e = p;
                if (*p == '/') p++;
            } else if (*p == '\'' || *p == '"') {  /* 'regex' */
                char q = *p++;
                s = p;
                while (*p && *p != q) p++;
                e = p;
                if (*p == q) p++;
            } else { p++; continue; }
            size_t rlen = e > s ? (size_t)(e - s) : 0;
            char rbuf[512];
            if (rlen > 0 && rlen < sizeof(rbuf)) {
                memcpy(rbuf, s, rlen);
                rbuf[rlen] = '\0';
                any = true;
                regex_t rx;
                if (puppet_regcomp(&rx, rbuf, REG_EXTENDED | REG_NOSUB) == 0) {
                    if (regexec(&rx, val->data.string.data, 0, NULL, 0) == 0) matched = true;
                    regfree(&rx);
                }
            }
        }
        return any ? matched : true;
    }

    /* Variant[T1, T2, …]: matches if val matches any inner type. Split on
     * top-level commas (bracket-depth aware), then recurse. */
    if (strcmp(base, "Variant") == 0) {
        const char *inner = bracket + 1;
        const char *end = strrchr(inner, ']');
        if (!end || end <= inner) return true;
        const char *seg = inner;
        int depth = 0;
        bool any = false, matched = false;
        for (const char *p = inner; p <= end && !matched; p++) {
            if (p < end && *p == '[') depth++;
            else if (p < end && *p == ']') depth--;
            if (p == end || (*p == ',' && depth == 0)) {
                const char *s = seg;
                size_t slen = (size_t)(p - seg);
                while (slen && (*s == ' ' || *s == '\t')) { s++; slen--; }
                while (slen && (s[slen-1] == ' ' || s[slen-1] == '\t')) slen--;
                if (slen) {
                    char tbuf[256];
                    if (slen < sizeof(tbuf)) {
                        memcpy(tbuf, s, slen);
                        tbuf[slen] = '\0';
                        any = true;
                        if (value_matches_type_str(val, tbuf, env)) matched = true;
                    } else { any = true; matched = true; }  /* too long → accept */
                }
                seg = p + 1;
            }
        }
        return any ? matched : true;
    }

    /* Remaining parametric/alias bases (Stdlib::* etc.) can't be checked from
     * raw text without type-alias resolution — confirm only the safe bare bases
     * and otherwise accept silently to avoid false positives. */
    if (strcmp(base, "Hash") == 0)    return val->type == PUPPET_VALUE_HASH;
    if (strcmp(base, "Array") == 0)   return val->type == PUPPET_VALUE_ARRAY;
    if (strcmp(base, "Boolean") == 0) return val->type == PUPPET_VALUE_BOOL;
    if (strcmp(base, "Numeric") == 0) return val->type == PUPPET_VALUE_NUMBER;

    /* Named user-defined type alias (e.g. Stdlib::Fqdn)? Resolve to its
     * underlying type and match against that. */
    {
        int r = match_type_alias(val, base, env);
        if (r >= 0) return r == 1;
    }

    /* Unknown / parametric base — accept silently to avoid false positives. */
    return true;
}

/* Lazily load a module type alias `Module::Alias` from
 * <modules_path>/<module>/types/<alias-path>.pp and register every `type … = …`
 * it defines into env->type_aliases. No-op if there's no loader/modulepath or
 * the file is absent. Safe to call repeatedly (re-registers are idempotent). */
static void puppet_try_load_type_alias(const char *name, puppet_env_t *env) {
    if (!name || !env || !env->prog || !env->prog->loader ||
        !env->prog->loader->modules_path) return;

    const char *n = name;
    if (strncmp(n, "::", 2) == 0) n += 2;
    const char *sep = strstr(n, "::");
    if (!sep) return;  /* unqualified — not a module alias */

    /* module = lowercased first segment */
    char module[128];
    size_t mlen = (size_t)(sep - n);
    if (mlen == 0 || mlen >= sizeof(module)) return;
    for (size_t i = 0; i < mlen; i++) {
        char c = n[i];
        module[i] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
    }
    module[mlen] = '\0';

    /* rest = lowercased remainder with :: → / (e.g. IP::Address → ip/address) */
    char rest[256];
    size_t ri = 0;
    for (const char *p = sep + 2; *p && ri < sizeof(rest) - 1; ) {
        if (p[0] == ':' && p[1] == ':') { rest[ri++] = '/'; p += 2; }
        else { char c = *p++; rest[ri++] = (c >= 'A' && c <= 'Z') ? c + 32 : c; }
    }
    rest[ri] = '\0';
    if (ri == 0) return;

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s/types/%s.pp",
             env->prog->loader->modules_path, module, rest);

    struct stat st;
    if (stat(path, &st) != 0) return;  /* no such alias file */

    puppet_stmt_list_t *stmts = puppet_ts_parse_file(path);
    if (!stmts) return;
    for (size_t i = 0; i < stmts->count; i++) {
        puppet_stmt_t *s = stmts->stmts[i];
        if (s && s->type == PUPPET_STMT_TYPE_ALIAS &&
            s->data.type_alias.name.data && s->data.type_alias.type_str.data) {
            const char *an = s->data.type_alias.name.data;
            if (!puppet_hash_get(env->type_aliases, an, strlen(an))) {
                puppet_hash_set(env->type_aliases, an, strlen(an),
                    puppet_value_create_string(s->data.type_alias.type_str.data,
                                               s->data.type_alias.type_str.len));
            }
        }
    }
    /* We copied each alias's text into the hash, so the parsed AST can go.
     * Wrap-and-destroy mirrors the loader's ownership handling. */
    puppet_program_t *prog = puppet_calloc(1, sizeof(puppet_program_t));
    prog->statements = *stmts;
    puppet_free(stmts);
    puppet_program_destroy(prog);
}

static int match_type_alias(puppet_value_t *val, const char *base, puppet_env_t *env) {
    static __thread int alias_depth = 0;
    if (!env || !env->type_aliases || !base || !*base) return -1;

    puppet_value_t *body = puppet_hash_get(env->type_aliases, base, strlen(base));
    if (!body && strstr(base, "::")) {
        /* Unknown qualified name — try the module's types/ directory once. */
        puppet_try_load_type_alias(base, env);
        body = puppet_hash_get(env->type_aliases, base, strlen(base));
    }
    if (!body || body->type != PUPPET_VALUE_STRING) return -1;
    if (alias_depth >= 16) return -1;  /* cycle / runaway guard */

    alias_depth++;
    bool m = value_matches_type_str(val, body->data.string.data, env);
    alias_depth--;
    return m ? 1 : 0;
}

/* ----------------------------------------------------------------------------
 * Item 30 — per-tree resource policy.
 *
 * If <base_path>/.puppetc-policy.json exists, its "deprecated_resources"
 * entries flag resource declarations by type + title (exact or POSIX-ERE
 * pattern), e.g. apt::source['openvox7'] on a branch that targets OpenVox 8:
 *
 *   { "deprecated_resources": [
 *       { "type": "apt::source", "title": "openvox7",
 *         "reason": "this branch targets OpenVox 8 — use the openvox8 repo",
 *         "level": "warning" } ] }
 *
 * "title_pattern" may be used instead of "title"; "level" is "warning"
 * (default) or "error". The file is per-branch, so each env branch carries
 * its own policy. Absent file = feature off. Checks run at declaration time
 * (the resolved title), deduped to one diagnostic per type[title] per run.
 * -------------------------------------------------------------------------- */
static void puppet_policy_load_locked(puppet_env_t *env) {
    puppet_program_state_t *prog = env->prog;
    prog->policy_loaded = true;

    if (!prog->loader || !prog->loader->base_path) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/.puppetc-policy.json", prog->loader->base_path);
    struct stat st;
    if (stat(path, &st) != 0) return;  /* no policy file — feature off */

    json_value_t *root = json_parse_file(path);
    if (!root || !json_is_object(root)) {
        if (root) json_value_destroy(root);
        puppet_warn("Ignoring malformed policy file %s (not a JSON object)", path);
        return;
    }
    json_value_t *arr = json_object_get(root, "deprecated_resources");
    if (arr && json_is_array(arr)) {
        size_t n = json_array_size(arr);
        prog->policy_entries = puppet_calloc(n ? n : 1, sizeof(*prog->policy_entries));
        for (size_t i = 0; i < n; i++) {
            json_value_t *e = json_array_get(arr, i);
            if (!e || !json_is_object(e)) continue;
            const char *ty = json_get_string(json_object_get(e, "type"));
            const char *ti = json_get_string(json_object_get(e, "title"));
            const char *tp = json_get_string(json_object_get(e, "title_pattern"));
            if (!ty || (!ti && !tp)) continue;  /* need a type and a title form */
            struct puppet_policy_entry *pe =
                &prog->policy_entries[prog->policy_entry_count++];
            pe->type = puppet_strdup(ty);
            pe->title = ti ? puppet_strdup(ti) : NULL;
            pe->title_pattern = tp ? puppet_strdup(tp) : NULL;
            const char *rs = json_get_string(json_object_get(e, "reason"));
            pe->reason = rs ? puppet_strdup(rs) : NULL;
            const char *lv = json_get_string(json_object_get(e, "level"));
            pe->level_error = (lv && strcmp(lv, "error") == 0);
        }
    }
    json_value_destroy(root);
}

static void puppet_policy_check_resource(puppet_env_t *env, const char *type,
                                         const char *title, puppet_location_t loc) {
    if (!env || !env->prog || !type || !title) return;
    puppet_program_state_t *prog = env->prog;

    if (!prog->policy_loaded) {
        pthread_mutex_lock(&prog->reg_mutex);
        if (!prog->policy_loaded) puppet_policy_load_locked(env);
        pthread_mutex_unlock(&prog->reg_mutex);
    }
    if (prog->policy_entry_count == 0) return;

    for (size_t i = 0; i < prog->policy_entry_count; i++) {
        struct puppet_policy_entry *pe = &prog->policy_entries[i];
        if (strcasecmp(pe->type, type) != 0) continue;
        bool hit = false;
        if (pe->title) {
            hit = (strcmp(pe->title, title) == 0);
        } else if (pe->title_pattern) {
            regex_t rx;
            if (puppet_regcomp(&rx, pe->title_pattern, REG_EXTENDED | REG_NOSUB) == 0) {
                hit = (regexec(&rx, title, 0, NULL, 0) == 0);
                regfree(&rx);
            }
        }
        if (!hit) continue;

        /* Dedup: one diagnostic per type[title] per run. */
        char key[640];
        snprintf(key, sizeof(key), "%s[%s]", type, title);
        bool seen = false;
        pthread_mutex_lock(&prog->reg_mutex);
        for (size_t j = 0; j < prog->policy_warned_count && !seen; j++) {
            if (strcmp(prog->policy_warned[j], key) == 0) seen = true;
        }
        if (!seen) {
            if (prog->policy_warned_count == prog->policy_warned_cap) {
                prog->policy_warned_cap = prog->policy_warned_cap ? prog->policy_warned_cap * 2 : 8;
                prog->policy_warned = puppet_realloc(prog->policy_warned,
                    prog->policy_warned_cap * sizeof(char *));
            }
            prog->policy_warned[prog->policy_warned_count++] = puppet_strdup(key);
        }
        pthread_mutex_unlock(&prog->reg_mutex);
        if (seen) return;

        const char *reason = pe->reason ? pe->reason : "deprecated by tree policy";
        if (pe->level_error) {
            puppet_error_at(loc, "Policy (.puppetc-policy.json): resource %s['%s'] "
                            "is disallowed: %s", type, title, reason);
        } else {
            puppet_warning_at(loc, "Policy (.puppetc-policy.json): resource %s['%s'] "
                              "is deprecated: %s", type, title, reason);
        }
        return;
    }
}

puppet_value_t *puppet_eval_binop(puppet_binop_t op, puppet_value_t *left, puppet_value_t *right) {
    switch (op) {
        case PUPPET_OP_ADD:
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_number(left->data.number + right->data.number);
            }
            /* Handle undef operands */
            if (left->type == PUPPET_VALUE_UNDEF && right->type == PUPPET_VALUE_HASH) {
                return puppet_value_copy(right);
            }
            if (left->type == PUPPET_VALUE_HASH && right->type == PUPPET_VALUE_UNDEF) {
                return puppet_value_copy(left);
            }
            if (left->type == PUPPET_VALUE_UNDEF && right->type == PUPPET_VALUE_ARRAY) {
                return puppet_value_copy(right);
            }
            if (left->type == PUPPET_VALUE_ARRAY && right->type == PUPPET_VALUE_UNDEF) {
                return puppet_value_copy(left);
            }
            /* Hash merge: {a => 1} + {b => 2} => {a => 1, b => 2} */
            if (left->type == PUPPET_VALUE_HASH && right->type == PUPPET_VALUE_HASH) {
                puppet_value_t *result = puppet_value_create_hash();
                /* Copy left hash entries */
                for (size_t i = 0; i < left->data.hash->bucket_count; i++) {
                    puppet_hash_entry_t *entry = left->data.hash->buckets[i];
                    while (entry) {
                        puppet_value_t *value_copy = puppet_value_copy(entry->value);
                        puppet_hash_set(result->data.hash, entry->key.data, entry->key.len, value_copy);
                        entry = entry->next;
                    }
                }
                /* Merge right hash entries (overwrites duplicates) */
                for (size_t i = 0; i < right->data.hash->bucket_count; i++) {
                    puppet_hash_entry_t *entry = right->data.hash->buckets[i];
                    while (entry) {
                        puppet_value_t *value_copy = puppet_value_copy(entry->value);
                        puppet_hash_set(result->data.hash, entry->key.data, entry->key.len, value_copy);
                        entry = entry->next;
                    }
                }
                return result;
            }
            /* Array concatenation: [1,2] + [3,4] => [1,2,3,4] */
            if (left->type == PUPPET_VALUE_ARRAY && right->type == PUPPET_VALUE_ARRAY) {
                puppet_value_t *result = puppet_value_create_array();
                /* Append left array items */
                for (size_t i = 0; i < left->data.array->count; i++) {
                    puppet_array_append(result->data.array, puppet_value_copy(left->data.array->items[i]));
                }
                /* Append right array items */
                for (size_t i = 0; i < right->data.array->count; i++) {
                    puppet_array_append(result->data.array, puppet_value_copy(right->data.array->items[i]));
                }
                return result;
            }
            /* String concatenation: 'foo' + 'bar' => 'foobar' */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_STRING) {
                size_t left_len = left->data.string.len;
                size_t right_len = right->data.string.len;
                char *result_str = puppet_malloc(left_len + right_len + 1);
                memcpy(result_str, left->data.string.data, left_len);
                memcpy(result_str + left_len, right->data.string.data, right_len);
                result_str[left_len + right_len] = '\0';
                puppet_value_t *result = puppet_value_create_string(result_str, left_len + right_len);
                puppet_free(result_str);
                return result;
            }
            break;
            
        case PUPPET_OP_SUB:
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_number(left->data.number - right->data.number);
            }
            // Hash - String: remove key from hash
            if (left->type == PUPPET_VALUE_HASH && right->type == PUPPET_VALUE_STRING) {
                puppet_value_t *result = puppet_value_create_hash();
                puppet_hash_t *src = left->data.hash;
                const char *key_to_remove = right->data.string.data;
                size_t key_len = right->data.string.len;
                for (size_t i = 0; i < src->bucket_count; i++) {
                    for (puppet_hash_entry_t *e = src->buckets[i]; e; e = e->next) {
                        if (e->key.len != key_len || strcmp(e->key.data, key_to_remove) != 0) {
                            puppet_hash_set(result->data.hash, e->key.data, e->key.len,
                                           puppet_value_copy(e->value));
                        }
                    }
                }
                return result;
            }
            // String - String: bareword like "www-data" parsed as subtraction
            // Concatenate with '-' to reconstruct the original bareword
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_STRING) {
                size_t left_len = left->data.string.len;
                size_t right_len = right->data.string.len;
                size_t total = left_len + 1 + right_len;
                char *buf = puppet_malloc(total + 1);
                memcpy(buf, left->data.string.data, left_len);
                buf[left_len] = '-';
                memcpy(buf + left_len + 1, right->data.string.data, right_len);
                buf[total] = '\0';
                puppet_value_t *result = puppet_value_create_string(buf, total);
                puppet_free(buf);
                return result;
            }
            // Array - Array: remove elements (set difference)
            if (left->type == PUPPET_VALUE_ARRAY && right->type == PUPPET_VALUE_ARRAY) {
                puppet_value_t *result = puppet_value_create_array();
                puppet_array_t *src = left->data.array;
                puppet_array_t *remove = right->data.array;
                for (size_t i = 0; i < src->count; i++) {
                    bool found = false;
                    puppet_value_t *item = src->items[i];
                    for (size_t j = 0; j < remove->count && !found; j++) {
                        puppet_value_t *rem = remove->items[j];
                        // Simple equality check for common types
                        if (item->type == rem->type) {
                            switch (item->type) {
                                case PUPPET_VALUE_STRING:
                                    found = (item->data.string.len == rem->data.string.len &&
                                            strcmp(item->data.string.data, rem->data.string.data) == 0);
                                    break;
                                case PUPPET_VALUE_NUMBER:
                                    found = (item->data.number == rem->data.number);
                                    break;
                                case PUPPET_VALUE_BOOL:
                                    found = (item->data.boolean == rem->data.boolean);
                                    break;
                                default:
                                    break;
                            }
                        }
                    }
                    if (!found) {
                        puppet_array_append(result->data.array, puppet_value_copy(src->items[i]));
                    }
                }
                return result;
            }
            break;
            
        case PUPPET_OP_MUL:
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_number(left->data.number * right->data.number);
            }
            break;
            
        case PUPPET_OP_DIV:
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                if (right->data.number != 0) {
                    return puppet_value_create_number(left->data.number / right->data.number);
                }
                fprintf(stderr, "Warning: division by zero\n");
            }
            break;
            
        case PUPPET_OP_EQ:
            return puppet_value_create_bool(puppet_values_equal(left, right));

        case PUPPET_OP_NE:
            return puppet_value_create_bool(!puppet_values_equal(left, right));
            
        case PUPPET_OP_LT:
            /* Comparisons with undef return false */
            if (left->type == PUPPET_VALUE_UNDEF || right->type == PUPPET_VALUE_UNDEF) {
                return puppet_value_create_bool(false);
            }
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_bool(left->data.number < right->data.number);
            }
            /* String comparison for version strings */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_STRING) {
                return puppet_value_create_bool(
                    strcmp(left->data.string.data, right->data.string.data) < 0);
            }
            /* Mixed string/number - convert string to number */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_NUMBER) {
                double left_num = atof(left->data.string.data);
                return puppet_value_create_bool(left_num < right->data.number);
            }
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_STRING) {
                double right_num = atof(right->data.string.data);
                return puppet_value_create_bool(left->data.number < right_num);
            }
            break;

        case PUPPET_OP_GT:
            if (left->type == PUPPET_VALUE_UNDEF || right->type == PUPPET_VALUE_UNDEF) {
                return puppet_value_create_bool(false);
            }
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_bool(left->data.number > right->data.number);
            }
            /* String comparison for version strings */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_STRING) {
                return puppet_value_create_bool(
                    strcmp(left->data.string.data, right->data.string.data) > 0);
            }
            /* Mixed string/number */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_NUMBER) {
                double left_num = atof(left->data.string.data);
                return puppet_value_create_bool(left_num > right->data.number);
            }
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_STRING) {
                double right_num = atof(right->data.string.data);
                return puppet_value_create_bool(left->data.number > right_num);
            }
            break;

        case PUPPET_OP_LE:
            if (left->type == PUPPET_VALUE_UNDEF || right->type == PUPPET_VALUE_UNDEF) {
                return puppet_value_create_bool(false);
            }
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_bool(left->data.number <= right->data.number);
            }
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_STRING) {
                return puppet_value_create_bool(
                    strcmp(left->data.string.data, right->data.string.data) <= 0);
            }
            /* Mixed string/number */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_NUMBER) {
                double left_num = atof(left->data.string.data);
                return puppet_value_create_bool(left_num <= right->data.number);
            }
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_STRING) {
                double right_num = atof(right->data.string.data);
                return puppet_value_create_bool(left->data.number <= right_num);
            }
            break;

        case PUPPET_OP_GE:
            if (left->type == PUPPET_VALUE_UNDEF || right->type == PUPPET_VALUE_UNDEF) {
                return puppet_value_create_bool(false);
            }
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_bool(left->data.number >= right->data.number);
            }
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_STRING) {
                return puppet_value_create_bool(
                    strcmp(left->data.string.data, right->data.string.data) >= 0);
            }
            /* Mixed string/number */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_NUMBER) {
                double left_num = atof(left->data.string.data);
                return puppet_value_create_bool(left_num >= right->data.number);
            }
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_STRING) {
                double right_num = atof(right->data.string.data);
                return puppet_value_create_bool(left->data.number >= right_num);
            }
            break;

        case PUPPET_OP_AND:
            /* Logical AND - returns true if both are truthy */
            {
                bool left_bool = (left->type == PUPPET_VALUE_BOOL) ? left->data.boolean :
                                 (left->type != PUPPET_VALUE_UNDEF);
                bool right_bool = (right->type == PUPPET_VALUE_BOOL) ? right->data.boolean :
                                  (right->type != PUPPET_VALUE_UNDEF);
                return puppet_value_create_bool(left_bool && right_bool);
            }

        case PUPPET_OP_OR:
            /* Logical OR - returns true if either is truthy */
            {
                bool left_bool = (left->type == PUPPET_VALUE_BOOL) ? left->data.boolean :
                                 (left->type != PUPPET_VALUE_UNDEF);
                bool right_bool = (right->type == PUPPET_VALUE_BOOL) ? right->data.boolean :
                                  (right->type != PUPPET_VALUE_UNDEF);
                return puppet_value_create_bool(left_bool || right_bool);
            }

        case PUPPET_OP_MOD:
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                /* Puppet's % is an integer operation; guard the *integer*
                 * divisor, not the double — a fractional divisor like 0.5
                 * truncates to 0 and would otherwise SIGFPE. */
                int divisor = (int)right->data.number;
                if (divisor != 0) {
                    return puppet_value_create_number(
                        (int)left->data.number % divisor);
                }
                fprintf(stderr, "Warning: modulo by zero\n");
            }
            break;

        case PUPPET_OP_IN:
            /* Check if left is in right (array, hash, or string) */
            if (!right || right->type == PUPPET_VALUE_UNDEF) {
                /* If right side is undef, 'in' always returns false */
                return puppet_value_create_bool(false);
            }
            if (right->type == PUPPET_VALUE_ARRAY) {
                for (size_t i = 0; i < right->data.array->count; i++) {
                    puppet_value_t *elem = right->data.array->items[i];
                    if (left->type == elem->type) {
                        if (left->type == PUPPET_VALUE_STRING &&
                            strcmp(left->data.string.data, elem->data.string.data) == 0) {
                            return puppet_value_create_bool(true);
                        }
                        if (left->type == PUPPET_VALUE_NUMBER &&
                            left->data.number == elem->data.number) {
                            return puppet_value_create_bool(true);
                        }
                    }
                }
                return puppet_value_create_bool(false);
            }
            if (right->type == PUPPET_VALUE_HASH && left->type == PUPPET_VALUE_STRING) {
                /* Hash key membership: check if left is a key in the hash */
                puppet_value_t *val = puppet_hash_get(right->data.hash,
                    left->data.string.data, left->data.string.len);
                return puppet_value_create_bool(val != NULL);
            }
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_STRING) {
                return puppet_value_create_bool(
                    strstr(right->data.string.data, left->data.string.data) != NULL);
            }
            break;

        case PUPPET_OP_MATCH:
            /* Regex match =~ or type match */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_STRING) {
                const char *rhs = right->data.string.data;
                /* Check for type comparison: type($x) =~ Type[Hash]
                 * Left side is type name like "Hash", right is "Type[Hash]" */
                if (strncmp(rhs, "Type[", 5) == 0 && rhs[strlen(rhs) - 1] == ']') {
                    /* Extract the type name from Type[X] */
                    size_t type_name_len = strlen(rhs) - 6;  /* -5 for "Type[" and -1 for "]" */
                    const char *type_name = rhs + 5;
                    /* Compare left side with extracted type name */
                    if (strncmp(left->data.string.data, type_name, type_name_len) == 0 &&
                        left->data.string.data[type_name_len] == '\0') {
                        return puppet_value_create_bool(true);
                    }
                    return puppet_value_create_bool(false);
                }
                /* Regular regex match */
                regex_t regex;
                int ret = puppet_regcomp(&regex, right->data.string.data, REG_EXTENDED | REG_NOSUB);
                if (ret == 0) {
                    ret = regexec(&regex, left->data.string.data, 0, NULL, 0);
                    regfree(&regex);
                    return puppet_value_create_bool(ret == 0);
                } else {
                    char errbuf[128];
                    regerror(ret, &regex, errbuf, sizeof(errbuf));
                    fprintf(stderr, "Warning: invalid regex '%s': %s\n", right->data.string.data, errbuf);
                }
            }
            /* String =~ Regexp */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_REGEXP) {
                regex_t regex;
                int ret = puppet_regcomp(&regex, right->data.regexp.data, REG_EXTENDED | REG_NOSUB);
                if (ret == 0) {
                    ret = regexec(&regex, left->data.string.data, 0, NULL, 0);
                    regfree(&regex);
                    return puppet_value_create_bool(ret == 0);
                } else {
                    char errbuf[128];
                    regerror(ret, &regex, errbuf, sizeof(errbuf));
                    fprintf(stderr, "Warning: invalid regex '%s': %s\n", right->data.regexp.data, errbuf);
                }
            }
            return puppet_value_create_bool(false);

        case PUPPET_OP_NOT_MATCH:
            /* Regex non-match !~ or type non-match */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_STRING) {
                const char *rhs = right->data.string.data;
                /* Check for type comparison: type($x) !~ Type[Hash] */
                if (strncmp(rhs, "Type[", 5) == 0 && rhs[strlen(rhs) - 1] == ']') {
                    size_t type_name_len = strlen(rhs) - 6;
                    const char *type_name = rhs + 5;
                    if (strncmp(left->data.string.data, type_name, type_name_len) == 0 &&
                        left->data.string.data[type_name_len] == '\0') {
                        return puppet_value_create_bool(false);
                    }
                    return puppet_value_create_bool(true);
                }
                /* Regular regex non-match */
                regex_t regex;
                int ret = puppet_regcomp(&regex, right->data.string.data, REG_EXTENDED | REG_NOSUB);
                if (ret == 0) {
                    ret = regexec(&regex, left->data.string.data, 0, NULL, 0);
                    regfree(&regex);
                    return puppet_value_create_bool(ret != 0);
                } else {
                    char errbuf[128];
                    regerror(ret, &regex, errbuf, sizeof(errbuf));
                    fprintf(stderr, "Warning: invalid regex '%s': %s\n", right->data.string.data, errbuf);
                }
            }
            /* String !~ Regexp */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_REGEXP) {
                regex_t regex;
                int ret = puppet_regcomp(&regex, right->data.regexp.data, REG_EXTENDED | REG_NOSUB);
                if (ret == 0) {
                    ret = regexec(&regex, left->data.string.data, 0, NULL, 0);
                    regfree(&regex);
                    return puppet_value_create_bool(ret != 0);
                } else {
                    char errbuf[128];
                    regerror(ret, &regex, errbuf, sizeof(errbuf));
                    fprintf(stderr, "Warning: invalid regex '%s': %s\n", right->data.regexp.data, errbuf);
                }
            }
            return puppet_value_create_bool(true);

        default:
            break;
    }

    puppet_warn("Unsupported binary operation: op=%d left_type=%d right_type=%d",
                op, left->type, right->type);
    return puppet_value_create_undef();
}

puppet_value_t *puppet_eval_unop(puppet_unop_t op, puppet_value_t *operand) {
    switch (op) {
        case PUPPET_UNOP_NOT:
            // Convert operand to boolean and negate
            if (operand->type == PUPPET_VALUE_BOOL) {
                return puppet_value_create_bool(!operand->data.boolean);
            } else if (operand->type == PUPPET_VALUE_UNDEF) {
                return puppet_value_create_bool(true);
            }
            return puppet_value_create_bool(false);
            
        case PUPPET_UNOP_MINUS:
            if (operand->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_number(-operand->data.number);
            }
            break;
            
        case PUPPET_UNOP_PLUS:
            if (operand->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_number(operand->data.number);
            }
            break;
            
        default:
            break;
    }

    puppet_warn("Unsupported unary operation");
    return puppet_value_create_undef();
}

/* Forward declaration */
static bool puppet_include_class_from_def(puppet_stmt_t *class_def, puppet_env_t *env);

/* ============================================================================
 * Resource Collector Helpers
 * ============================================================================ */

/**
 * Get an attribute value from a pre-evaluated virtual resource
 */
static puppet_value_t *collector_get_vres_attribute(
    puppet_virtual_resource_t *vres,
    const char *attr_name
) {
    if (!vres) return NULL;

    for (size_t i = 0; i < vres->attr_count; i++) {
        if (vres->attrs[i].name && strcmp(vres->attrs[i].name, attr_name) == 0) {
            return vres->attrs[i].value;
        }
    }
    return NULL;
}

/**
 * Check if a virtual resource matches a collector filter expression
 * Handles: ==, !=, and, or operators with attribute comparisons
 */
static bool collector_matches_filter(
    puppet_expr_t *filter,
    puppet_virtual_resource_t *vres,
    puppet_env_t *env
) {
    if (!filter) return true;  /* No filter = match all */

    switch (filter->type) {
        case PUPPET_EXPR_BINOP: {
            puppet_binop_t op = filter->data.binop.op;

            /* Handle logical operators */
            if (op == PUPPET_OP_AND) {
                bool left = collector_matches_filter(filter->data.binop.left, vres, env);
                if (!left) return false;
                return collector_matches_filter(filter->data.binop.right, vres, env);
            }
            if (op == PUPPET_OP_OR) {
                bool left = collector_matches_filter(filter->data.binop.left, vres, env);
                if (left) return true;
                return collector_matches_filter(filter->data.binop.right, vres, env);
            }

            /* Handle comparison operators: left should be attribute name (variable) */
            if (op == PUPPET_OP_EQ || op == PUPPET_OP_NE) {
                /* Get attribute name from left side */
                const char *attr_name = NULL;
                if (filter->data.binop.left->type == PUPPET_EXPR_VARIABLE) {
                    attr_name = filter->data.binop.left->data.variable.data;
                } else if (filter->data.binop.left->type == PUPPET_EXPR_VALUE &&
                           filter->data.binop.left->data.value->type == PUPPET_VALUE_STRING) {
                    attr_name = filter->data.binop.left->data.value->data.string.data;
                }

                if (!attr_name) {
                    puppet_warn("Collector filter: left side must be attribute name");
                    return false;
                }

                /* Get the expected value from right side */
                puppet_value_t *expected = puppet_eval_expr(filter->data.binop.right, env);
                if (!expected) return false;

                /* Get the actual attribute value from resource (already evaluated) */
                puppet_value_t *actual = collector_get_vres_attribute(vres, attr_name);

                bool result = false;
                if (actual) {
                    bool is_equal = false;

                    /* Handle array attribute with 'in' semantics for equality */
                    if (actual->type == PUPPET_VALUE_ARRAY) {
                        /* Check if expected value is in the array */
                        for (size_t ai = 0; ai < actual->data.array->count; ai++) {
                            puppet_value_t *cmp = puppet_eval_binop(PUPPET_OP_EQ, actual->data.array->items[ai], expected);
                            if (cmp && cmp->type == PUPPET_VALUE_BOOL && cmp->data.boolean) {
                                is_equal = true;
                                puppet_value_destroy(cmp);
                                break;
                            }
                            if (cmp) puppet_value_destroy(cmp);
                        }
                    } else {
                        puppet_value_t *cmp = puppet_eval_binop(PUPPET_OP_EQ, actual, expected);
                        is_equal = (cmp && cmp->type == PUPPET_VALUE_BOOL && cmp->data.boolean);
                        if (cmp) puppet_value_destroy(cmp);
                    }

                    result = (op == PUPPET_OP_EQ) ? is_equal : !is_equal;
                    /* Don't destroy actual - it's owned by vres */
                } else {
                    /* Attribute not found: == fails, != succeeds */
                    result = (op == PUPPET_OP_NE);
                }

                puppet_value_destroy(expected);
                return result;
            }
            break;
        }

        default:
            puppet_warn("Collector filter: unsupported expression type %d", filter->type);
            break;
    }

    return false;
}

/**
 * Execute a resource collector - realize matching virtual or exported resources
 */
static void puppet_exec_collector(puppet_stmt_t *stmt, puppet_env_t *env) {
    if (!stmt || stmt->type != PUPPET_STMT_RESOURCE_COLLECTOR) return;

    const char *collect_type = stmt->data.collector.type.data;
    puppet_expr_t *filter = stmt->data.collector.search_expr;
    bool is_exported = (stmt->data.collector.style == PUPPET_RES_EXPORTED);

    /* Build lowercase type prefix for matching (e.g., "user[") */
    size_t type_len = strlen(collect_type);
    char *type_lower = puppet_malloc(type_len + 2);
    for (size_t i = 0; i < type_len; i++) {
        type_lower[i] = tolower((unsigned char)collect_type[i]);
    }
    type_lower[type_len] = '[';
    type_lower[type_len + 1] = '\0';
    size_t prefix_len = type_len + 1;

    size_t realized_count = 0;

    /* Handle exported resource collector (<<| |>>) - query PuppetDB */
    if (is_exported && env->puppetdb) {
        puppet_debug("Exported collector: querying PuppetDB for %s resources", collect_type);

        char *json_result = puppetdb_query_exported(env->puppetdb, collect_type);
        if (json_result) {
            /* Parse JSON array using proper JSON parser */
            json_value_t *resources = json_parse(json_result);
            if (resources && json_is_array(resources)) {
                size_t count = json_array_size(resources);

                for (size_t i = 0; i < count; i++) {
                    json_value_t *res_obj = json_array_get(resources, i);
                    if (!res_obj || !json_is_object(res_obj)) continue;

                    /* Extract fields */
                    char *res_type = json_object_get_string(res_obj, "type");
                    char *res_title = json_object_get_string(res_obj, "title");
                    char *res_certname = json_object_get_string(res_obj, "certname");

                    if (!res_type || !res_title) {
                        puppet_free(res_type);
                        puppet_free(res_title);
                        puppet_free(res_certname);
                        continue;
                    }

                    /* Skip resources from the same node (don't collect your own exports) */
                    if (res_certname && env->catalog && env->catalog->certname &&
                        strcmp(res_certname, env->catalog->certname) == 0) {
                        puppet_debug("Exported collector: skipping own resource %s[%s]",
                                    res_type, res_title);
                        puppet_free(res_type);
                        puppet_free(res_title);
                        puppet_free(res_certname);
                        continue;
                    }

                    /* Build resource ID */
                    size_t res_id_len = strlen(res_type) + strlen(res_title) + 3;
                    char *resource_id = puppet_malloc(res_id_len);
                    snprintf(resource_id, res_id_len, "%s[%s]", res_type, res_title);

                    /* Check for duplicate */
                    puppet_value_t *existing = puppet_hash_get(env->resource_catalog,
                                                               resource_id, strlen(resource_id));
                    if (existing) {
                        puppet_debug("Exported collector: %s already declared", resource_id);
                        puppet_free(resource_id);
                        puppet_free(res_type);
                        puppet_free(res_title);
                        puppet_free(res_certname);
                        continue;
                    }

                    puppet_debug("Exported collector: realizing %s from %s",
                                resource_id, res_certname ? res_certname : "unknown");

                    /* Mark as declared */
                    puppet_value_t *marker = puppet_value_create_bool(true);
                    puppet_hash_set(env->resource_catalog, resource_id, strlen(resource_id), marker);

                    /* Add to catalog with parameters */
                    if (env->build_catalog && env->catalog) {
                        puppet_catalog_param_t *params = NULL;
                        size_t param_count = 0;

                        /* Get parameters object */
                        json_value_t *params_obj = json_object_get(res_obj, "parameters");
                        if (params_obj && json_is_object(params_obj)) {
                            param_count = json_object_size(params_obj);
                            if (param_count > 0) {
                                params = puppet_calloc(param_count, sizeof(puppet_catalog_param_t));
                                for (size_t pi = 0; pi < param_count; pi++) {
                                    /* Get key and value from object */
                                    const char *key = params_obj->data.object.keys[pi];
                                    json_value_t *val = params_obj->data.object.values[pi];

                                    params[pi].name = puppet_strdup(key);
                                    params[pi].value = json_value_to_puppet_value(val);
                                }
                            }
                        }

                        int rc = puppet_catalog_add_resource(env->catalog, res_type, res_title,
                                                            params, param_count, NULL, 0);
                        if (rc == 0) {
                            /* Mark as exported in catalog */
                            puppet_catalog_resource_t *res = puppet_catalog_find_resource(
                                env->catalog, res_type, res_title);
                            if (res) {
                                res->exported = true;
                            }
                            /* Apply current scope tags */
                            puppet_apply_current_tags(env, res_type, res_title);
                            realized_count++;
                        }
                    }

                    puppet_free(resource_id);
                    puppet_free(res_type);
                    puppet_free(res_title);
                    puppet_free(res_certname);
                }
                json_value_destroy(resources);
            }
            puppet_free(json_result);
        }
        puppet_free(type_lower);
        puppet_debug("Exported collector: realized %zu %s resource(s) from PuppetDB",
                    realized_count, collect_type);
        return;
    }

    /* Handle virtual resource collector (<| |>) */
    if (!env->virtual_resources) {
        puppet_free(type_lower);
        return;
    }

    puppet_debug("Collector: looking for virtual %s resources", collect_type);

    /* Iterate through all virtual resources */
    for (size_t i = 0; i < env->virtual_resources->bucket_count; i++) {
        puppet_hash_entry_t *entry = env->virtual_resources->buckets[i];
        while (entry) {
            puppet_hash_entry_t *next = entry->next;  /* Save next before potential removal */

            /* Check if this resource matches the type */
            if (strncmp(entry->key.data, type_lower, prefix_len) == 0) {
                /* Get pre-evaluated virtual resource */
                puppet_virtual_resource_t *vres = (puppet_virtual_resource_t *)entry->value->data.string.data;
                if (!vres) {
                    entry = next;
                    continue;
                }

                /* Skip already realized resources */
                if (vres->realized) {
                    entry = next;
                    continue;
                }

                /* Check filter if present */
                if (collector_matches_filter(filter, vres, env)) {
                    puppet_debug("Collector: realizing %s", entry->key.data);

                    /* Build resource ID for duplicate check */
                    size_t res_id_len = strlen(vres->type) + strlen(vres->title) + 3;
                    char *resource_id = puppet_malloc(res_id_len);
                    snprintf(resource_id, res_id_len, "%s[%s]", vres->type, vres->title);

                    /* Check for duplicate */
                    puppet_value_t *existing = puppet_hash_get(env->resource_catalog, resource_id, strlen(resource_id));
                    if (existing) {
                        puppet_warn("Collector: Resource %s already declared", resource_id);
                        puppet_free(resource_id);
                        entry = next;
                        continue;
                    }

                    /* Mark as declared */
                    puppet_value_t *marker = puppet_value_create_bool(true);
                    puppet_hash_set(env->resource_catalog, resource_id, strlen(resource_id), marker);

                    /* Check if this is a defined type - need to execute its body */
                    puppet_value_t *define_ptr = puppet_hash_get(env->define_types,
                        vres->type, strlen(vres->type));

                    /* If not found, search through class_scopes for a define with matching suffix */
                    if (!define_ptr && !strchr(vres->type, ':') && env->class_scopes) {
                        size_t type_len = strlen(vres->type);
                        for (size_t ci = 0; ci < env->class_scopes->bucket_count && !define_ptr; ci++) {
                            puppet_hash_entry_t *class_entry = env->class_scopes->buckets[ci];
                            while (class_entry && !define_ptr) {
                                const char *class_name = class_entry->key.data;
                                /* Try fully qualified name: class_name::type_name */
                                size_t class_len = strlen(class_name);
                                size_t fq_len = class_len + 2 + type_len + 1;
                                char *fq_type_name = puppet_malloc(fq_len);
                                snprintf(fq_type_name, fq_len, "%s::%s", class_name, vres->type);
                                define_ptr = puppet_hash_get(env->define_types, fq_type_name, strlen(fq_type_name));
                                puppet_free(fq_type_name);
                                class_entry = class_entry->next;
                            }
                        }
                    }

                    /* Try to autoload the define if not found */
                    if (!define_ptr && env->prog->loader && strchr(vres->type, ':')) {
                        puppet_stmt_t *loaded_def = puppet_loader_load_define(env->prog->loader, vres->type);
                        if (loaded_def) {
                            puppet_debug("Collector: autoloaded defined type: %s", vres->type);
                            puppet_value_t *stmt_ptr = puppet_calloc(1, sizeof(puppet_value_t));
                            stmt_ptr->type = PUPPET_VALUE_UNDEF;
                            stmt_ptr->data.string.data = (char*)loaded_def;
                            puppet_hash_set(env->define_types, vres->type, strlen(vres->type), stmt_ptr);
                            define_ptr = stmt_ptr;
                        }
                    }

                    if (define_ptr) {
                        /* Execute the defined type body */
                        puppet_stmt_t *define_stmt = (puppet_stmt_t *)define_ptr->data.string.data;
                        puppet_deadcode_mark_define_used(env->prog->deadcode, vres->type);

                        puppet_debug("Collector: executing defined type %s[%s]", vres->type, vres->title);

                        /* Create new scope for the define execution */
                        puppet_scope_t *define_scope = puppet_scope_create(env->current_scope, vres->type);
                        puppet_scope_push(env, define_scope);

                        /* Bind $name and $title to the title */
                        puppet_value_t *name_val = puppet_value_create_string(vres->title, strlen(vres->title));
                        puppet_scope_set_var(define_scope, "name", name_val);
                        puppet_scope_set_var(define_scope, "title", puppet_value_copy(name_val));

                        /* Set $module_name for the define */
                        const char *define_sep = strstr(vres->type, "::");
                        if (define_sep) {
                            size_t def_module_len = define_sep - vres->type;
                            char *def_module = puppet_malloc(def_module_len + 1);
                            memcpy(def_module, vres->type, def_module_len);
                            def_module[def_module_len] = '\0';
                            puppet_scope_set_var(define_scope, "module_name",
                                puppet_value_create_string(def_module, def_module_len));
                            puppet_free(def_module);
                        } else {
                            puppet_scope_set_var(define_scope, "module_name",
                                puppet_value_create_string(vres->type, strlen(vres->type)));
                        }

                        /* Bind define parameters from virtual resource attributes */
                        for (size_t p = 0; p < define_stmt->data.define.params.count; p++) {
                            const char *param_name = define_stmt->data.define.params.params[p].name.data;
                            puppet_value_t *param_value = NULL;

                            /* Look for matching attribute in virtual resource */
                            for (size_t a = 0; a < vres->attr_count; a++) {
                                if (vres->attrs[a].name && strcmp(vres->attrs[a].name, param_name) == 0) {
                                    param_value = puppet_value_copy(vres->attrs[a].value);
                                    break;
                                }
                            }

                            /* Use default if no attribute provided */
                            if (!param_value && define_stmt->data.define.params.params[p].default_value) {
                                param_value = puppet_eval_expr(define_stmt->data.define.params.params[p].default_value, env);
                            }

                            if (param_value) {
                                puppet_scope_set_var(define_scope, param_name, param_value);
                            }
                        }

                        /* Execute the define body */
                        for (size_t bi = 0; bi < define_stmt->data.define.body.count; bi++) {
                            puppet_exec_stmt(define_stmt->data.define.body.stmts[bi], env);
                        }

                        /* Pop the define scope */
                        puppet_scope_t *popped = puppet_scope_pop(env);
                        puppet_scope_destroy(popped);
                    } else {
                        /* Built-in resource type - just add to catalog */
                        if (env->build_catalog && env->catalog) {
                            puppet_catalog_param_t *params = NULL;
                            if (vres->attr_count > 0) {
                                params = puppet_calloc(vres->attr_count, sizeof(puppet_catalog_param_t));
                                for (size_t j = 0; j < vres->attr_count; j++) {
                                    if (vres->attrs[j].name) {
                                        params[j].name = puppet_strdup(vres->attrs[j].name);
                                        params[j].value = puppet_value_copy(vres->attrs[j].value);
                                    }
                                }
                            }
                            puppet_catalog_add_resource(env->catalog, vres->type, vres->title,
                                                       params, vres->attr_count, NULL, 0);
                            /* Apply current scope tags */
                            puppet_apply_current_tags(env, vres->type, vres->title);
                        }
                    }

                    vres->realized = true;
                    realized_count++;
                    puppet_free(resource_id);
                }
            }
            entry = next;
        }
    }

    puppet_free(type_lower);
    puppet_debug("Collector: realized %zu %s resource(s)", realized_count, collect_type);
}

/* Forward declarations */
void puppet_exec_require(puppet_stmt_t *require_stmt, puppet_env_t *env);
void puppet_exec_contain(puppet_stmt_t *contain_stmt, puppet_env_t *env);

/* Metaparameters accepted on any class declaration without being declared. */
static const char *const puppet_class_metaparams[] = {
    "alias", "audit", "before", "consume", "export", "loglevel",
    "noop", "notify", "require", "schedule", "stage", "subscribe", "tag",
    NULL
};

/*
 * Verify every provided attribute matches a declared class parameter.
 * Puppet rejects unknown class params with "no parameter named X" — silently
 * accepting them lets interface-drift bugs (e.g. a site.pp adding
 * `mount_innodbtmp => true` before the module grows the param) ship to
 * production without warning. Metaparameters are always allowed, and splat
 * attributes (NULL name, `* => $hash`) are skipped. Emits one error per
 * unknown attribute via puppet_error_at + puppet_env_increment_error.
 */
static void puppet_validate_class_args(const puppet_stmt_t *class_def,
                                       const puppet_attribute_t *attrs,
                                       size_t attr_count,
                                       const char *class_name,
                                       puppet_location_t loc,
                                       puppet_env_t *env) {
    if (!class_def) return;
    const puppet_param_list_t *params = &class_def->data.class_def.params;
    for (size_t ai = 0; ai < attr_count; ai++) {
        const char *aname = attrs[ai].name.data;
        if (!aname) continue;  /* splat (* => $hash) */
        bool matched = false;
        for (size_t pi = 0; pi < params->count; pi++) {
            if (strcmp(aname, params->params[pi].name.data) == 0) {
                matched = true;
                break;
            }
        }
        if (matched) continue;
        for (size_t mi = 0; puppet_class_metaparams[mi]; mi++) {
            if (strcmp(aname, puppet_class_metaparams[mi]) == 0) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            puppet_error_at(loc, "Class '%s' has no parameter named '%s'",
                            class_name, aname);
            puppet_env_increment_error(env);
        }
    }
}

void puppet_exec_stmt(puppet_stmt_t *stmt, puppet_env_t *env) {
    if (!stmt) return;
    
    switch (stmt->type) {
        case PUPPET_STMT_ASSIGNMENT:
            puppet_exec_assignment(stmt->data.assignment.variable.data,
                                  stmt->data.assignment.value, env);
            break;

        case PUPPET_STMT_APPEND: {
            /* Array append: $var += value */
            const char *var_name = stmt->data.append.variable.data;
            puppet_value_t *append_val = puppet_eval_expr(stmt->data.append.value, env);

            /* Get current value of variable */
            puppet_value_t *current = puppet_env_get_var(env, var_name);

            if (!current || current->type == PUPPET_VALUE_UNDEF) {
                /* Variable doesn't exist or is undef - create new array */
                if (append_val->type == PUPPET_VALUE_ARRAY) {
                    /* Value is already an array, use it directly */
                    puppet_env_set_scoped_var(env, var_name, append_val, PUPPET_VAR_LOCAL);
                } else {
                    /* Wrap single value in array */
                    puppet_value_t *new_array = puppet_value_create_array();
                    puppet_array_append(new_array->data.array, puppet_value_copy(append_val));
                    puppet_env_set_scoped_var(env, var_name, new_array, PUPPET_VAR_LOCAL);
                    puppet_value_destroy(append_val);
                }
            } else if (current->type == PUPPET_VALUE_ARRAY) {
                /* Append to existing array */
                puppet_value_t *new_array = puppet_value_copy(current);
                if (append_val->type == PUPPET_VALUE_ARRAY) {
                    /* Concatenate arrays */
                    for (size_t i = 0; i < append_val->data.array->count; i++) {
                        puppet_array_append(new_array->data.array,
                            puppet_value_copy(append_val->data.array->items[i]));
                    }
                } else {
                    /* Append single value */
                    puppet_array_append(new_array->data.array, puppet_value_copy(append_val));
                }
                puppet_env_set_scoped_var(env, var_name, new_array, PUPPET_VAR_LOCAL);
                puppet_value_destroy(append_val);
            } else if (current->type == PUPPET_VALUE_HASH && append_val->type == PUPPET_VALUE_HASH) {
                /* Merge hashes */
                puppet_value_t *new_hash = puppet_value_copy(current);
                /* Copy entries from append_val to new_hash */
                for (size_t i = 0; i < append_val->data.hash->bucket_count; i++) {
                    puppet_hash_entry_t *entry = append_val->data.hash->buckets[i];
                    while (entry) {
                        puppet_hash_set(new_hash->data.hash, entry->key.data, entry->key.len,
                            puppet_value_copy(entry->value));
                        entry = entry->next;
                    }
                }
                puppet_env_set_scoped_var(env, var_name, new_hash, PUPPET_VAR_LOCAL);
                puppet_value_destroy(append_val);
            } else {
                puppet_warn("Cannot append to non-array/non-hash variable '%s'", var_name);
                puppet_value_destroy(append_val);
            }

            if (puppet_verbose) {
                puppet_debug("Appended to $%s", var_name);
            }
            break;
        }

        case PUPPET_STMT_CLASS_DEF:
            puppet_exec_class_def(stmt, env);
            break;

        case PUPPET_STMT_DEFINE:
            /* Register the defined type for later instantiation */
            if (stmt->data.define.name.data) {
                /* Build fully qualified name (prepend class scope if inside a class) */
                char *fq_name;
                if (env->class_scope && env->class_scope->name.data) {
                    size_t class_len = strlen(env->class_scope->name.data);
                    size_t name_len = strlen(stmt->data.define.name.data);
                    fq_name = puppet_malloc(class_len + 2 + name_len + 1);
                    snprintf(fq_name, class_len + 2 + name_len + 1, "%s::%s",
                             env->class_scope->name.data, stmt->data.define.name.data);
                } else {
                    fq_name = puppet_strdup(stmt->data.define.name.data);
                }

                puppet_debug("Registering defined type: %s", fq_name);
                /* Store pointer to statement (don't copy - AST owns it) */
                puppet_value_t *stmt_ptr = puppet_calloc(1, sizeof(puppet_value_t));
                stmt_ptr->type = PUPPET_VALUE_UNDEF;
                stmt_ptr->data.string.data = (char*)stmt;
                puppet_hash_set(env->define_types, fq_name, strlen(fq_name), stmt_ptr);
                puppet_free(fq_name);
            }
            break;

        case PUPPET_STMT_FUNCTION_DEF:
            /* Register the user-defined function for later calls. The name is
             * already fully namespaced (e.g. "math::double"). Store a wrapper
             * holding a borrowed pointer to the AST stmt, like define_types. */
            if (stmt->data.function_def.name.data) {
                const char *fn = stmt->data.function_def.name.data;
                puppet_debug("Registering function: %s", fn);
                puppet_value_t *fn_ptr = puppet_calloc(1, sizeof(puppet_value_t));
                fn_ptr->type = PUPPET_VALUE_UNDEF;
                fn_ptr->data.string.data = (char*)stmt;
                puppet_hash_set(env->user_functions, fn, strlen(fn), fn_ptr);
            }
            break;

        case PUPPET_STMT_TYPE_ALIAS:
            /* Register `type Name = <type>` so value_matches_type can resolve
             * the named alias (e.g. Stdlib::Fqdn) to its underlying type. Store
             * the raw aliased type text as the hash value. */
            if (stmt->data.type_alias.name.data && stmt->data.type_alias.type_str.data &&
                env->type_aliases) {
                const char *an = stmt->data.type_alias.name.data;
                puppet_hash_set(env->type_aliases, an, strlen(an),
                    puppet_value_create_string(stmt->data.type_alias.type_str.data,
                                               stmt->data.type_alias.type_str.len));
            }
            break;

        case PUPPET_STMT_CLASS_INSTANCE:
            puppet_exec_class_instance(stmt, env);
            break;
            
        case PUPPET_STMT_NODE:
            puppet_exec_node(stmt, env);
            break;
            
        case PUPPET_STMT_INCLUDE:
            puppet_exec_include(stmt, env);
            break;

        case PUPPET_STMT_REQUIRE:
            puppet_exec_require(stmt, env);
            break;

        case PUPPET_STMT_CONTAIN:
            puppet_exec_contain(stmt, env);
            break;

        case PUPPET_STMT_FUNCTION_CALL:
            // Execute function call statement (stored as expression)
            if (stmt->data.expr) {
                puppet_value_t *result = puppet_eval_expr(stmt->data.expr, env);
                puppet_value_destroy(result);
            }
            break;

        case PUPPET_STMT_EXPRESSION:
            // Execute bare expression statement
            if (stmt->data.expr) {
                puppet_value_t *result = puppet_eval_expr(stmt->data.expr, env);
                puppet_value_destroy(result);
            }
            break;

        case PUPPET_STMT_RESOURCE:
            puppet_debug("Executing resource: %s", stmt->data.resource.type.data);
            puppet_deadcode_mark_type_used(env->prog->deadcode, stmt->data.resource.type.data);

            // Handle class resources specially - they instantiate classes
            if (strcmp(stmt->data.resource.type.data, "class") == 0) {
                for (size_t i = 0; i < stmt->data.resource.instance_count; i++) {
                    puppet_resource_instance_t *instance = &stmt->data.resource.instances[i];
                    if (instance->title) {
                        puppet_value_t *title_val = puppet_eval_expr(instance->title, env);
                        const char *class_name_raw = puppet_value_to_string(title_val);

                        // Normalize class name by stripping leading ::
                        const char *class_name = class_name_raw;
                        if (strncmp(class_name, "::", 2) == 0) {
                            class_name = class_name_raw + 2;
                        }
                        puppet_debug("  Class resource: %s", class_name);
                        puppet_deadcode_mark_class_used(env->prog->deadcode, class_name);

                        // Check if this class was already declared with class { } syntax
                        // (resource-style declarations are NOT idempotent, but include is)
                        if (puppet_hash_get(env->class_resource_decls, class_name, strlen(class_name))) {
                            puppet_error_at(stmt->loc, "Duplicate declaration - class[%s] is already declared", class_name);
                            fprintf(stderr, "       Use 'include' for idempotent class inclusion\n");
                            puppet_env_increment_error(env);
                            puppet_value_destroy(title_val);
                            continue;
                        }

                        // Mark this class as declared via class { } syntax
                        puppet_hash_set(env->class_resource_decls, class_name, strlen(class_name),
                                        puppet_value_create_bool(true));

                        // Check if class was already executed via include
                        // If class {} declaration provides parameters and the class was already included,
                        // we need to re-execute the class body with the new parameters.
                        puppet_scope_t *existing_scope = (puppet_scope_t *)puppet_hash_get(env->class_scopes, class_name, strlen(class_name));

                        if (existing_scope) {
                            // Check if we have parameters to apply
                            if (instance->attr_count > 0) {
                                // Check if this class is currently being re-executed (loop guard)
                                if (puppet_hash_get(env->classes_being_reexecuted, class_name, strlen(class_name))) {
                                    // Already re-executing this class - skip to prevent infinite loop
                                    puppet_debug("Class %s already being re-executed, skipping", class_name);
                                    puppet_value_destroy(title_val);
                                    continue;
                                }

                                // Mark this class as being re-executed
                                puppet_hash_set(env->classes_being_reexecuted, class_name, strlen(class_name),
                                                puppet_value_create_bool(true));

                                puppet_debug("Class %s already included, re-executing with parameters from class {} declaration", class_name);

                                // Find the class definition
                                puppet_stmt_t *class_def = puppet_find_class_def(env, class_name);
                                if (!class_def && env->prog->loader) {
                                    class_def = puppet_loader_load_class(env->prog->loader, class_name);
                                }

                                if (class_def) {
                                    // Reject unknown params here too — otherwise an
                                    // already-included class would silently accept bogus
                                    // attributes that a fresh declaration would reject.
                                    puppet_validate_class_args(class_def, instance->attributes,
                                                               instance->attr_count, class_name,
                                                               stmt->loc, env);

                                    // Update parameters in existing scope
                                    for (size_t ai = 0; ai < instance->attr_count; ai++) {
                                        if (instance->attributes[ai].name.data) {
                                            puppet_value_t *attr_val = puppet_eval_expr(instance->attributes[ai].value, env);
                                            puppet_scope_set_var(existing_scope, instance->attributes[ai].name.data, attr_val);
                                            puppet_debug("  Updated param %s in existing scope", instance->attributes[ai].name.data);
                                        }
                                    }

                                    // Re-execute class body with updated parameters
                                    puppet_scope_push(env, existing_scope);
                                    puppet_scope_t *old_class_scope = env->class_scope;
                                    env->class_scope = existing_scope;

                                    // Set class_reexecuting flag to allow resource overwrites
                                    bool old_reexecuting = env->class_reexecuting;
                                    env->class_reexecuting = true;

                                    // Set caller_module_name for template lookups
                                    char *old_caller_module = env->caller_module_name;
                                    const char *sep = strstr(class_name, "::");
                                    if (sep) {
                                        size_t module_len = sep - class_name;
                                        env->caller_module_name = puppet_malloc(module_len + 1);
                                        memcpy(env->caller_module_name, class_name, module_len);
                                        env->caller_module_name[module_len] = '\0';
                                    } else {
                                        env->caller_module_name = puppet_strdup(class_name);
                                    }

                                    // Execute class body
                                    for (size_t bi = 0; bi < class_def->data.class_def.body.count; bi++) {
                                        puppet_exec_stmt(class_def->data.class_def.body.stmts[bi], env);
                                    }

                                    puppet_free(env->caller_module_name);
                                    env->caller_module_name = old_caller_module;
                                    env->class_scope = old_class_scope;
                                    env->class_reexecuting = old_reexecuting;
                                    puppet_scope_pop(env);
                                }

                                // Note: We leave the re-execution flag set - it prevents duplicate re-execution
                                // of the same class if multiple class {} declarations exist

                                puppet_value_destroy(title_val);
                                continue;
                            }

                            // Class already included with no new parameters - skip
                            puppet_debug("Class %s already included, skipping re-execution", class_name);
                            puppet_value_destroy(title_val);
                            continue;
                        }

                        // Find the class definition
                        puppet_stmt_t *class_def = puppet_find_class_def(env, class_name);
                        if (!class_def && env->prog->loader) {
                            class_def = puppet_loader_load_class(env->prog->loader, class_name);
                        }

                        if (!class_def) {
                            puppet_error_at(stmt->loc, "Class '%s' not found", class_name);
                            puppet_env_increment_error(env);
                            puppet_value_destroy(title_val);
                            continue;
                        }

                        // Handle class inheritance - include parent class first
                        puppet_scope_t *parent_class_scope = NULL;
                        if (class_def->data.class_def.inherits && class_def->data.class_def.inherits->data) {
                            const char *parent_name = class_def->data.class_def.inherits->data;
                            /* Strip leading :: from parent name for lookups */
                            const char *parent_lookup_name = parent_name;
                            if (strncmp(parent_lookup_name, "::", 2) == 0) {
                                parent_lookup_name = parent_name + 2;
                            }
                            puppet_debug("Class %s inherits from %s", class_name, parent_name);

                            puppet_stmt_t *parent_def = puppet_find_class_def(env, parent_lookup_name);
                            if (!parent_def && env->prog->loader) {
                                parent_def = puppet_loader_load_class(env->prog->loader, parent_lookup_name);
                            }

                            if (parent_def) {
                                parent_class_scope = (puppet_scope_t *)puppet_hash_get(
                                    env->class_scopes, parent_lookup_name, strlen(parent_lookup_name));
                                if (!parent_class_scope) {
                                    puppet_include_class_from_def(parent_def, env);
                                    parent_class_scope = (puppet_scope_t *)puppet_hash_get(
                                        env->class_scopes, parent_lookup_name, strlen(parent_lookup_name));
                                }
                            } else {
                                puppet_warn("Parent class '%s' not found for class '%s'", parent_name, class_name);
                            }
                        }

                        // Verify every provided attribute matches a declared class param.
                        puppet_validate_class_args(class_def, instance->attributes,
                                                   instance->attr_count, class_name,
                                                   stmt->loc, env);

                        // Pre-evaluate all attribute values in CALLER's scope (before pushing new class scope)
                        // This is critical - variables like $backups in "directories => $backups" must
                        // be looked up in the calling class's scope, not the new class being declared
                        size_t param_count = class_def->data.class_def.params.count;
                        puppet_value_t **pre_eval_values = NULL;
                        bool *attr_found = NULL;

                        if (param_count > 0) {
                            pre_eval_values = puppet_malloc(param_count * sizeof(puppet_value_t *));
                            attr_found = puppet_malloc(param_count * sizeof(bool));

                            for (size_t pi = 0; pi < param_count; pi++) {
                                puppet_param_t *param = &class_def->data.class_def.params.params[pi];
                                const char *param_name = param->name.data;
                                pre_eval_values[pi] = NULL;
                                attr_found[pi] = false;

                                // Look for matching attribute and evaluate in caller's scope
                                for (size_t ai = 0; ai < instance->attr_count; ai++) {
                                    if (instance->attributes[ai].name.data &&
                                        strcmp(instance->attributes[ai].name.data, param_name) == 0) {
                                        pre_eval_values[pi] = puppet_eval_expr(instance->attributes[ai].value, env);
                                        // Puppet semantics: an explicitly-supplied
                                        // undef value is stripped, so the default
                                        // (or APL) applies. Leave attr_found false
                                        // and let the binding loop below resolve it;
                                        // otherwise a typed param passed `undef`
                                        // would fail the type-check even though real
                                        // Puppet uses the default.
                                        if (pre_eval_values[pi] &&
                                            pre_eval_values[pi]->type == PUPPET_VALUE_UNDEF &&
                                            param->default_value) {
                                            puppet_value_destroy(pre_eval_values[pi]);
                                            pre_eval_values[pi] = NULL;
                                            break;
                                        }
                                        attr_found[pi] = true;
                                        // Type-check the provided value (see
                                        // puppet_exec_class_instance for the
                                        // detailed rationale).
                                        if (param->type_expr && pre_eval_values[pi] &&
                                            !value_matches_type_str(pre_eval_values[pi],
                                                                     param->type_str.data, env)) {
                                            char typestr[128];
                                            snprintf(typestr, sizeof(typestr), "%s", param->type_str.data ? param->type_str.data : "?");
                                            puppet_error_at(stmt->loc,
                                                "Class '%s' parameter $%s: expected %s, got incompatible value",
                                                class_name, param_name, typestr);
                                            puppet_env_increment_error(env);
                                        }
                                        break;
                                    }
                                }
                            }
                        }

                        // Now create scope for class, parented by inherited class scope if any.
                        // IMPORTANT: Use node_scope instead of current_scope to avoid dangling pointers
                        // when current_scope is a transient define scope that gets destroyed.
                        puppet_scope_t *scope_parent = parent_class_scope ? parent_class_scope : env->node_scope;
                        puppet_scope_t *class_scope = puppet_scope_create(scope_parent, class_name);
                        puppet_scope_push(env, class_scope);
                        puppet_scope_t *old_class_scope = env->class_scope;
                        env->class_scope = class_scope;

                        // Store class scope BEFORE executing body for $class::var lookups
                        puppet_hash_set(env->class_scopes, class_name, strlen(class_name), (puppet_value_t *)class_scope);

                        // Set $title and $name to the class's own name (Puppet semantics)
                        puppet_value_t *cls_title = puppet_value_create_string(class_name, strlen(class_name));
                        puppet_scope_set_var(class_scope, "title", cls_title);
                        puppet_scope_set_var(class_scope, "name", puppet_value_copy(cls_title));

                        // Set class parameters using pre-evaluated values or defaults
                        for (size_t pi = 0; pi < param_count; pi++) {
                            puppet_param_t *param = &class_def->data.class_def.params.params[pi];
                            const char *param_name = param->name.data;
                            puppet_value_t *param_value;

                            if (attr_found[pi]) {
                                // Use pre-evaluated value from caller's scope
                                param_value = pre_eval_values[pi];
                            } else {
                                // Try Automatic Parameter Lookup (APL) from Hiera
                                param_value = puppet_apl_lookup(class_name, param_name, env);
                                if (!param_value) {
                                    // Fall back to default value if APL didn't find anything
                                    if (param->default_value) {
                                        param_value = puppet_eval_expr(param->default_value, env);
                                    } else {
                                        param_value = puppet_value_create_undef();
                                    }
                                }
                            }

                            puppet_scope_set_var(class_scope, param_name, param_value);
                        }

                        // Clean up temporary arrays
                        if (pre_eval_values) puppet_free(pre_eval_values);
                        if (attr_found) puppet_free(attr_found);

                        // Set caller_module_name for hiera lookups (extract module from class name)
                        char *old_caller_module_res = env->caller_module_name;
                        const char *sep_res = strstr(class_name, "::");
                        if (sep_res) {
                            size_t module_len = sep_res - class_name;
                            env->caller_module_name = puppet_malloc(module_len + 1);
                            memcpy(env->caller_module_name, class_name, module_len);
                            env->caller_module_name[module_len] = '\0';
                        } else {
                            env->caller_module_name = puppet_strdup(class_name);
                        }

                        // Execute class body
                        if (puppet_verbose) fprintf(stderr, "Including class: %s\n", class_name);
                        puppet_exec_stmt_list(&class_def->data.class_def.body, env);

                        // Restore caller_module_name
                        puppet_free(env->caller_module_name);
                        env->caller_module_name = old_caller_module_res;

                        // Add to catalog
                        if (env->build_catalog && env->catalog) {
                            puppet_catalog_add_class(env->catalog, class_name);
                        }

                        // Cleanup - pop scope but don't destroy (it's stored in class_scopes)
                        env->class_scope = old_class_scope;
                        (void)puppet_scope_pop(env);  // Pop but don't destroy
                        puppet_value_destroy(title_val);
                    }
                }
                break;
            }

            // Handle virtual resources - evaluate and store attributes now
            if (stmt->data.resource.style == PUPPET_RES_VIRTUAL) {
                for (size_t i = 0; i < stmt->data.resource.instance_count; i++) {
                    puppet_resource_instance_t *instance = &stmt->data.resource.instances[i];
                    if (instance->title) {
                        puppet_value_t *title_val = puppet_eval_expr(instance->title, env);
                        const char *title_str = puppet_value_to_string(title_val);

                        // Build resource identifier
                        size_t res_id_len = strlen(stmt->data.resource.type.data) + strlen(title_str) + 3;
                        char *resource_id = puppet_malloc(res_id_len);
                        snprintf(resource_id, res_id_len, "%s[%s]", stmt->data.resource.type.data, title_str);

                        // Check for duplicate virtual resource
                        puppet_value_t *existing = puppet_hash_get(env->virtual_resources,
                                                                   resource_id, strlen(resource_id));
                        if (existing) {
                            puppet_debug("Virtual resource %s already declared", resource_id);
                            puppet_free(resource_id);
                            puppet_value_destroy(title_val);
                            continue;
                        }

                        // Create pre-evaluated virtual resource structure
                        puppet_virtual_resource_t *vres = puppet_calloc(1, sizeof(puppet_virtual_resource_t));
                        vres->type = puppet_strdup(stmt->data.resource.type.data);
                        vres->title = puppet_strdup(title_str);
                        vres->realized = false;

                        // Evaluate and store all attributes NOW (while scope is correct)
                        vres->attr_count = instance->attr_count;
                        if (vres->attr_count > 0) {
                            vres->attrs = puppet_calloc(vres->attr_count, sizeof(puppet_virtual_attr_t));
                            for (size_t j = 0; j < instance->attr_count; j++) {
                                if (instance->attributes[j].name.data) {
                                    vres->attrs[j].name = puppet_strdup(instance->attributes[j].name.data);
                                    vres->attrs[j].value = puppet_eval_expr(instance->attributes[j].value, env);
                                }
                            }
                        }

                        // Store pointer to virtual resource struct
                        puppet_value_t *vres_ptr = puppet_calloc(1, sizeof(puppet_value_t));
                        vres_ptr->type = PUPPET_VALUE_UNDEF;
                        vres_ptr->data.string.data = (char*)vres;
                        puppet_hash_set(env->virtual_resources, resource_id, strlen(resource_id), vres_ptr);

                        puppet_debug("Stored virtual resource: %s (with %zu attrs)", resource_id, vres->attr_count);

                        /* Check if a pending realize exists for this resource */
                        if (env->pending_realizes) {
                            puppet_value_t *pending = puppet_hash_get(env->pending_realizes,
                                resource_id, strlen(resource_id));
                            if (pending && !vres->realized) {
                                /* Auto-realize: add to resource catalog */
                                puppet_value_t *marker = puppet_value_create_bool(true);
                                puppet_hash_set(env->resource_catalog, resource_id, strlen(resource_id), marker);

                                if (env->build_catalog && env->catalog) {
                                    puppet_catalog_param_t *params = NULL;
                                    if (vres->attr_count > 0) {
                                        params = puppet_calloc(vres->attr_count, sizeof(puppet_catalog_param_t));
                                        for (size_t j = 0; j < vres->attr_count; j++) {
                                            if (vres->attrs[j].name) {
                                                params[j].name = puppet_strdup(vres->attrs[j].name);
                                                params[j].value = puppet_value_copy(vres->attrs[j].value);
                                            }
                                        }
                                    }
                                    puppet_catalog_add_resource(env->catalog,
                                        vres->type, vres->title, params, vres->attr_count, NULL, 0);
                                    puppet_apply_current_tags(env, vres->type, vres->title);
                                }

                                vres->realized = true;
                                puppet_debug("Auto-realized pending virtual resource: %s", resource_id);
                            }
                        }

                        puppet_free(resource_id);
                        puppet_value_destroy(title_val);
                    }
                }
                break;  /* Virtual resources are not applied now */
            }

            /* Handle exported resources - store to PuppetDB and also as virtual */
            if (stmt->data.resource.style == PUPPET_RES_EXPORTED) {
                for (size_t i = 0; i < stmt->data.resource.instance_count; i++) {
                    puppet_resource_instance_t *instance = &stmt->data.resource.instances[i];
                    if (instance->title) {
                        puppet_value_t *title_val = puppet_eval_expr(instance->title, env);
                        const char *title_str = puppet_value_to_string(title_val);

                        /* Build resource identifier */
                        size_t res_id_len = strlen(stmt->data.resource.type.data) + strlen(title_str) + 3;
                        char *resource_id = puppet_malloc(res_id_len);
                        snprintf(resource_id, res_id_len, "%s[%s]", stmt->data.resource.type.data, title_str);

                        /* Create pre-evaluated exported resource structure (like virtual) */
                        puppet_virtual_resource_t *vres = puppet_calloc(1, sizeof(puppet_virtual_resource_t));
                        vres->type = puppet_strdup(stmt->data.resource.type.data);
                        vres->title = puppet_strdup(title_str);
                        vres->realized = false;

                        /* Evaluate and store all attributes */
                        vres->attr_count = instance->attr_count;
                        if (vres->attr_count > 0) {
                            vres->attrs = puppet_calloc(vres->attr_count, sizeof(puppet_virtual_attr_t));
                            for (size_t j = 0; j < instance->attr_count; j++) {
                                if (instance->attributes[j].name.data) {
                                    vres->attrs[j].name = puppet_strdup(instance->attributes[j].name.data);
                                    vres->attrs[j].value = puppet_eval_expr(instance->attributes[j].value, env);
                                }
                            }
                        }

                        /* Store locally for potential local collection */
                        puppet_value_t *vres_ptr = puppet_calloc(1, sizeof(puppet_value_t));
                        vres_ptr->type = PUPPET_VALUE_UNDEF;
                        vres_ptr->data.string.data = (char*)vres;
                        puppet_hash_set(env->exported_resources, resource_id, strlen(resource_id), vres_ptr);

                        /* Store to PuppetDB if available */
                        if (env->puppetdb && env->catalog && env->catalog->certname) {
                            /* Build JSON for parameters using proper JSON serialization */
                            json_buffer_t *buf = json_buffer_create();
                            json_buffer_append_char(buf, '{');
                            bool first_param = true;
                            for (size_t j = 0; j < vres->attr_count; j++) {
                                if (vres->attrs[j].name && vres->attrs[j].value) {
                                    if (!first_param) {
                                        json_buffer_append_char(buf, ',');
                                    }
                                    first_param = false;
                                    json_buffer_append_string(buf, vres->attrs[j].name);
                                    json_buffer_append_char(buf, ':');
                                    puppet_value_to_json(buf, vres->attrs[j].value, 0);
                                }
                            }
                            json_buffer_append_char(buf, '}');
                            const char *params_json = buf->data;

                            /* Store to PuppetDB */
                            int rc = puppetdb_store_exported(env->puppetdb, env->catalog->certname,
                                                            vres->type, vres->title, params_json, "[]");
                            json_buffer_destroy(buf);
                            if (rc == 0) {
                                puppet_debug("Exported resource %s to PuppetDB from %s",
                                            resource_id, env->catalog->certname);
                            } else {
                                puppet_warn("Failed to export %s to PuppetDB", resource_id);
                            }
                        } else {
                            puppet_debug("Stored exported resource locally: %s (PuppetDB not available)",
                                        resource_id);
                        }

                        puppet_free(resource_id);
                        puppet_value_destroy(title_val);
                    }
                }
                break;  /* Exported resources are stored, not applied now */
            }

            /* Check if this is a defined type - execute its body if so */
            {
                puppet_value_t *define_ptr = puppet_hash_get(env->define_types,
                    stmt->data.resource.type.data, strlen(stmt->data.resource.type.data));

                /* If not found and we're inside a class scope, try fully qualified name */
                char *fq_lookup_name = NULL;
                if (!define_ptr && env->class_scope && env->class_scope->name.data &&
                    !strchr(stmt->data.resource.type.data, ':')) {
                    /* Build fully qualified name: class_name::type_name */
                    size_t class_len = strlen(env->class_scope->name.data);
                    size_t type_len = strlen(stmt->data.resource.type.data);
                    fq_lookup_name = puppet_malloc(class_len + 2 + type_len + 1);
                    snprintf(fq_lookup_name, class_len + 2 + type_len + 1, "%s::%s",
                             env->class_scope->name.data, stmt->data.resource.type.data);
                    define_ptr = puppet_hash_get(env->define_types, fq_lookup_name, strlen(fq_lookup_name));
                }

                /* Try to autoload the define if not already registered */
                if (!define_ptr && env->prog->loader && strchr(stmt->data.resource.type.data, ':')) {
                    puppet_stmt_t *loaded_def = puppet_loader_load_define(env->prog->loader,
                        stmt->data.resource.type.data);
                    if (loaded_def) {
                        /* Register the loaded define */
                        puppet_debug("Autoloaded defined type: %s", stmt->data.resource.type.data);
                        puppet_value_t *stmt_ptr = puppet_calloc(1, sizeof(puppet_value_t));
                        stmt_ptr->type = PUPPET_VALUE_UNDEF;
                        stmt_ptr->data.string.data = (char*)loaded_def;
                        puppet_hash_set(env->define_types, stmt->data.resource.type.data,
                                       strlen(stmt->data.resource.type.data), stmt_ptr);
                        define_ptr = stmt_ptr;
                    } else {
                        /* Namespaced type that can't be resolved — real Puppet would
                         * fail the catalog with "Unknown resource type". Emit an error
                         * so typos and stale references to deleted modules don't slip by. */
                        puppet_error_at(stmt->loc,
                            "Unknown resource type: '%s'",
                            stmt->data.resource.type.data);
                        puppet_env_increment_error(env);
                    }
                }

                if (define_ptr) {
                    puppet_stmt_t *define_stmt = (puppet_stmt_t *)define_ptr->data.string.data;
                    puppet_deadcode_mark_define_used(env->prog->deadcode, stmt->data.resource.type.data);

                    /* Execute each instance of this defined type */
                    for (size_t i = 0; i < stmt->data.resource.instance_count; i++) {
                        puppet_resource_instance_t *instance = &stmt->data.resource.instances[i];
                        if (!instance->title) continue;

                        puppet_value_t *title_val = puppet_eval_expr(instance->title, env);

                        /* Handle array titles - expand into multiple defined type instances */
                        size_t title_count = 1;
                        puppet_value_t **titles = NULL;

                        if (title_val->type == PUPPET_VALUE_ARRAY) {
                            title_count = title_val->data.array->count;
                            titles = title_val->data.array->items;
                        }

                        for (size_t t = 0; t < title_count; t++) {
                        const char *title_str;
                        if (titles) {
                            title_str = puppet_value_to_string(titles[t]);
                        } else {
                            title_str = puppet_value_to_string(title_val);
                        }

                        /* Check for duplicate */
                        size_t res_id_len = strlen(stmt->data.resource.type.data) + strlen(title_str) + 3;
                        char *resource_id = puppet_malloc(res_id_len);
                        snprintf(resource_id, res_id_len, "%s[%s]", stmt->data.resource.type.data, title_str);

                        puppet_value_t *existing = puppet_hash_get(env->resource_catalog,
                                                                   resource_id, strlen(resource_id));
                        if (existing) {
                            if (env->class_reexecuting) {
                                // During class re-execution, allow resource overwrites
                                puppet_debug("Re-executing defined type %s (overwriting)", resource_id);
                            } else {
                                puppet_error_at(stmt->loc, "Duplicate declaration - %s is already declared", resource_id);
                                puppet_env_increment_error(env);
                                puppet_free(resource_id);
                                puppet_value_destroy(title_val);
                                continue;
                            }
                        }

                        puppet_value_t *marker = puppet_value_create_bool(true);
                        puppet_hash_set(env->resource_catalog, resource_id, strlen(resource_id), marker);

                        /* Item 30: per-tree resource policy (deprecated repos etc.) */
                        puppet_policy_check_resource(env, stmt->data.resource.type.data,
                                                     title_str, stmt->loc);

                        puppet_debug("Deferring defined type %s with title: %s",
                                    stmt->data.resource.type.data, title_str);

                        /* Defer execution: grow array if needed */
                        if (env->deferred_define_count >= env->deferred_define_capacity) {
                            size_t new_cap = env->deferred_define_capacity ? env->deferred_define_capacity * 2 : 16;
                            env->deferred_defines = puppet_realloc(env->deferred_defines,
                                new_cap * sizeof(puppet_deferred_define_t));
                            env->deferred_define_capacity = new_cap;
                        }
                        puppet_deferred_define_t *deferred = &env->deferred_defines[env->deferred_define_count++];
                        deferred->define_stmt = define_stmt;
                        deferred->resource_stmt = stmt;
                        deferred->instance = instance;
                        deferred->type_name = puppet_strdup(stmt->data.resource.type.data);
                        deferred->title = puppet_strdup(title_str);
                        deferred->resource_id = resource_id;  /* transfer ownership */
                        deferred->override_attrs = puppet_calloc(1, sizeof(puppet_hash_t));
                        deferred->override_attrs->bucket_count = 8;
                        deferred->override_attrs->buckets = puppet_calloc(8, sizeof(puppet_hash_entry_t*));
                        deferred->class_reexecuting = env->class_reexecuting;
                        /* Snapshot the caller's scope so that lazy
                         * attribute evaluation can still see the local
                         * variables that were in scope at declaration
                         * time. */
                        deferred->caller_scope = env->current_scope;

                        }  /* End of array title expansion loop */

                        puppet_value_destroy(title_val);
                    }
                    if (fq_lookup_name) puppet_free(fq_lookup_name);
                    break;  /* Defined type handled */
                }
                /* Free fq_lookup_name if we didn't find a defined type */
                if (fq_lookup_name) puppet_free(fq_lookup_name);
            }

            /* Unnamespaced, non-define resource: validate it's a known
             * built-in or Ruby type. Catches typos like 'fiel', 'packge',
             * 'servic'. The namespaced counterpart is handled above in
             * the define-loader branch. */
            if (!strchr(stmt->data.resource.type.data, ':')) {
                size_t tlen = strlen(stmt->data.resource.type.data);
                char *type_lower = puppet_malloc(tlen + 1);
                for (size_t i = 0; i < tlen; i++) {
                    type_lower[i] = tolower((unsigned char)stmt->data.resource.type.data[i]);
                }
                type_lower[tlen] = '\0';
                bool known = puppet_type_is_known(env, type_lower);
                if (!known && env->prog->loader) {
                    /* Try autoload: module 'foo' owning define foo in
                     * foo/manifests/init.pp is valid unnamespaced usage. */
                    puppet_stmt_t *loaded = puppet_loader_load_define(env->prog->loader,
                        stmt->data.resource.type.data);
                    if (loaded) {
                        puppet_value_t *stmt_ptr = puppet_calloc(1, sizeof(puppet_value_t));
                        stmt_ptr->type = PUPPET_VALUE_UNDEF;
                        stmt_ptr->data.string.data = (char*)loaded;
                        puppet_hash_set(env->define_types,
                            stmt->data.resource.type.data,
                            strlen(stmt->data.resource.type.data), stmt_ptr);
                        /* Re-enter the define path by restarting the stmt. */
                        puppet_free(type_lower);
                        puppet_exec_stmt(stmt, env);
                        break;
                    }
                }
                if (!known) {
                    puppet_error_at(stmt->loc,
                        "Unknown resource type: '%s'",
                        stmt->data.resource.type.data);
                    puppet_env_increment_error(env);
                    puppet_free(type_lower);
                    break;
                }
                puppet_free(type_lower);
            }

            // Normal resource execution
            // Evaluate resource titles and check for duplicates
            for (size_t i = 0; i < stmt->data.resource.instance_count; i++) {
                puppet_resource_instance_t *instance = &stmt->data.resource.instances[i];
                if (instance->title) {
                    puppet_value_t *title_val = puppet_eval_expr(instance->title, env);

                    // Handle array titles - expand into multiple resources
                    size_t title_count = 1;
                    puppet_value_t **titles = NULL;

                    if (title_val->type == PUPPET_VALUE_ARRAY) {
                        title_count = title_val->data.array->count;
                        titles = title_val->data.array->items;
                    }

                    for (size_t t = 0; t < title_count; t++) {
                        const char *title_str;
                        if (titles) {
                            title_str = puppet_value_to_string(titles[t]);
                        } else {
                            title_str = puppet_value_to_string(title_val);
                        }

                        // Build resource identifier (type::title)
                        size_t res_id_len = strlen(stmt->data.resource.type.data) + strlen(title_str) + 3;
                        char *resource_id = puppet_malloc(res_id_len);
                        snprintf(resource_id, res_id_len, "%s[%s]", stmt->data.resource.type.data, title_str);

                        // Check for duplicate resource
                        puppet_value_t *existing = puppet_hash_get(env->resource_catalog,
                                                                   resource_id, strlen(resource_id));
                        if (existing) {
                            if (env->class_reexecuting) {
                                // During class re-execution, allow resource overwrites
                                puppet_debug("Re-executing resource %s (overwriting)", resource_id);
                            } else {
                                puppet_error_at(stmt->loc, "Duplicate declaration - %s is already declared", resource_id);
                                fprintf(stderr, "       Resource titles must be unique within their type\n");
                                puppet_env_increment_error(env);
                                puppet_free(resource_id);
                                continue;  // Skip this duplicate resource
                            }
                        }

                        // Add to duplicate detection catalog
                        puppet_value_t *marker = puppet_value_create_bool(true);
                        puppet_hash_set(env->resource_catalog, resource_id, strlen(resource_id), marker);

                        /* Item 30: per-tree resource policy (deprecated repos etc.) */
                        puppet_policy_check_resource(env, stmt->data.resource.type.data,
                                                     title_str, stmt->loc);

                        puppet_debug("  Title: %s", title_str);

                        // Check if this is the template target for output
                        bool is_template_target = (env->template_output_target &&
                                                   strcmp(title_str, env->template_output_target) == 0 &&
                                                   strcmp(stmt->data.resource.type.data, "file") == 0);

                        // Collect parameters for catalog (dynamic allocation to handle splat)
                        puppet_catalog_param_t *params = NULL;
                        size_t param_capacity = instance->attr_count + 16;  // Extra space for splat expansion
                        size_t param_idx = 0;
                        if (env->build_catalog) {
                            params = puppet_calloc(param_capacity, sizeof(puppet_catalog_param_t));
                        }

                        // Helper to add a parameter (grows array if needed)
                        #define ADD_PARAM(name_str, val) do { \
                            if (env->build_catalog && params) { \
                                if (param_idx >= param_capacity) { \
                                    param_capacity *= 2; \
                                    params = puppet_realloc(params, param_capacity * sizeof(puppet_catalog_param_t)); \
                                } \
                                params[param_idx].name = puppet_strdup(name_str); \
                                params[param_idx].value = puppet_value_copy(val); \
                                param_idx++; \
                            } \
                        } while(0)

                        // Show attributes for this instance
                        for (size_t j = 0; j < instance->attr_count; j++) {
                            // Handle splat operator (* => hash) - NULL name means splat
                            if (!instance->attributes[j].name.data) {
                                puppet_value_t *splat_val = puppet_eval_expr(instance->attributes[j].value, env);
                                if (splat_val && splat_val->type == PUPPET_VALUE_HASH) {
                                    // Expand hash entries as individual attributes
                                    puppet_hash_t *hash = splat_val->data.hash;
                                    for (size_t b = 0; b < hash->bucket_count; b++) {
                                        for (puppet_hash_entry_t *e = hash->buckets[b]; e; e = e->next) {
                                            const char *attr_str = puppet_value_to_string(e->value);
                                            puppet_debug("    %s => %s (from splat)", e->key.data, attr_str);
                                            ADD_PARAM(e->key.data, e->value);
                                        }
                                    }
                                }
                                if (splat_val) puppet_value_destroy(splat_val);
                                continue;
                            }

                            puppet_value_t *attr_val = puppet_eval_expr(instance->attributes[j].value, env);
                            const char *attr_str = puppet_value_to_string(attr_val);
                            puppet_debug("    %s => %s", instance->attributes[j].name.data, attr_str);

                            // If this is template output mode and we found the content attribute
                            // Output goes to stdout (clean, for piping) - no markers
                            if (is_template_target && strcmp(instance->attributes[j].name.data, "content") == 0) {
                                if (attr_val->type == PUPPET_VALUE_STRING) {
                                    printf("%s", attr_val->data.string.data);
                                    env->template_output_found = true;
                                }
                            }

                            // Store in catalog params
                            ADD_PARAM(instance->attributes[j].name.data, attr_val);

                            puppet_value_destroy(attr_val);
                        }
                        #undef ADD_PARAM

                        // Add to resource catalog if building
                        if (env->build_catalog && env->catalog) {
                            int add_result = puppet_catalog_add_resource(env->catalog,
                                                        stmt->data.resource.type.data,
                                                        title_str,
                                                        params,
                                                        param_idx,
                                                        stmt->loc.filename,
                                                        stmt->loc.line);  // Use actual count, not attr_count
                            // If duplicate and we're re-executing a class, update the resource instead
                            if (add_result == -1 && env->class_reexecuting) {
                                puppet_catalog_update_resource(env->catalog,
                                                               stmt->data.resource.type.data,
                                                               title_str,
                                                               params,
                                                               param_idx);
                            }
                            /* Apply current scope tags */
                            puppet_apply_current_tags(env, stmt->data.resource.type.data, title_str);
                        }

                        puppet_free(resource_id);
                    }
                    puppet_value_destroy(title_val);
                }
            }
            break;

        case PUPPET_STMT_IF: {
            // Execute if/elsif/else chain
            puppet_if_branch_t *branch = stmt->data.if_stmt.branches;
            bool executed = false;

            while (branch && !executed) {
                env->in_truthiness_check++;
                puppet_value_t *cond = puppet_eval_expr(branch->condition, env);
                env->in_truthiness_check--;
                bool is_true = false;

                // Evaluate truthiness: false and undef are falsy, everything else is truthy
                if (cond) {
                    if (cond->type == PUPPET_VALUE_BOOL) {
                        is_true = cond->data.boolean;
                    } else if (cond->type == PUPPET_VALUE_UNDEF) {
                        is_true = false;
                    } else {
                        is_true = true;
                    }
                    puppet_value_destroy(cond);
                }

                if (is_true) {
                    puppet_exec_stmt_list(&branch->body, env);
                    executed = true;
                }
                branch = branch->next;
            }

            // Execute else branch if no condition matched
            if (!executed && stmt->data.if_stmt.else_body) {
                puppet_exec_stmt_list(stmt->data.if_stmt.else_body, env);
            }
            break;
        }

        case PUPPET_STMT_UNLESS: {
            // Execute unless (inverse of if)
            env->in_truthiness_check++;
            puppet_value_t *cond = puppet_eval_expr(stmt->data.unless_stmt.condition, env);
            env->in_truthiness_check--;
            bool is_false = true;

            if (cond) {
                if (cond->type == PUPPET_VALUE_BOOL) {
                    is_false = !cond->data.boolean;
                } else if (cond->type == PUPPET_VALUE_UNDEF) {
                    is_false = true;
                } else {
                    is_false = false;
                }
                puppet_value_destroy(cond);
            }

            if (is_false) {
                puppet_exec_stmt_list(&stmt->data.unless_stmt.body, env);
            } else if (stmt->data.unless_stmt.else_body) {
                puppet_exec_stmt_list(stmt->data.unless_stmt.else_body, env);
            }
            break;
        }

        case PUPPET_STMT_CASE: {
            // Execute case statement
            puppet_value_t *expr_val = puppet_eval_expr(stmt->data.case_stmt.expr, env);
            bool matched = false;

            // First pass: check non-default cases
            puppet_case_when_t *default_when = NULL;
            for (size_t i = 0; i < stmt->data.case_stmt.when_count && !matched; i++) {
                puppet_case_when_t *when = &stmt->data.case_stmt.whens[i];

                // Skip default case on first pass (test == NULL means default)
                if (!when->test) {
                    default_when = when;
                    continue;
                }

                puppet_value_t *test_val = puppet_eval_expr(when->test, env);

                // Check for match (using equality comparison or regex match)
                bool is_match = false;
                if (expr_val && test_val) {
                    if (expr_val->type == test_val->type) {
                        switch (expr_val->type) {
                            case PUPPET_VALUE_UNDEF:
                                is_match = true;  // Both are undef
                                break;
                            case PUPPET_VALUE_BOOL:
                                is_match = (expr_val->data.boolean == test_val->data.boolean);
                                break;
                            case PUPPET_VALUE_NUMBER:
                                is_match = (expr_val->data.number == test_val->data.number);
                                break;
                            case PUPPET_VALUE_STRING:
                                // Case-insensitive string comparison for case statements
                                is_match = (expr_val->data.string.len == test_val->data.string.len &&
                                           strncasecmp(expr_val->data.string.data, test_val->data.string.data,
                                                  expr_val->data.string.len) == 0);
                                break;
                            default:
                                break;
                        }
                    } else if (test_val->type == PUPPET_VALUE_REGEXP &&
                               expr_val->type == PUPPET_VALUE_STRING) {
                        // Regex match: test pattern against string value
                        regex_t regex;
                        int ret = puppet_regcomp(&regex, test_val->data.regexp.data, REG_EXTENDED | REG_NOSUB);
                        if (ret == 0) {
                            ret = regexec(&regex, expr_val->data.string.data, 0, NULL, 0);
                            is_match = (ret == 0);
                            regfree(&regex);
                        }
                    }
                }

                if (test_val) puppet_value_destroy(test_val);

                if (is_match) {
                    puppet_exec_stmt_list(&when->body, env);
                    matched = true;
                }
            }

            // Execute default branch if no when matched
            // Check both the legacy default_body and when entries with NULL test
            if (!matched) {
                if (default_when) {
                    puppet_exec_stmt_list(&default_when->body, env);
                    matched = true;
                } else if (stmt->data.case_stmt.default_body) {
                    puppet_exec_stmt_list(stmt->data.case_stmt.default_body, env);
                    matched = true;
                }
            }

            if (expr_val) puppet_value_destroy(expr_val);
            break;
        }

        case PUPPET_STMT_RESOURCE_COLLECTOR:
            puppet_exec_collector(stmt, env);
            break;

        case PUPPET_STMT_RESOURCE_DEFAULT:
            /* Resource defaults (Type { attr => value }) set default values for resources.
             * TODO: Store defaults in scope and apply when creating resources.
             * For now, just log and continue - the resources will use their own defaults.
             */
            puppet_debug("Resource default for %s (not yet applied)",
                        stmt->data.resource_default.type.data);
            break;

        case PUPPET_STMT_RESOURCE_OVERRIDE: {
            /* Resource override: Type['title'] { attr => value } */
            if (!stmt->data.resource_override.reference) {
                puppet_error_at(stmt->loc, "Resource override: missing reference");
                puppet_env_increment_error(env);
                break;
            }

            puppet_expr_t *ref = stmt->data.resource_override.reference;
            if (ref->type != PUPPET_EXPR_RESOURCE_REF) {
                puppet_error_at(stmt->loc, "Resource override: invalid reference type");
                puppet_env_increment_error(env);
                break;
            }

            /* Get the resource type and convert to lowercase */
            const char *res_type_orig = ref->data.resource_ref.type.data;
            if (!res_type_orig) {
                puppet_error_at(stmt->loc, "Resource override: missing resource type");
                puppet_env_increment_error(env);
                break;
            }

            /* Normalize type to lowercase (Puppet types are case-insensitive) */
            size_t type_len = strlen(res_type_orig);
            char *res_type = puppet_malloc(type_len + 1);
            for (size_t i = 0; i < type_len; i++) {
                res_type[i] = tolower((unsigned char)res_type_orig[i]);
            }
            res_type[type_len] = '\0';

            /* Evaluate title expression */
            puppet_value_t *title_val = puppet_eval_expr(ref->data.resource_ref.title, env);
            const char *title_str = puppet_value_to_string(title_val);

            /* Build resource ID for lookup */
            size_t res_id_len = type_len + strlen(title_str) + 3;
            char *resource_id = puppet_malloc(res_id_len);
            snprintf(resource_id, res_id_len, "%s[%s]", res_type, title_str);

            /* Check if resource exists in resource catalog */
            puppet_value_t *existing = puppet_hash_get(env->resource_catalog,
                                                       resource_id, strlen(resource_id));

            /* Also check virtual and exported resources if not found in regular catalog */
            puppet_virtual_resource_t *vres = NULL;
            if (!existing && env->virtual_resources) {
                puppet_value_t *vres_val = puppet_hash_get(env->virtual_resources,
                                                           resource_id, strlen(resource_id));
                if (vres_val) {
                    vres = (puppet_virtual_resource_t *)vres_val->data.string.data;
                }
            }
            if (!existing && !vres && env->exported_resources) {
                puppet_value_t *vres_val = puppet_hash_get(env->exported_resources,
                                                           resource_id, strlen(resource_id));
                if (vres_val) {
                    vres = (puppet_virtual_resource_t *)vres_val->data.string.data;
                }
            }

            /* Also check deferred defines (always search - deferred defines have
             * a marker in resource_catalog so 'existing' may be true) */
            puppet_deferred_define_t *deferred_def = NULL;
            for (size_t di = 0; di < env->deferred_define_count; di++) {
                if (strcmp(env->deferred_defines[di].resource_id, resource_id) == 0) {
                    deferred_def = &env->deferred_defines[di];
                    break;
                }
            }

            if (!existing && !vres && !deferred_def) {
                puppet_error_at(stmt->loc, "Resource override: %s has not been declared", resource_id);
                puppet_env_increment_error(env);
                puppet_free(res_type);
                puppet_free(resource_id);
                puppet_value_destroy(title_val);
                break;
            }

            if (deferred_def) {
                /* Apply override to deferred define - store evaluated attrs */
                for (size_t i = 0; i < stmt->data.resource_override.attr_count; i++) {
                    const char *attr_name = stmt->data.resource_override.attributes[i].name.data;
                    puppet_value_t *attr_val = puppet_eval_expr(
                        stmt->data.resource_override.attributes[i].value, env);
                    puppet_hash_set(deferred_def->override_attrs,
                                   attr_name, strlen(attr_name), attr_val);
                }
                puppet_debug("Applied resource override to deferred define %s", resource_id);
            }

            if (vres) {
                /* Apply override to virtual/exported resource */
                for (size_t i = 0; i < stmt->data.resource_override.attr_count; i++) {
                    const char *attr_name = stmt->data.resource_override.attributes[i].name.data;
                    puppet_value_t *attr_val = puppet_eval_expr(
                        stmt->data.resource_override.attributes[i].value, env);

                    /* Check if attribute already exists */
                    bool found = false;
                    for (size_t j = 0; j < vres->attr_count; j++) {
                        if (vres->attrs[j].name && strcmp(vres->attrs[j].name, attr_name) == 0) {
                            /* Replace existing value */
                            puppet_value_destroy(vres->attrs[j].value);
                            vres->attrs[j].value = puppet_value_copy(attr_val);
                            found = true;
                            break;
                        }
                    }

                    if (!found) {
                        /* Add new attribute */
                        size_t new_count = vres->attr_count + 1;
                        vres->attrs = puppet_realloc(vres->attrs,
                            new_count * sizeof(puppet_virtual_attr_t));
                        vres->attrs[vres->attr_count].name = puppet_strdup(attr_name);
                        vres->attrs[vres->attr_count].value = puppet_value_copy(attr_val);
                        vres->attr_count = new_count;
                    }

                    puppet_value_destroy(attr_val);
                }

                puppet_debug("Applied resource override to virtual %s", resource_id);
            }

            /* Find resource in catalog and update attributes */
            if (existing && env->build_catalog && env->catalog) {
                puppet_catalog_resource_t *cat_res = puppet_catalog_find_resource(
                    env->catalog, res_type, title_str);

                if (cat_res) {
                    /* Apply override attributes */
                    for (size_t i = 0; i < stmt->data.resource_override.attr_count; i++) {
                        const char *attr_name = stmt->data.resource_override.attributes[i].name.data;
                        puppet_value_t *attr_val = puppet_eval_expr(
                            stmt->data.resource_override.attributes[i].value, env);

                        /* Check if attribute already exists */
                        bool found = false;
                        for (size_t j = 0; j < cat_res->param_count; j++) {
                            if (strcmp(cat_res->parameters[j].name, attr_name) == 0) {
                                /* Replace existing value */
                                puppet_value_destroy(cat_res->parameters[j].value);
                                cat_res->parameters[j].value = puppet_value_copy(attr_val);
                                found = true;
                                break;
                            }
                        }

                        if (!found) {
                            /* Add new attribute */
                            size_t new_count = cat_res->param_count + 1;
                            cat_res->parameters = puppet_realloc(cat_res->parameters,
                                new_count * sizeof(puppet_catalog_param_t));
                            cat_res->parameters[cat_res->param_count].name = puppet_strdup(attr_name);
                            cat_res->parameters[cat_res->param_count].value = puppet_value_copy(attr_val);
                            cat_res->param_count = new_count;
                        }

                        puppet_value_destroy(attr_val);
                    }

                    puppet_debug("Applied resource override to %s", resource_id);
                }
            }

            puppet_free(res_type);
            puppet_free(resource_id);
            puppet_value_destroy(title_val);
            break;
        }

        case PUPPET_STMT_RESOURCE_CHAIN: {
            /* Execute both sides of the chain */
            puppet_stmt_t *left_stmt = stmt->data.chain.left;
            puppet_stmt_t *right_stmt = stmt->data.chain.right;

            /* Execute left side (may add resources to catalog) */
            if (left_stmt) {
                puppet_exec_stmt(left_stmt, env);
            }

            /* Execute right side (may add resources to catalog) */
            if (right_stmt) {
                puppet_exec_stmt(right_stmt, env);
            }

            /* Now add edges from left resources to right resources */
            if (!env->catalog || !left_stmt || !right_stmt) {
                break;
            }

            /* Determine relationship type */
            puppet_relationship_t rel = (stmt->data.chain.type == CHAIN_NOTIFY)
                ? PUPPET_REL_NOTIFY : PUPPET_REL_BEFORE;

            /* Helper: extract resource refs from a statement */
            /* For expression statements with resource refs */
            char *left_type = NULL, *left_title = NULL;
            char *right_type = NULL, *right_title = NULL;

            /* Extract left resource reference */
            /* For nested chains, use the rightmost resource */
            puppet_stmt_t *left_ref = left_stmt;
            while (left_ref && left_ref->type == PUPPET_STMT_RESOURCE_CHAIN) {
                left_ref = left_ref->data.chain.right;
            }

            if (left_ref && left_ref->type == PUPPET_STMT_EXPRESSION &&
                left_ref->data.expr &&
                left_ref->data.expr->type == PUPPET_EXPR_RESOURCE_REF) {

                left_type = puppet_strdup(left_ref->data.expr->data.resource_ref.type.data);
                puppet_value_t *title_val = puppet_eval_expr(
                    left_ref->data.expr->data.resource_ref.title, env);
                if (title_val && title_val->type == PUPPET_VALUE_STRING) {
                    left_title = puppet_strdup(title_val->data.string.data);
                }
                puppet_value_destroy(title_val);

            } else if (left_ref && left_ref->type == PUPPET_STMT_RESOURCE &&
                       left_ref->data.resource.instances &&
                       left_ref->data.resource.instance_count > 0) {
                /* Resource declaration - get type and first title */
                left_type = puppet_strdup(left_ref->data.resource.type.data);
                puppet_value_t *title_val = puppet_eval_expr(
                    left_ref->data.resource.instances[0].title, env);
                if (title_val && title_val->type == PUPPET_VALUE_STRING) {
                    left_title = puppet_strdup(title_val->data.string.data);
                }
                puppet_value_destroy(title_val);
            }

            /* Extract right resource reference */
            if (right_stmt->type == PUPPET_STMT_EXPRESSION &&
                right_stmt->data.expr &&
                right_stmt->data.expr->type == PUPPET_EXPR_RESOURCE_REF) {

                right_type = puppet_strdup(right_stmt->data.expr->data.resource_ref.type.data);
                puppet_value_t *title_val = puppet_eval_expr(
                    right_stmt->data.expr->data.resource_ref.title, env);
                if (title_val && title_val->type == PUPPET_VALUE_STRING) {
                    right_title = puppet_strdup(title_val->data.string.data);
                }
                puppet_value_destroy(title_val);

            } else if (right_stmt->type == PUPPET_STMT_RESOURCE &&
                       right_stmt->data.resource.instances &&
                       right_stmt->data.resource.instance_count > 0) {
                /* Resource declaration - get type and first title */
                right_type = puppet_strdup(right_stmt->data.resource.type.data);
                puppet_value_t *title_val = puppet_eval_expr(
                    right_stmt->data.resource.instances[0].title, env);
                if (title_val && title_val->type == PUPPET_VALUE_STRING) {
                    right_title = puppet_strdup(title_val->data.string.data);
                }
                puppet_value_destroy(title_val);
            }

            /* Add edge if we have both sides */
            if (left_type && left_title && right_type && right_title) {
                puppet_catalog_add_edge(env->catalog,
                    left_type, left_title, right_type, right_title, rel);
                puppet_debug("Added %s edge: %s[%s] -> %s[%s]",
                    rel == PUPPET_REL_NOTIFY ? "notify" : "before",
                    left_type, left_title, right_type, right_title);
            } else {
                puppet_warn("Could not extract resource references from chain");
            }

            puppet_free(left_type);
            puppet_free(left_title);
            puppet_free(right_type);
            puppet_free(right_title);
            break;
        }

        default:
            puppet_warn("Unimplemented statement type: %d", stmt->type);
            break;
    }
}

/**
 * Execute a deferred defined type instance.
 * Parameters are resolved from: override_attrs first, then instance attrs, then defaults.
 */
static void puppet_exec_deferred_define(puppet_deferred_define_t *deferred, puppet_env_t *env) {
    puppet_stmt_t *define_stmt = deferred->define_stmt;
    puppet_stmt_t *resource_stmt = deferred->resource_stmt;
    puppet_resource_instance_t *instance = deferred->instance;

    puppet_debug("Executing deferred defined type %s with title: %s",
                deferred->type_name, deferred->title);

    /* Restore class_reexecuting state from deferral time */
    bool saved_class_reexecuting = env->class_reexecuting;
    env->class_reexecuting = deferred->class_reexecuting;

    /* Temporarily restore the caller's scope as current so that
     * (1) the new define_scope is parented under it, and
     * (2) attribute values that interpolate caller-local variables
     *     (e.g. content => "${x}") resolve them correctly.
     * Without this they look up against whatever scope happens to be
     * current at the end-of-statement-list deferred execution pass,
     * which doesn't see the declaring class's locals. */
    puppet_scope_t *saved_current_scope = env->current_scope;
    puppet_scope_t *parent_scope = deferred->caller_scope
        ? deferred->caller_scope : env->current_scope;
    env->current_scope = parent_scope;

    /* Create new scope for the define execution */
    puppet_scope_t *define_scope = puppet_scope_create(parent_scope,
                                                      deferred->type_name);
    puppet_scope_push(env, define_scope);

    /* Bind $name and $title to the title */
    puppet_value_t *name_val = puppet_value_create_string(deferred->title, strlen(deferred->title));
    puppet_scope_set_var(define_scope, "name", name_val);
    puppet_scope_set_var(define_scope, "title", puppet_value_copy(name_val));

    /* Set $module_name for the define */
    const char *def_type_sep = strstr(deferred->type_name, "::");
    if (def_type_sep) {
        size_t def_type_module_len = def_type_sep - deferred->type_name;
        char *def_type_module = puppet_malloc(def_type_module_len + 1);
        memcpy(def_type_module, deferred->type_name, def_type_module_len);
        def_type_module[def_type_module_len] = '\0';
        puppet_scope_set_var(define_scope, "module_name",
            puppet_value_create_string(def_type_module, def_type_module_len));
        puppet_free(def_type_module);
    } else {
        puppet_scope_set_var(define_scope, "module_name",
            puppet_value_create_string(deferred->type_name,
                                      strlen(deferred->type_name)));
    }

    /* Bind define parameters: override_attrs > instance attrs > defaults */
    for (size_t p = 0; p < define_stmt->data.define.params.count; p++) {
        const char *param_name = define_stmt->data.define.params.params[p].name.data;
        puppet_value_t *param_value = NULL;

        /* 1. Check override attributes first */
        puppet_value_t *override_val = puppet_hash_get(deferred->override_attrs,
            param_name, strlen(param_name));
        if (override_val) {
            param_value = puppet_value_copy(override_val);
        }

        /* 2. Look for matching attribute in resource instance */
        if (!param_value) {
            for (size_t a = 0; a < instance->attr_count; a++) {
                if (instance->attributes[a].name.data &&
                    strcmp(instance->attributes[a].name.data, param_name) == 0) {
                    param_value = puppet_eval_expr(instance->attributes[a].value, env);
                    break;
                }
            }
        }

        /* Puppet semantics: an explicitly-supplied undef attribute value is
         * stripped and the parameter default applies (e.g. a `ensure => $x`
         * where the selector/hiera lookup $x missed). Without this, a typed
         * parameter such as Enum['present','absent'] passed undef fails the
         * type check below even though real Puppet falls back to the default
         * and compiles cleanly. */
        if (param_value && param_value->type == PUPPET_VALUE_UNDEF &&
            define_stmt->data.define.params.params[p].default_value) {
            puppet_value_destroy(param_value);
            param_value = puppet_eval_expr(
                define_stmt->data.define.params.params[p].default_value, env);
        }

        /* 3. Use default value if not provided */
        if (!param_value && define_stmt->data.define.params.params[p].default_value) {
            param_value = puppet_eval_expr(
                define_stmt->data.define.params.params[p].default_value, env);
        }

        /* Type-check the resolved value against the declared
         * constraint. Same rationale as for class params — a
         * `apt::source { 'k8s': repos => '' }` with
         * `String[1] $repos` should fail at compile time, not at
         * runtime on the puppetserver. */
        puppet_param_t *dparam = &define_stmt->data.define.params.params[p];
        if (dparam->type_expr && param_value &&
            !value_matches_type_str(param_value, dparam->type_str.data, env)) {
            char typestr[128];
            snprintf(typestr, sizeof(typestr), "%s", dparam->type_str.data ? dparam->type_str.data : "?");
            puppet_error_at(deferred->instance ? deferred->define_stmt->loc : define_stmt->loc,
                "Defined type %s['%s']: parameter $%s expected %s, got incompatible value",
                deferred->type_name, deferred->title, param_name, typestr);
            puppet_env_increment_error(env);
        }

        if (param_value) {
            puppet_scope_set_var(define_scope, param_name, param_value);
        }
    }

    /* Item 20 diagnostic: under --verbose, trace every descent into a define
     * body with the node, the define[title], and the declaration site. A
     * define body must only ever be entered through a real instance; if a node
     * that should have zero Apt::Ppa[…] still prints a line here, the trace
     * names the declaration that wrongly triggered it. */
    if (puppet_verbose) {
        puppet_location_t dloc = deferred->resource_stmt ? deferred->resource_stmt->loc
                                                          : define_stmt->loc;
        fprintf(stderr, "[define-trace] node %s: entering %s['%s'] (declared at %s:%d)\n",
                env->current_node_certname ? env->current_node_certname : "(default)",
                deferred->type_name ? deferred->type_name : "?",
                deferred->title ? deferred->title : "?",
                dloc.filename ? dloc.filename : "?", dloc.line);
    }

    /* Execute the define body */
    puppet_exec_stmt_list(&define_stmt->data.define.body, env);

    /* Add the define instance to catalog */
    if (env->build_catalog && env->catalog) {
        /* Collect parameters for catalog (instance attrs + overrides merged) */
        size_t max_params = instance->attr_count + deferred->override_attrs->bucket_count;
        puppet_catalog_param_t *params = puppet_calloc(max_params, sizeof(puppet_catalog_param_t));
        size_t param_idx = 0;

        for (size_t j = 0; j < instance->attr_count; j++) {
            if (instance->attributes[j].name.data) {
                const char *attr_name = instance->attributes[j].name.data;
                /* Check if overridden */
                puppet_value_t *override_val = puppet_hash_get(deferred->override_attrs,
                    attr_name, strlen(attr_name));
                puppet_value_t *attr_val;
                if (override_val) {
                    attr_val = puppet_value_copy(override_val);
                } else {
                    attr_val = puppet_eval_expr(instance->attributes[j].value, env);
                }
                params[param_idx].name = puppet_strdup(attr_name);
                params[param_idx].value = puppet_value_copy(attr_val);
                param_idx++;
                puppet_value_destroy(attr_val);
            }
        }

        /* Add override-only attributes (not in instance) */
        for (size_t b = 0; b < deferred->override_attrs->bucket_count; b++) {
            puppet_hash_entry_t *entry = deferred->override_attrs->buckets[b];
            while (entry) {
                /* Check if already added from instance */
                bool already_added = false;
                for (size_t j = 0; j < instance->attr_count; j++) {
                    if (instance->attributes[j].name.data &&
                        strcmp(instance->attributes[j].name.data, entry->key.data) == 0) {
                        already_added = true;
                        break;
                    }
                }
                if (!already_added) {
                    params[param_idx].name = puppet_strdup(entry->key.data);
                    params[param_idx].value = puppet_value_copy(entry->value);
                    param_idx++;
                }
                entry = entry->next;
            }
        }

        int add_result = puppet_catalog_add_resource(env->catalog,
                                    deferred->type_name,
                                    deferred->title,
                                    params,
                                    param_idx,
                                    resource_stmt->loc.filename,
                                    resource_stmt->loc.line);
        if (add_result == -1 && env->class_reexecuting) {
            puppet_catalog_update_resource(env->catalog,
                                           deferred->type_name,
                                           deferred->title,
                                           params,
                                           param_idx);
        }
        puppet_apply_current_tags(env, deferred->type_name, deferred->title);
    }

    /* Pop the define scope */
    puppet_scope_t *popped = puppet_scope_pop(env);
    puppet_scope_destroy(popped);

    /* Restore current_scope (was temporarily swapped to caller_scope) */
    env->current_scope = saved_current_scope;

    /* Restore class_reexecuting state */
    env->class_reexecuting = saved_class_reexecuting;
}

void puppet_exec_stmt_list(puppet_stmt_list_t *stmts, puppet_env_t *env) {
    size_t saved_count = env->deferred_define_count;

    for (size_t i = 0; i < stmts->count; i++) {
        puppet_exec_stmt(stmts->stmts[i], env);
    }

    /* Execute defines deferred during THIS statement list (not outer ones).
     * Use while loop because executing a deferred define may itself defer more. */
    size_t exec_cursor = saved_count;
    while (exec_cursor < env->deferred_define_count) {
        size_t batch_end = env->deferred_define_count;
        for (size_t i = exec_cursor; i < batch_end; i++) {
            puppet_exec_deferred_define(&env->deferred_defines[i], env);
        }
        exec_cursor = batch_end;
    }

    /* Cleanup deferred entries from this level */
    for (size_t i = saved_count; i < env->deferred_define_count; i++) {
        puppet_free(env->deferred_defines[i].type_name);
        puppet_free(env->deferred_defines[i].title);
        puppet_free(env->deferred_defines[i].resource_id);
        /* Free override_attrs hash */
        if (env->deferred_defines[i].override_attrs) {
            for (size_t b = 0; b < env->deferred_defines[i].override_attrs->bucket_count; b++) {
                puppet_hash_entry_t *entry = env->deferred_defines[i].override_attrs->buckets[b];
                while (entry) {
                    puppet_hash_entry_t *next = entry->next;
                    puppet_free(entry->key.data);
                    puppet_value_destroy(entry->value);
                    puppet_free(entry);
                    entry = next;
                }
            }
            puppet_free(env->deferred_defines[i].override_attrs->buckets);
            puppet_free(env->deferred_defines[i].override_attrs);
        }
    }
    env->deferred_define_count = saved_count;
}

void puppet_exec_assignment(const char *var, puppet_expr_t *value, puppet_env_t *env) {
    puppet_value_t *val = puppet_eval_expr(value, env);

    // Use scoped variable assignment (defaults to local scope)
    puppet_env_set_scoped_var(env, var, val, PUPPET_VAR_LOCAL);

    // Debug output
    if (puppet_verbose) {
        const char *val_str;
        char num_buf[64];
        switch (val->type) {
            case PUPPET_VALUE_UNDEF:
                val_str = "undef";
                break;
            case PUPPET_VALUE_BOOL:
                val_str = val->data.boolean ? "true" : "false";
                break;
            case PUPPET_VALUE_STRING:
                val_str = val->data.string.data;
                break;
            case PUPPET_VALUE_NUMBER:
                snprintf(num_buf, sizeof(num_buf), "%.6g", val->data.number);
                val_str = num_buf;
                break;
            default:
                val_str = "(complex value)";
                break;
        }
        puppet_debug("Set $%s = %s", var, val_str);
    }
}

void puppet_exec_class_def(puppet_stmt_t *class_stmt, puppet_env_t *env) {
    const char *class_name = class_stmt->data.class_def.name.data;
    puppet_debug("Defining class: %s", class_name);

    // Register this class definition for later instantiation
    // The class body is NOT executed here - it will be executed when
    // the class is included via 'include', 'require', 'contain', or
    // resource-style instantiation (class { 'name': ... })
    puppet_register_class_def(env, class_stmt);
}

void puppet_exec_program(puppet_program_t *program, puppet_env_t *env) {
    /* Set thread-local log env for error/warning counting */
    puppet_set_log_env(env);

    /* Reset node matching state */
    env->node_matched = false;
    env->default_node = NULL;
    env->node_def_count = 0;  /* Reset node definition registry */

    /*
     * Special mode: when execute_all_nodes AND facts_db is set with multiple nodes,
     * we iterate over nodes in the facts database rather than executing node blocks
     * as we encounter them.
     */
    bool facts_db_iteration_mode = env->execute_all_nodes &&
                                    env->prog->facts_db &&
                                    puppet_facts_db_node_count(env->prog->facts_db) > 0;

    if (facts_db_iteration_mode) {
        /* Enable deferred node execution - collect node definitions */
        env->defer_node_execution = true;
        puppet_debug("Facts DB iteration mode: collecting node definitions");
    }

    /* Remember top-level statements so each per-node compilation can
     * re-evaluate site.pp top-level assignments (e.g. "$jbossenv =
     * $hostname ? {...}") with that node's facts bound, matching
     * puppetresources' fresh-compiler-per-catalog semantics. */
    env->prog->top_level_stmts = &program->statements;

    /* Execute all statements (in defer mode, nodes are registered not executed) */
    puppet_exec_stmt_list(&program->statements, env);

    if (facts_db_iteration_mode) {
        /* Disable defer mode */
        env->defer_node_execution = false;

        /* Now iterate over all nodes in the facts database */
        size_t node_count = puppet_facts_db_node_count(env->prog->facts_db);
        puppet_debug("Executing %zu nodes from facts database", node_count);

        if (env->parallel_nodes) {
            /* Parallel execution mode - one thread per node */
            puppet_exec_nodes_parallel(env, node_count);
        } else {
            /* Sequential execution */
            for (size_t i = 0; i < node_count; i++) {
                const char *certname = puppet_facts_db_get_node_name(env->prog->facts_db, i);
                if (!certname) continue;

                /* Find matching node definition */
                puppet_stmt_t *matching_node = puppet_find_matching_node(env, certname);

                if (matching_node) {
                    /* Execute the matching node block for this certname */
                    puppet_exec_node_for_certname(matching_node, certname, env);
                } else {
                    puppet_warn("No matching node block found for '%s'", certname);
                }
            }
        }
    } else {
        /* Fallback to default node if specific node was requested but not found */
        if (env->node_name && !env->node_matched && env->default_node) {
            puppet_debug("Node '%s' not found, falling back to 'default' node", env->node_name);
            /* Temporarily allow default node execution while preserving certname for facts */
            char *saved_node_name = env->node_name;
            env->node_name = NULL;
            /* Pre-set current_node_certname so facts lookup uses the original certname */
            env->current_node_certname = saved_node_name;
            puppet_exec_node(env->default_node, env);
            env->node_name = saved_node_name;
        }
    }
}

/* Helper function to execute a registered class definition */
static bool puppet_include_class_from_def(puppet_stmt_t *class_def, puppet_env_t *env) {
    if (!class_def || class_def->type != PUPPET_STMT_CLASS_DEF || !env) return false;

    const char *class_name_raw = class_def->data.class_def.name.data;

    /* Normalize class name by stripping leading :: */
    const char *class_name = class_name_raw;
    if (strncmp(class_name, "::", 2) == 0) {
        class_name = class_name_raw + 2;
    }

    puppet_deadcode_mark_class_used(env->prog->deadcode, class_name);

    /* Check if this class is already included - classes are idempotent */
    if (puppet_hash_get(env->class_scopes, class_name, strlen(class_name))) {
        puppet_debug("Class %s already included, skipping", class_name);
        return true;
    }

    if (puppet_verbose) fprintf(stderr, "Including class: %s\n", class_name);

    /* Handle class inheritance - include parent class first */
    puppet_scope_t *parent_class_scope = NULL;
    if (class_def->data.class_def.inherits && class_def->data.class_def.inherits->data) {
        const char *parent_name = class_def->data.class_def.inherits->data;

        /* Strip leading :: from parent name for lookups */
        const char *parent_lookup_name = parent_name;
        if (strncmp(parent_lookup_name, "::", 2) == 0) {
            parent_lookup_name = parent_name + 2;
        }

        puppet_debug("Class %s inherits from %s", class_name, parent_name);

        /* Find and include the parent class */
        puppet_stmt_t *parent_def = puppet_find_class_def(env, parent_lookup_name);
        if (!parent_def && env->prog->loader) {
            parent_def = puppet_loader_load_class(env->prog->loader, parent_lookup_name);
        }

        if (parent_def) {
            /* Check if parent is already included */
            parent_class_scope = (puppet_scope_t *)puppet_hash_get(
                env->class_scopes, parent_lookup_name, strlen(parent_lookup_name));
            if (!parent_class_scope) {
                /* Include the parent class first */
                puppet_include_class_from_def(parent_def, env);
                parent_class_scope = (puppet_scope_t *)puppet_hash_get(
                    env->class_scopes, parent_lookup_name, strlen(parent_lookup_name));
            }
        } else {
            puppet_warn("Parent class '%s' not found for class '%s'", parent_name, class_name);
        }
    }

    /* Create a new scope for the class, parented by the inherited class scope if any.
     * IMPORTANT: If no inherited parent, use node_scope instead of current_scope.
     * current_scope could be a transient define scope that gets destroyed, leaving
     * the class scope with a dangling parent pointer. Node scope is stable. */
    puppet_scope_t *scope_parent = parent_class_scope ? parent_class_scope : env->node_scope;
    puppet_scope_t *class_scope = puppet_scope_create(scope_parent, class_name);
    puppet_scope_push(env, class_scope);

    /* Set class scope in environment for enhanced variable lookup */
    puppet_scope_t *old_class_scope = env->class_scope;
    env->class_scope = class_scope;

    /* Store class scope BEFORE executing body - allows $class::var lookups during execution */
    puppet_hash_set(env->class_scopes, class_name, strlen(class_name), (puppet_value_t *)class_scope);

    /* Puppet semantics: inside a class body, $title and $name are set to the class's
     * full name (e.g. "firewall::linux"). Used by modern modules to build submodule
     * references like "${title}::linux". */
    puppet_value_t *title_val = puppet_value_create_string(class_name, strlen(class_name));
    puppet_scope_set_var(class_scope, "title", title_val);
    puppet_scope_set_var(class_scope, "name", puppet_value_copy(title_val));

    /* Process class parameters - use APL (Automatic Parameter Lookup) for unset params */
    for (size_t i = 0; i < class_def->data.class_def.params.count; i++) {
        puppet_param_t *param = &class_def->data.class_def.params.params[i];
        const char *param_name = param->name.data;
        puppet_value_t *param_value = NULL;

        /* Try Automatic Parameter Lookup (APL) from Hiera first */
        param_value = puppet_apl_lookup(class_name, param_name, env);

        if (!param_value) {
            /* Fall back to default value if APL didn't find anything */
            if (param->default_value) {
                param_value = puppet_eval_expr(param->default_value, env);
            } else {
                param_value = puppet_value_create_undef();
            }
        }

        puppet_scope_set_var(class_scope, param_name, param_value);
    }

    /* Set caller_module_name for hiera lookups (extract module from class name) */
    char *old_caller_module = env->caller_module_name;
    const char *sep = strstr(class_name, "::");
    if (sep) {
        /* Class like "tomee::config" -> module is "tomee" */
        size_t module_len = sep - class_name;
        env->caller_module_name = puppet_malloc(module_len + 1);
        memcpy(env->caller_module_name, class_name, module_len);
        env->caller_module_name[module_len] = '\0';
    } else {
        /* Top-level class like "tomee" -> module is "tomee" */
        env->caller_module_name = puppet_strdup(class_name);
    }

    /* Set $module_name variable in class scope */
    puppet_value_t *module_name_val = puppet_value_create_string(env->caller_module_name, strlen(env->caller_module_name));
    puppet_scope_set_var(class_scope, "module_name", module_name_val);

    /* Execute the class body */
    puppet_exec_stmt_list(&class_def->data.class_def.body, env);

    /* Restore caller_module_name */
    puppet_free(env->caller_module_name);
    env->caller_module_name = old_caller_module;

    /* Add class to catalog if building */
    if (env->build_catalog && env->catalog) {
        puppet_catalog_add_class(env->catalog, class_name);
    }

    /* Restore old class scope */
    env->class_scope = old_class_scope;

    /* Pop the class scope but don't destroy (it's stored in class_scopes) */
    (void)puppet_scope_pop(env);

    return true;
}

void puppet_exec_include(puppet_stmt_t *include_stmt, puppet_env_t *env) {
    if (!include_stmt || include_stmt->type != PUPPET_STMT_INCLUDE) return;

    /* Process each included class */
    for (size_t i = 0; i < include_stmt->data.names.count; i++) {
        puppet_expr_t *name_expr = include_stmt->data.names.exprs[i];
        if (!name_expr) continue;

        /* Evaluate the expression to get the class name (supports string interpolation) */
        puppet_value_t *name_val = puppet_eval_expr(name_expr, env);
        if (!name_val || name_val->type != PUPPET_VALUE_STRING) {
            if (name_val) puppet_value_destroy(name_val);
            continue;
        }

        const char *class_name_raw = name_val->data.string.data;

        /* Normalize class name by stripping leading :: */
        const char *class_name = class_name_raw;
        if (strncmp(class_name, "::", 2) == 0) {
            class_name = class_name_raw + 2;
        }

        /* Item 4: error when this node includes a class from a module whose
         * metadata.json declares puppet incompatible with the Puppet 8 target.
         * Deduped to once per module per node via modules_p8_checked. */
        if (env->prog->loader && env->modules_p8_checked) {
            size_t mlen = strcspn(class_name, ":");  /* module = up to first ':' */
            if (mlen > 0 && mlen < 256) {
                char modname[256];
                memcpy(modname, class_name, mlen);
                modname[mlen] = '\0';
                if (!puppet_hash_get(env->modules_p8_checked, modname, mlen)) {
                    puppet_hash_set(env->modules_p8_checked, modname, mlen,
                                    puppet_value_create_bool(true));
                    const char *req = NULL;
                    if (puppet_loader_module_puppet8_incompatible(env->prog->loader,
                                                                  modname, &req)) {
                        /* puppet_error_at already increments the error count via
                         * the active log env; don't double-count. */
                        puppet_error_at(name_expr->loc,
                            "Module '%s' requires puppet %s, incompatible with the Puppet 8 target",
                            modname, req ? req : "(unknown)");
                    }
                }
            }
        }

        /* First, try to find the class in registered definitions */
        puppet_stmt_t *class_def = puppet_find_class_def(env, class_name);
        if (class_def) {
            puppet_include_class_from_def(class_def, env);
            puppet_value_destroy(name_val);
            continue;
        }

        /* If not found, try to load from module files using the loader.
         * A missing class is a real bug — typically an orphan `include`
         * left behind after the class was removed. Surface it as an
         * error so CI catches the mismatch before it ships. */
        if (env->prog->loader) {
            if (!puppet_loader_include_class(env->prog->loader, class_name, env)) {
                puppet_error_at(name_expr->loc, "Failed to include class '%s'", class_name);
                puppet_env_increment_error(env);
            }
        } else {
            puppet_error_at(name_expr->loc, "Class '%s' not found", class_name);
            puppet_env_increment_error(env);
        }

        puppet_value_destroy(name_val);
    }
}

void puppet_exec_require(puppet_stmt_t *require_stmt, puppet_env_t *env) {
    if (!require_stmt || require_stmt->type != PUPPET_STMT_REQUIRE) return;

    /*
     * 'require' is like 'include' but also creates an ordering dependency:
     * all resources in the current scope will require (depend on) the
     * required class. For now, we just include the class.
     * TODO: Add dependency tracking for proper ordering.
     */

    for (size_t i = 0; i < require_stmt->data.names.count; i++) {
        puppet_expr_t *name_expr = require_stmt->data.names.exprs[i];

        if (name_expr && name_expr->type == PUPPET_EXPR_VALUE &&
            name_expr->data.value->type == PUPPET_VALUE_STRING) {

            const char *class_name = name_expr->data.value->data.string.data;

            /* First, try to find the class in registered definitions */
            puppet_stmt_t *class_def = puppet_find_class_def(env, class_name);
            if (class_def) {
                puppet_include_class_from_def(class_def, env);
                continue;
            }

            /* If not found, try to load from module files using the loader */
            if (env->prog->loader) {
                if (!puppet_loader_include_class(env->prog->loader, class_name, env)) {
                    puppet_warning_at(name_expr->loc, "Failed to require class '%s'", class_name);
                    puppet_env_increment_warning(env);
                }
            } else {
                puppet_error_at(name_expr->loc, "Class '%s' not found", class_name);
                puppet_env_increment_error(env);
            }
        }
    }
}

void puppet_exec_contain(puppet_stmt_t *contain_stmt, puppet_env_t *env) {
    if (!contain_stmt || contain_stmt->type != PUPPET_STMT_CONTAIN) return;

    /*
     * 'contain' is like 'include' but the contained class's dependencies
     * become dependencies of the containing class. This is important for
     * proper ordering when classes are used in dependency chains.
     * For now, we just include the class.
     * TODO: Add containment tracking for proper dependency propagation.
     */

    for (size_t i = 0; i < contain_stmt->data.names.count; i++) {
        puppet_expr_t *name_expr = contain_stmt->data.names.exprs[i];

        if (name_expr && name_expr->type == PUPPET_EXPR_VALUE &&
            name_expr->data.value->type == PUPPET_VALUE_STRING) {

            const char *class_name = name_expr->data.value->data.string.data;

            /* First, try to find the class in registered definitions */
            puppet_stmt_t *class_def = puppet_find_class_def(env, class_name);
            if (class_def) {
                puppet_include_class_from_def(class_def, env);
                continue;
            }

            /* If not found, try to load from module files using the loader */
            if (env->prog->loader) {
                if (!puppet_loader_include_class(env->prog->loader, class_name, env)) {
                    puppet_warning_at(name_expr->loc, "Failed to contain class '%s'", class_name);
                    puppet_env_increment_warning(env);
                }
            } else {
                puppet_error_at(name_expr->loc, "Class '%s' not found", class_name);
                puppet_env_increment_error(env);
            }
        }
    }
}

void puppet_env_set_loader(puppet_env_t *env, struct puppet_loader *loader) {
    if (!env || !env->prog) return;
    env->prog->loader = loader;
}

/**
 * @brief Execute a node definition for a specific certname
 *
 * This is used when iterating over facts_db nodes. The certname is used
 * to set the correct facts before executing the node body.
 *
 * @param node_stmt Node definition to execute
 * @param certname Node certname (for facts lookup)
 * @param env Execution environment
 */
static void puppet_exec_node_for_certname(puppet_stmt_t *node_stmt, const char *certname, puppet_env_t *env) {
    if (!node_stmt || node_stmt->type != PUPPET_STMT_NODE || !certname) return;

    /* Skip if we've already hit an error in fail-fast mode */
    if (env->stop_on_error) {
        return;
    }

    /* Set thread-local log env for error/warning counting */
    puppet_set_log_env(env);

    puppet_debug("Executing node block for certname: %s", certname);
    env->node_matched = true;

    /* Track node processing for CI validation */
    env->nodes_processed++;
    if (env->output_buffer) {
        puppet_env_buffer_printf(env, "--- Node: %s ---\n", certname);
    } else {
        fprintf(stderr, "--- Node: %s ---\n", certname);
    }
    puppet_free(env->current_node_certname);
    env->current_node_certname = puppet_strdup(certname);
    env->current_node_failed = false;

    /* Clear state for this node (each node has its own catalog and class scope) */
    if (env->resource_catalog) {
        for (size_t i = 0; i < env->resource_catalog->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->resource_catalog->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
            env->resource_catalog->buckets[i] = NULL;
        }
    }

    /* Clear class scopes (classes need to be re-included for each node) */
    if (env->class_scopes) {
        for (size_t i = 0; i < env->class_scopes->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->class_scopes->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                /* Note: scope values are managed elsewhere, don't destroy */
                puppet_free(entry);
                entry = next;
            }
            env->class_scopes->buckets[i] = NULL;
        }
    }

    /* Clear the per-node module-metadata-checked set so each node re-reports
     * its own incompatible-module errors. */
    if (env->modules_p8_checked) {
        for (size_t i = 0; i < env->modules_p8_checked->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->modules_p8_checked->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
            env->modules_p8_checked->buckets[i] = NULL;
        }
    }

    /* Clear resource-style class declarations */
    if (env->class_resource_decls) {
        for (size_t i = 0; i < env->class_resource_decls->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->class_resource_decls->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
            env->class_resource_decls->buckets[i] = NULL;
        }
    }

    /* Clear virtual resources */
    if (env->virtual_resources) {
        for (size_t i = 0; i < env->virtual_resources->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->virtual_resources->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
            env->virtual_resources->buckets[i] = NULL;
        }
    }

    /* Clear defined resources tracking */
    if (env->defined_resources) {
        for (size_t i = 0; i < env->defined_resources->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->defined_resources->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
            env->defined_resources->buckets[i] = NULL;
        }
    }

    /* Clear pending realizes */
    if (env->pending_realizes) {
        for (size_t i = 0; i < env->pending_realizes->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->pending_realizes->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
            env->pending_realizes->buckets[i] = NULL;
        }
    }

    /* Switch to node-specific facts (skip in parallel mode - uses env->current_node_certname instead) */
    if (env->prog->facts_db && !env->parallel_nodes) {
        if (puppet_facts_db_set_current_node(env->prog->facts_db, certname) == 0) {
            puppet_debug("Using facts for node: %s", certname);
        } else {
            puppet_warn("No facts found for node %s", certname);
        }
    }

    /* Create a new scope for the node using the certname. Bind it as
     * env->node_scope so that downstream class loads (which parent new
     * class scopes under env->node_scope) can see vars we set here —
     * in particular the re-evaluated top-level assignments below. */
    puppet_scope_t *node_scope = puppet_scope_create(env->current_scope, certname);
    puppet_scope_push(env, node_scope);
    puppet_scope_t *saved_node_scope = env->node_scope;
    env->node_scope = node_scope;

    /* Set $hostname from certname only if no hostname fact exists.
     * Facts take precedence since $hostname fact is the short hostname,
     * while certname is typically the FQDN.
     */
    puppet_value_t *hostname_fact = puppet_facts_get(env, "hostname");
    if (!hostname_fact) {
        puppet_value_t *hostname_value = puppet_value_create_string(certname, strlen(certname));
        puppet_scope_set_var(node_scope, "hostname", hostname_value);
    } else {
        puppet_value_destroy(hostname_fact);
    }

    /* Re-run site.pp top-level $var = ... assignments now that node
     * facts ($hostname, $fqdn, $facts[...]) are bound in scope.
     * puppetresources does this implicitly by starting a fresh
     * compilation per node; we simulate it by replaying assignments.
     * Only assignments are replayed to avoid re-registering class /
     * define / node definitions (which would duplicate state). */
    if (env->prog->top_level_stmts) {
        for (size_t i = 0; i < env->prog->top_level_stmts->count; i++) {
            puppet_stmt_t *s = env->prog->top_level_stmts->stmts[i];
            if (s && s->type == PUPPET_STMT_ASSIGNMENT) {
                puppet_exec_stmt(s, env);
            }
        }
    }

    /* Execute node body */
    puppet_exec_stmt_list(&node_stmt->data.node.body, env);

    /* Restore the outer node_scope pointer before popping. */
    env->node_scope = saved_node_scope;

    /* Pop the node scope */
    puppet_scope_t *old_scope = puppet_scope_pop(env);
    puppet_scope_destroy(old_scope);

    /* Finalize validation: every catalog produced here is fully built.
     * Check that relationship metaparameter refs (require/subscribe/
     * before/notify) resolve and that puppet:///modules/... URLs point
     * to real files. Errors go through puppet_error_at, which will mark
     * current_node_failed via the standard path below. */
    if (env->build_catalog && env->catalog) {
        puppet_catalog_validate_refs(env->catalog);
        if (env->prog->loader && env->prog->loader->modules_path) {
            puppet_catalog_validate_sources(env->catalog, env->prog->loader->modules_path);
        }
    }

    /* Track node failure for CI validation */
    if (env->current_node_failed) {
        env->nodes_failed++;
        if (env->output_buffer) {
            puppet_env_buffer_printf(env, "--- Node: %s FAILED ---\n", certname);
        } else {
            fprintf(stderr, "--- Node: %s FAILED ---\n", certname);
        }
        /* Stop processing on first failure */
        env->stop_on_error = true;
    }
    env->current_node_certname = NULL;
}

void puppet_exec_node(puppet_stmt_t *node_stmt, puppet_env_t *env) {
    if (!node_stmt || node_stmt->type != PUPPET_STMT_NODE) return;

    const char *node_name = node_stmt->data.node.name.data;
    bool is_default = (strcmp(node_name, "default") == 0);

    /* Store default node for potential fallback */
    if (is_default) {
        env->default_node = node_stmt;
    }

    /* If in defer mode (facts_db iteration), just register the node */
    if (env->defer_node_execution) {
        puppet_register_node_def(env, node_stmt);
        return;
    }

    /* Check if we should execute this node */
    bool should_execute = false;

    /* Check if node name is a regex pattern (starts and ends with /) */
    size_t name_len = strlen(node_name);
    bool is_regex = (name_len > 2 && node_name[0] == '/' && node_name[name_len - 1] == '/');

    if (env->execute_all_nodes) {
        /* Execute all literal nodes when --all-nodes is specified */
        /* Skip regex nodes - they require a specific node name to match against */
        if (is_regex) {
            puppet_debug("Skipping regex node: %s (use --node to match)", node_name);
            env->nodes_skipped_regex++;
            should_execute = false;
        } else {
            should_execute = true;
        }
    } else if (!env->node_name) {
        /* No node specified - only execute 'default' node */
        should_execute = is_default;
    } else {
        /* Specific node requested - check for match (not default) */
        if (!is_default) {
            if (is_regex) {
                /* Extract regex pattern (without the slashes) */
                char *pattern = puppet_malloc(name_len - 1);
                strncpy(pattern, node_name + 1, name_len - 2);
                pattern[name_len - 2] = '\0';

                /* Compile and execute regex */
                regex_t regex;
                int ret = puppet_regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB);
                if (ret == 0) {
                    ret = regexec(&regex, env->node_name, 0, NULL, 0);
                    should_execute = (ret == 0);
                    regfree(&regex);
                }
                puppet_free(pattern);
            } else {
                /* Literal string match */
                should_execute = (strcmp(node_name, env->node_name) == 0);
            }
        }
    }

    /* Skip if we've already hit an error in fail-fast mode */
    if (env->stop_on_error) {
        return;
    }

    if (should_execute) {
        puppet_debug("Executing node: %s", node_name);
        env->node_matched = true;

        /* Set thread-local log env for error/warning counting */
        puppet_set_log_env(env);

        /* Track node processing for CI validation */
        env->nodes_processed++;
        /* Use actual node name from -n option when available (for regex matches),
         * otherwise use node_name from the node block.
         * Don't overwrite if already set (e.g., for default node fallback) */
        if (!env->current_node_certname) {
            env->current_node_certname = (char*)(env->node_name ? env->node_name : node_name);
        }
        env->current_node_failed = false;

        /* Print node name for CI tracking */
        if (env->execute_all_nodes) {
            if (env->output_buffer) {
                puppet_env_buffer_printf(env, "--- Node: %s ---\n", node_name);
            } else {
                fprintf(stderr, "--- Node: %s ---\n", node_name);
            }
        }

        /* Clear state for this node when executing multiple nodes */
        if (env->execute_all_nodes) {
            /* Clear resource catalog (each node has its own catalog) */
            if (env->resource_catalog) {
                for (size_t i = 0; i < env->resource_catalog->bucket_count; i++) {
                    puppet_hash_entry_t *entry = env->resource_catalog->buckets[i];
                    while (entry) {
                        puppet_hash_entry_t *next = entry->next;
                        puppet_free(entry->key.data);
                        puppet_value_destroy(entry->value);
                        puppet_free(entry);
                        entry = next;
                    }
                    env->resource_catalog->buckets[i] = NULL;
                }
            }

            /* Clear class scopes (classes need to be re-included for each node) */
            if (env->class_scopes) {
                for (size_t i = 0; i < env->class_scopes->bucket_count; i++) {
                    puppet_hash_entry_t *entry = env->class_scopes->buckets[i];
                    while (entry) {
                        puppet_hash_entry_t *next = entry->next;
                        puppet_free(entry->key.data);
                        /* Note: scope values are managed elsewhere, don't destroy */
                        puppet_free(entry);
                        entry = next;
                    }
                    env->class_scopes->buckets[i] = NULL;
                }
            }

            /* Clear resource-style class declarations */
            if (env->class_resource_decls) {
                for (size_t i = 0; i < env->class_resource_decls->bucket_count; i++) {
                    puppet_hash_entry_t *entry = env->class_resource_decls->buckets[i];
                    while (entry) {
                        puppet_hash_entry_t *next = entry->next;
                        puppet_free(entry->key.data);
                        puppet_value_destroy(entry->value);
                        puppet_free(entry);
                        entry = next;
                    }
                    env->class_resource_decls->buckets[i] = NULL;
                }
            }

            /* Clear virtual resources */
            if (env->virtual_resources) {
                for (size_t i = 0; i < env->virtual_resources->bucket_count; i++) {
                    puppet_hash_entry_t *entry = env->virtual_resources->buckets[i];
                    while (entry) {
                        puppet_hash_entry_t *next = entry->next;
                        puppet_free(entry->key.data);
                        puppet_value_destroy(entry->value);
                        puppet_free(entry);
                        entry = next;
                    }
                    env->virtual_resources->buckets[i] = NULL;
                }
            }

            /* Clear defined resources tracking */
            if (env->defined_resources) {
                for (size_t i = 0; i < env->defined_resources->bucket_count; i++) {
                    puppet_hash_entry_t *entry = env->defined_resources->buckets[i];
                    while (entry) {
                        puppet_hash_entry_t *next = entry->next;
                        puppet_free(entry->key.data);
                        puppet_value_destroy(entry->value);
                        puppet_free(entry);
                        entry = next;
                    }
                    env->defined_resources->buckets[i] = NULL;
                }
            }

            /* Clear pending realizes */
            if (env->pending_realizes) {
                for (size_t i = 0; i < env->pending_realizes->bucket_count; i++) {
                    puppet_hash_entry_t *entry = env->pending_realizes->buckets[i];
                    while (entry) {
                        puppet_hash_entry_t *next = entry->next;
                        puppet_free(entry->key.data);
                        puppet_value_destroy(entry->value);
                        puppet_free(entry);
                        entry = next;
                    }
                    env->pending_realizes->buckets[i] = NULL;
                }
            }
        }

        /* Switch to node-specific facts if available (skip in parallel mode - uses env->current_node_certname) */
        if (env->prog->facts_db && !env->parallel_nodes) {
            /* Use the actual certname (from -n option) rather than node block name (e.g., "default") */
            const char *facts_node = env->current_node_certname ? env->current_node_certname : node_name;
            if (puppet_facts_db_set_current_node(env->prog->facts_db, facts_node) == 0) {
                puppet_debug("Using facts for node: %s", facts_node);
            } else {
                puppet_debug("No facts found for node %s, using default facts", facts_node);
            }
        }

        /* Create a new scope for the node */
        puppet_scope_t *node_scope = puppet_scope_create(env->current_scope, node_name);
        puppet_scope_push(env, node_scope);

        /* Set $hostname from node name only if no hostname fact exists */
        puppet_value_t *hostname_fact = puppet_facts_get(env, "hostname");
        if (!hostname_fact) {
            puppet_value_t *hostname_value = puppet_value_create_string(node_name, strlen(node_name));
            puppet_scope_set_var(node_scope, "hostname", hostname_value);
        } else {
            puppet_value_destroy(hostname_fact);
        }

        /* Execute node body */
        puppet_exec_stmt_list(&node_stmt->data.node.body, env);

        /* Pop the node scope */
        puppet_scope_t *old_scope = puppet_scope_pop(env);
        puppet_scope_destroy(old_scope);

        /* Finalize validation: same hook as puppet_exec_node_for_certname. */
        if (env->build_catalog && env->catalog) {
            puppet_catalog_validate_refs(env->catalog);
            if (env->prog->loader && env->prog->loader->modules_path) {
                puppet_catalog_validate_sources(env->catalog, env->prog->loader->modules_path);
            }
        }

        /* Track node failure for CI validation */
        if (env->current_node_failed) {
            env->nodes_failed++;
            if (env->execute_all_nodes) {
                if (env->output_buffer) {
                    puppet_env_buffer_printf(env, "--- Node: %s FAILED ---\n", node_name);
                } else {
                    fprintf(stderr, "--- Node: %s FAILED ---\n", node_name);
                }
                /* Stop processing on first failure in all-nodes mode */
                env->stop_on_error = true;
            }
        }
        env->current_node_certname = NULL;
    } else {
        /* Skip this node */
        if (!env->execute_all_nodes && env->node_name) {
            /* Only report skipping when a specific node was requested */
            puppet_debug("Skipping node: %s (looking for %s)", node_name, env->node_name);
        }
    }
}

void puppet_env_set_node(puppet_env_t *env, const char *node_name) {
    if (!env) return;
    
    puppet_free(env->node_name);
    env->node_name = node_name ? puppet_strdup(node_name) : NULL;
    env->execute_all_nodes = false;  /* Specific node mode */
}

void puppet_env_set_execute_all_nodes(puppet_env_t *env, bool execute_all) {
    if (!env) return;

    env->execute_all_nodes = execute_all;
    if (execute_all) {
        puppet_free(env->node_name);
        env->node_name = NULL;  /* Clear specific node when in all-nodes mode */
        /* ERB stays enabled in --all-nodes: the native C engine renders
         * the common subset directly (cache shared via puppet_program_state),
         * and the Ruby fallback serialises through ruby_mutex when needed.
         * Parallel mode (-P) keeps ERB enabled too — see
         * puppet_env_set_parallel_nodes. */
    }
}

void puppet_env_set_template_output(puppet_env_t *env, const char *template_target) {
    if (!env) return;

    puppet_free(env->template_output_target);
    env->template_output_target = template_target ? puppet_strdup(template_target) : NULL;
}

void puppet_env_enable_catalog(puppet_env_t *env, const char *certname, const char *environment) {
    if (!env) return;

    env->build_catalog = true;
    env->catalog = puppet_catalog_create(certname, environment);
}

puppet_catalog_t *puppet_env_get_catalog(puppet_env_t *env) {
    if (!env) return NULL;

    puppet_catalog_t *catalog = env->catalog;
    env->catalog = NULL;  /* Transfer ownership to caller */
    return catalog;
}

void puppet_exec_class_instance(puppet_stmt_t *class_instance_stmt, puppet_env_t *env) {
    if (!class_instance_stmt || class_instance_stmt->type != PUPPET_STMT_CLASS_INSTANCE) return;

    const char *class_name = class_instance_stmt->data.class_instance.class_name.data;
    puppet_debug("Instantiating class: %s", class_name);

    // Find the class definition - first check registered definitions
    puppet_stmt_t *class_def = puppet_find_class_def(env, class_name);

    // If not found in registered definitions, try to load from module files
    if (!class_def && env->prog->loader) {
        class_def = puppet_loader_load_class(env->prog->loader, class_name);
    }

    if (!class_def) {
        puppet_error_at(class_instance_stmt->loc, "Class '%s' not found", class_name);
        puppet_env_increment_error(env);
        return;
    }

    // Create a new scope for the class instance
    puppet_scope_t *class_scope = puppet_scope_create(env->current_scope, class_name);
    puppet_scope_push(env, class_scope);
    
    // Set class scope in environment for enhanced variable lookup
    puppet_scope_t *old_class_scope = env->class_scope;
    env->class_scope = class_scope;
    
    // Process class parameters and apply defaults first
    for (size_t i = 0; i < class_def->data.class_def.params.count; i++) {
        puppet_param_t *param = &class_def->data.class_def.params.params[i];
        const char *param_name = param->name.data;

        // Look for this parameter in provided arguments
        puppet_value_t *param_value = NULL;
        bool found_arg = false;

        for (size_t j = 0; j < class_instance_stmt->data.class_instance.arg_count; j++) {
            puppet_attribute_t *arg = &class_instance_stmt->data.class_instance.arguments[j];
            if (!arg->name.data) continue;  /* splat (* => $hash) has no name */
            if (strcmp(arg->name.data, param_name) == 0) {
                param_value = puppet_eval_expr(arg->value, env);
                found_arg = true;
                break;
            }
        }

        // Puppet semantics: an explicitly-supplied undef value is stripped and
        // the parameter default applies. Without this, a typed param passed
        // `undef` (a selector/hiera miss) fails the type-check below even
        // though real Puppet falls back to the default and compiles cleanly.
        if (found_arg && param_value && param_value->type == PUPPET_VALUE_UNDEF &&
            param->default_value) {
            puppet_value_destroy(param_value);
            param_value = puppet_eval_expr(param->default_value, env);
        }

        // If not provided, use default value
        if (!found_arg && param->default_value) {
            param_value = puppet_eval_expr(param->default_value, env);
        } else if (!found_arg) {
            param_value = puppet_value_create_undef();
        }

        // Type-check the parameter value against its declared constraint.
        // Puppet refuses class { 'X': attr => v } when v doesn't satisfy
        // the type — e.g. apt::source 'kubernetes': repos => '' fails because
        // repos is String[1]. Be conservative: only error when we have a
        // type_expr AND we recognise the type name; skip when in doubt.
        if (param->type_expr && param_value &&
            !value_matches_type_str(param_value, param->type_str.data, env)) {
            char typestr[128];
            snprintf(typestr, sizeof(typestr), "%s", param->type_str.data ? param->type_str.data : "?");
            puppet_error_at(class_instance_stmt->loc,
                "Class '%s' parameter $%s: expected %s, got incompatible value",
                class_name, param_name, typestr);
            puppet_env_increment_error(env);
        }

        // Set the parameter value in class scope
        if (param_value) {
            puppet_scope_set_var(class_scope, param_name, param_value);

            // Debug output
            if (puppet_verbose) {
                const char *val_str;
                char num_buf[64];
                const char *source;
                if (!found_arg && !param->default_value) {
                    val_str = "undef";
                    source = " (no default)";
                } else {
                    source = found_arg ? " (provided)" : " (default)";
                    switch (param_value->type) {
                        case PUPPET_VALUE_BOOL:
                            val_str = param_value->data.boolean ? "true" : "false";
                            break;
                        case PUPPET_VALUE_NUMBER:
                            snprintf(num_buf, sizeof(num_buf), "%.6g", param_value->data.number);
                            val_str = num_buf;
                            break;
                        case PUPPET_VALUE_STRING:
                            val_str = param_value->data.string.data;
                            break;
                        default:
                            val_str = "(complex value)";
                            break;
                    }
                }
                puppet_debug("Set class parameter $%s = %s%s", param_name, val_str, source);
            }
        }
    }

    // Verify every provided argument matches a declared class parameter.
    puppet_validate_class_args(class_def,
                               class_instance_stmt->data.class_instance.arguments,
                               class_instance_stmt->data.class_instance.arg_count,
                               class_name, class_instance_stmt->loc, env);

    // Execute the class body
    puppet_debug("Executing class body for: %s", class_name);
    puppet_exec_stmt_list(&class_def->data.class_def.body, env);

    // Add class to catalog if building
    if (env->build_catalog && env->catalog) {
        puppet_catalog_add_class(env->catalog, class_name);
    }

    puppet_debug("Class %s instantiation complete", class_name);
    
    // Restore old class scope
    env->class_scope = old_class_scope;
    
    // Pop the class scope
    puppet_scope_t *old_scope = puppet_scope_pop(env);
    puppet_scope_destroy(old_scope);
}

/*
 * ===========================================================================
 * CLASS DEFINITION MANAGEMENT
 * ===========================================================================
 */

/**
 * @brief Register a class definition for later instantiation
 *
 * @param env Execution environment
 * @param class_def Class definition statement
 * @return 0 on success, -1 on error
 */
int puppet_register_class_def(puppet_env_t *env, puppet_stmt_t *class_def) {
    if (!env || !class_def || class_def->type != PUPPET_STMT_CLASS_DEF) return -1;
    
    // Expand class definition array if needed
    if (env->class_def_count >= env->class_def_capacity) {
        env->class_def_capacity *= 2;
        env->class_definitions = puppet_realloc(env->class_definitions, 
            env->class_def_capacity * sizeof(puppet_stmt_t*));
        if (!env->class_definitions) {
            return -1;
        }
    }
    
    // Add class definition to registry
    env->class_definitions[env->class_def_count] = class_def;
    env->class_def_count++;
    
    return 0;
}

/**
 * @brief Find a class definition by name
 *
 * @param env Execution environment
 * @param class_name Class name to find
 * @return Class definition statement or NULL if not found
 */
puppet_stmt_t *puppet_find_class_def(puppet_env_t *env, const char *class_name) {
    if (!env || !class_name) return NULL;

    /* Normalize class name by stripping leading :: */
    const char *normalized_name = class_name;
    if (strncmp(normalized_name, "::", 2) == 0) {
        normalized_name = class_name + 2;
    }

    for (size_t i = 0; i < env->class_def_count; i++) {
        puppet_stmt_t *class_def = env->class_definitions[i];
        if (class_def && class_def->type == PUPPET_STMT_CLASS_DEF) {
            const char *def_name = class_def->data.class_def.name.data;
            /* Also normalize the definition name for comparison */
            const char *normalized_def = def_name;
            if (strncmp(normalized_def, "::", 2) == 0) {
                normalized_def = def_name + 2;
            }
            if (strcmp(normalized_def, normalized_name) == 0) {
                return class_def;
            }
        }
    }

    return NULL;
}

/*
 * ===========================================================================
 * NODE DEFINITION MANAGEMENT
 * ===========================================================================
 */

/**
 * @brief Register a node definition for later execution
 *
 * @param env Execution environment
 * @param node_def Node definition statement
 * @return 0 on success, -1 on error
 */
static int puppet_register_node_def(puppet_env_t *env, puppet_stmt_t *node_def) {
    if (!env || !node_def || node_def->type != PUPPET_STMT_NODE) return -1;

    // Expand node definition array if needed
    if (env->node_def_count >= env->node_def_capacity) {
        env->node_def_capacity *= 2;
        env->node_definitions = puppet_realloc(env->node_definitions,
            env->node_def_capacity * sizeof(puppet_stmt_t*));
        if (!env->node_definitions) {
            return -1;
        }
    }

    // Add node definition to registry
    env->node_definitions[env->node_def_count] = node_def;
    env->node_def_count++;

    return 0;
}

/**
 * @brief Find a node definition matching a certname
 *
 * Searches through registered node definitions to find one matching the certname.
 * Handles both literal node names and regex patterns.
 *
 * @param env Execution environment
 * @param certname Node certname to match
 * @return Matching node definition or NULL if not found
 */
static puppet_stmt_t *puppet_find_matching_node(puppet_env_t *env, const char *certname) {
    if (!env || !certname) return NULL;

    puppet_stmt_t *default_node = NULL;

    for (size_t i = 0; i < env->node_def_count; i++) {
        puppet_stmt_t *node_def = env->node_definitions[i];
        if (!node_def || node_def->type != PUPPET_STMT_NODE) continue;

        const char *node_name = node_def->data.node.name.data;

        // Check for default node
        if (strcmp(node_name, "default") == 0) {
            default_node = node_def;
            continue;
        }

        size_t name_len = strlen(node_name);

        // Check if node name is a regex pattern (starts and ends with /)
        if (name_len > 2 && node_name[0] == '/' && node_name[name_len - 1] == '/') {
            // Extract regex pattern (without the slashes)
            char *pattern = puppet_malloc(name_len - 1);
            strncpy(pattern, node_name + 1, name_len - 2);
            pattern[name_len - 2] = '\0';

            // Compile and execute regex
            regex_t regex;
            int ret = puppet_regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB);
            if (ret == 0) {
                ret = regexec(&regex, certname, 0, NULL, 0);
                regfree(&regex);
                if (ret == 0) {
                    puppet_free(pattern);
                    return node_def;  // Regex match found
                }
            }
            puppet_free(pattern);
        } else {
            // Literal string match
            if (strcmp(node_name, certname) == 0) {
                return node_def;
            }
        }
    }

    // Return default node if no specific match found
    return default_node;
}

/*
 * ===========================================================================
 * ENHANCED VARIABLE SYSTEM IMPLEMENTATION
 * ===========================================================================
 */

/**
 * @brief Enhanced variable lookup with full chain traversal
 *
 * Implements the complete Puppet variable lookup chain:
 * 1. Local scope (current function/class)
 * 2. Class scope (if inside class)
 * 3. Node scope (node-specific variables)  
 * 4. Global scope (top-level variables)
 * 5. Data providers (Hiera, external data sources)
 *
 * @param env Execution environment
 * @param name Variable name to look up
 * @return Variable value or NULL if not found
 */
puppet_value_t *puppet_variable_lookup_chain(puppet_env_t *env, const char *name) {
    if (!env || !name) return NULL;

    puppet_value_t *value = NULL;

    // Handle :: prefix (top-level/global scope indicator)
    // Variables like $::fqdn, $::hostname explicitly request top-level scope
    const char *lookup_name = name;
    bool top_level_only = false;
    if (strncmp(name, "::", 2) == 0) {
        lookup_name = name + 2;  // Skip the :: prefix
        top_level_only = true;
    }

    // Handle class-qualified variable names like $secrets::root, $apt::params::provider
    // Look for :: in the name (after handling leading ::)
    const char *last_sep = strrchr(lookup_name, ':');
    if (last_sep && last_sep > lookup_name && *(last_sep - 1) == ':') {
        // This is a class-qualified variable like "secrets::root" or "apt::params::provider"
        // Split into class_name and var_name at the last ::
        size_t class_len = (last_sep - 1) - lookup_name;
        char *class_name = puppet_malloc(class_len + 1);
        strncpy(class_name, lookup_name, class_len);
        class_name[class_len] = '\0';
        const char *var_name = last_sep + 1;

        // First check the class_scopes registry (for previously included classes)
        if (env->class_scopes) {
            puppet_scope_t *stored_scope = (puppet_scope_t *)puppet_hash_get(
                env->class_scopes, class_name, strlen(class_name));
            if (stored_scope) {
                // Use recursive=true to search parent scopes (for inherited class variables)
                value = puppet_scope_get_var(stored_scope, var_name, true);
                puppet_free(class_name);
                return value;
            }
        }

        // Look up the class scope - search through the scope stack
        puppet_scope_t *scope = env->current_scope;
        while (scope) {
            if (scope->name.data && strcmp(scope->name.data, class_name) == 0) {
                // Use recursive=true to search parent scopes
                value = puppet_scope_get_var(scope, var_name, true);
                puppet_free(class_name);
                return value;  // Return even if NULL - variable should be in this scope
            }
            scope = scope->parent;
        }

        // Also check if class_scope matches (current class being executed)
        if (env->class_scope && env->class_scope->name.data &&
            strcmp(env->class_scope->name.data, class_name) == 0) {
            // Use recursive=true to search parent scopes
            value = puppet_scope_get_var(env->class_scope, var_name, true);
            puppet_free(class_name);
            return value;
        }

        // Class not found in scopes - try to auto-include it
        // This is Puppet's behavior when referencing $class::var before include
        puppet_stmt_t *class_def = puppet_find_class_def(env, class_name);
        if (!class_def && env->prog->loader) {
            // Try to autoload the class from its manifest file
            class_def = puppet_loader_load_class(env->prog->loader, class_name);
        }
        if (class_def) {
            puppet_include_class_from_def(class_def, env);
            // Now try to get the variable again from the newly included class
            puppet_scope_t *stored_scope = (puppet_scope_t *)puppet_hash_get(
                env->class_scopes, class_name, strlen(class_name));
            if (stored_scope) {
                value = puppet_scope_get_var(stored_scope, var_name, true);
                puppet_free(class_name);
                return value;
            }
        }

        puppet_free(class_name);
        // Class scope not found - fall through to return NULL
        return NULL;
    }

    // If top-level only, skip local and class scopes
    if (!top_level_only) {
        // 1. Local scope (current scope, recursive to walk up parent chain)
        // This is needed for nested defines to see outer define's variables
        value = puppet_scope_get_var(env->current_scope, lookup_name, true);
        if (value) return value;

        // 2. Class scope (if we're inside a class)
        if (env->class_scope && env->class_scope != env->current_scope) {
            value = puppet_scope_get_var(env->class_scope, lookup_name, false);
            if (value) return value;
        }

        // 3. Node scope (node-specific variables)
        if (env->node_scope && env->node_scope != env->current_scope) {
            value = puppet_scope_get_var(env->node_scope, lookup_name, false);
            if (value) return value;
        }
    }

    // 4. Global scope (top-level variables)
    if (env->global_scope != env->current_scope || top_level_only) {
        value = puppet_scope_get_var(env->global_scope, lookup_name, false);
        if (value) return value;
    }

    // 5. Facts lookup
    if (env->prog->facts_db) {
        // Special handling for $facts - return the whole facts hash
        if (strcmp(lookup_name, "facts") == 0) {
            value = puppet_facts_get_all_as_hash(env);
            if (value) return value;
        }
        // Direct fact access (e.g., $hostname, $operatingsystem)
        value = puppet_facts_get(env, lookup_name);
        if (value) return value;
    }
    
    // 6. Data providers (Hiera, external data sources)
    // Skip if we're in hiera path interpolation to prevent infinite recursion
    if (!top_level_only && !env->in_hiera_interpolation) {
        for (size_t i = 0; i < env->prog->data_provider_count; i++) {
            puppet_data_provider_t *provider = env->prog->data_providers[i];
            if (provider && provider->lookup) {
                value = provider->lookup(lookup_name, env, provider->data);
                if (value) return value;
            }
        }
    }

    // 7. Not found
    return NULL;
}

/**
 * @brief Look up variable in specific scope type
 *
 * @param env Execution environment
 * @param name Variable name
 * @param scope Scope type to search
 * @return Variable value or NULL if not found
 */
puppet_value_t *puppet_variable_lookup_scoped(puppet_env_t *env, const char *name, puppet_var_scope_t scope) {
    if (!env || !name) return NULL;
    
    switch (scope) {
        case PUPPET_VAR_LOCAL:
            return puppet_scope_get_var(env->current_scope, name, false);
            
        case PUPPET_VAR_CLASS:
            return env->class_scope ? 
                puppet_scope_get_var(env->class_scope, name, false) : NULL;
                
        case PUPPET_VAR_NODE:
            return env->node_scope ? 
                puppet_scope_get_var(env->node_scope, name, false) : NULL;
                
        case PUPPET_VAR_GLOBAL:
            return puppet_scope_get_var(env->global_scope, name, false);
            
        case PUPPET_VAR_FACT:
            // Facts would be handled by a fact provider
            // For now, fall through to data providers
            for (size_t i = 0; i < env->prog->data_provider_count; i++) {
                puppet_data_provider_t *provider = env->prog->data_providers[i];
                if (provider && provider->lookup) {
                    puppet_value_t *value = provider->lookup(name, env, provider->data);
                    if (value) return value;
                }
            }
            return NULL;
            
        default:
            return NULL;
    }
}

/**
 * @brief Set variable in specific scope
 *
 * @param env Execution environment
 * @param name Variable name
 * @param value Variable value
 * @param scope Target scope type
 */
void puppet_env_set_scoped_var(puppet_env_t *env, const char *name, puppet_value_t *value, puppet_var_scope_t scope) {
    if (!env || !name) return;
    
    switch (scope) {
        case PUPPET_VAR_LOCAL:
            puppet_scope_set_var(env->current_scope, name, value);
            break;
            
        case PUPPET_VAR_CLASS:
            if (env->class_scope) {
                puppet_scope_set_var(env->class_scope, name, value);
            } else {
                // Create class scope if it doesn't exist
                env->class_scope = puppet_scope_create(env->global_scope, "class");
                puppet_scope_set_var(env->class_scope, name, value);
            }
            break;
            
        case PUPPET_VAR_NODE:
            puppet_scope_set_var(env->node_scope, name, value);
            break;
            
        case PUPPET_VAR_GLOBAL:
            puppet_scope_set_var(env->global_scope, name, value);
            break;
            
        case PUPPET_VAR_FACT:
            // Facts are typically read-only, but we could support setting
            // them in node scope for now
            puppet_scope_set_var(env->node_scope, name, value);
            break;
    }
}

/*
 * ===========================================================================
 * DATA PROVIDER MANAGEMENT
 * ===========================================================================
 */

/**
 * @brief Register a data provider (Hiera, etc.)
 *
 * @param env Execution environment
 * @param provider Data provider to register
 * @return 0 on success, -1 on error
 */
int puppet_register_data_provider(puppet_env_t *env, puppet_data_provider_t *provider) {
    if (!env || !provider) return -1;
    
    // Expand provider array if needed
    if (env->prog->data_provider_count >= env->prog->data_provider_capacity) {
        env->prog->data_provider_capacity *= 2;
        env->prog->data_providers = puppet_realloc(env->prog->data_providers, 
            env->prog->data_provider_capacity * sizeof(puppet_data_provider_t*));
        if (!env->prog->data_providers) {
            return -1;
        }
    }
    
    // Add provider to array
    env->prog->data_providers[env->prog->data_provider_count] = provider;
    env->prog->data_provider_count++;
    
    return 0;
}

/**
 * @brief Unregister data provider by name
 *
 * @param env Execution environment
 * @param name Provider name to remove
 */
void puppet_unregister_data_provider(puppet_env_t *env, const char *name) {
    if (!env || !name) return;
    
    for (size_t i = 0; i < env->prog->data_provider_count; i++) {
        puppet_data_provider_t *provider = env->prog->data_providers[i];
        if (provider && provider->name && strcmp(provider->name, name) == 0) {
            // Clean up provider
            if (provider->cleanup) {
                provider->cleanup(provider->data);
            }
            puppet_free(provider->name);
            puppet_free(provider);
            
            // Shift remaining providers down
            for (size_t j = i; j < env->prog->data_provider_count - 1; j++) {
                env->prog->data_providers[j] = env->prog->data_providers[j + 1];
            }
            env->prog->data_provider_count--;
            break;
        }
    }
}

/**
 * @brief Get data provider by name
 *
 * @param env Execution environment
 * @param name Provider name to find
 * @return Provider pointer or NULL if not found
 */
puppet_data_provider_t *puppet_get_data_provider(puppet_env_t *env, const char *name) {
    if (!env || !name) return NULL;
    
    for (size_t i = 0; i < env->prog->data_provider_count; i++) {
        puppet_data_provider_t *provider = env->prog->data_providers[i];
        if (provider && provider->name && strcmp(provider->name, name) == 0) {
            return provider;
        }
    }
    
    return NULL;
}

/*
 * ===========================================================================
 * FACTS DATABASE IMPLEMENTATION
 * ===========================================================================
 */

puppet_facts_db_t *puppet_facts_db_create(void) {
    puppet_facts_db_t *facts_db = puppet_calloc(1, sizeof(puppet_facts_db_t));
    facts_db->node_count = 0;
    facts_db->node_capacity = 4;
    facts_db->nodes = puppet_calloc(facts_db->node_capacity, sizeof(puppet_node_facts_t));
    facts_db->node_index = puppet_calloc(1, sizeof(puppet_hash_t));
    facts_db->node_index->bucket_count = 16;
    facts_db->node_index->buckets = puppet_calloc(facts_db->node_index->bucket_count, sizeof(puppet_hash_entry_t*));
    facts_db->current_node = NULL;
    
    return facts_db;
}

void puppet_facts_db_destroy(puppet_facts_db_t *facts_db) {
    if (!facts_db) return;
    
    // Clean up nodes
    for (size_t i = 0; i < facts_db->node_count; i++) {
        puppet_node_facts_t *node = &facts_db->nodes[i];
        puppet_free(node->certname);
        puppet_free(node->environment);
        
        // Clean up facts hash table
        if (node->facts) {
            for (size_t j = 0; j < node->facts->bucket_count; j++) {
                puppet_hash_entry_t *entry = node->facts->buckets[j];
                while (entry) {
                    puppet_hash_entry_t *next = entry->next;
                    puppet_string_free(entry->key);
                    puppet_value_destroy(entry->value);
                    puppet_free(entry);
                    entry = next;
                }
            }
            puppet_free(node->facts->buckets);
            puppet_free(node->facts);
        }
    }
    puppet_free(facts_db->nodes);
    
    // Clean up node index
    for (size_t i = 0; i < facts_db->node_index->bucket_count; i++) {
        puppet_hash_entry_t *entry = facts_db->node_index->buckets[i];
        while (entry) {
            puppet_hash_entry_t *next = entry->next;
            puppet_string_free(entry->key);
            puppet_value_destroy(entry->value); // Safe to destroy index values
            puppet_free(entry);
            entry = next;
        }
    }
    puppet_free(facts_db->node_index->buckets);
    puppet_free(facts_db->node_index);
    
    puppet_free(facts_db->current_node);
    puppet_free(facts_db);
}

static int puppet_facts_db_add_node(puppet_facts_db_t *facts_db, const char *certname, const char *environment) {
    if (!facts_db || !certname) return -1;
    
    // Expand array if needed
    if (facts_db->node_count >= facts_db->node_capacity) {
        facts_db->node_capacity *= 2;
        facts_db->nodes = puppet_realloc(facts_db->nodes, facts_db->node_capacity * sizeof(puppet_node_facts_t));
    }
    
    // Initialize new node
    puppet_node_facts_t *node = &facts_db->nodes[facts_db->node_count];
    node->certname = puppet_strdup(certname);
    node->environment = environment ? puppet_strdup(environment) : NULL;
    node->facts = puppet_calloc(1, sizeof(puppet_hash_t));
    node->facts->bucket_count = 16;
    node->facts->buckets = puppet_calloc(node->facts->bucket_count, sizeof(puppet_hash_entry_t*));
    
    // Add to index (store array index as number)
    puppet_value_t *index_value = puppet_value_create_number((double)facts_db->node_count);
    puppet_hash_set(facts_db->node_index, certname, strlen(certname), index_value);
    
    facts_db->node_count++;
    return 0;
}

static void puppet_facts_add_fact(puppet_node_facts_t *node, const char *fact_name, json_value_t *json_val) {
    if (!node || !fact_name || !json_val) return;

    puppet_value_t *puppet_val = json_value_to_puppet_value(json_val);
    puppet_hash_set(node->facts, fact_name, strlen(fact_name), puppet_val);
}

/* YAML facts support - add fact directly from puppet_value_t */
static void puppet_facts_add_from_value(puppet_node_facts_t *node, const char *fact_name, puppet_value_t *value) {
    if (!node || !fact_name || !value) return;
    puppet_hash_set(node->facts, fact_name, strlen(fact_name), puppet_value_copy(value));
}

/* Process YAML-loaded puppet_value_t hash recursively */
static void puppet_facts_process_value(puppet_node_facts_t *node, const char *prefix, puppet_value_t *obj) {
    if (!node || !obj || obj->type != PUPPET_VALUE_HASH) return;

    puppet_hash_t *hash = obj->data.hash;
    for (size_t b = 0; b < hash->bucket_count; b++) {
        puppet_hash_entry_t *entry = hash->buckets[b];
        while (entry) {
            const char *key = entry->key.data;
            puppet_value_t *value = entry->value;

            /* Create fully qualified fact name */
            char *fact_name;
            if (prefix && strlen(prefix) > 0) {
                size_t len = strlen(prefix) + strlen(key) + 2;
                fact_name = puppet_malloc(len);
                snprintf(fact_name, len, "%s.%s", prefix, key);
            } else {
                fact_name = puppet_strdup(key);
            }

            if (value->type == PUPPET_VALUE_HASH) {
                /* Recursively process nested hashes */
                puppet_facts_process_value(node, fact_name, value);
            } else {
                /* Add leaf fact */
                puppet_facts_add_from_value(node, fact_name, value);
            }

            /* Also add top-level key for direct access */
            if (!prefix || strlen(prefix) == 0) {
                puppet_facts_add_from_value(node, key, value);
            }

            puppet_free(fact_name);
            entry = entry->next;
        }
    }
}

static void puppet_facts_process_object(puppet_node_facts_t *node, const char *prefix, json_value_t *obj) {
    if (!node || !obj || obj->type != JSON_VALUE_OBJECT) return;
    
    for (size_t i = 0; i < obj->data.object.count; i++) {
        const char *key = obj->data.object.keys[i];
        json_value_t *value = obj->data.object.values[i];
        
        // Create fully qualified fact name
        char *fact_name;
        if (prefix && strlen(prefix) > 0) {
            size_t len = strlen(prefix) + strlen(key) + 2; // +2 for '.' and '\0'
            fact_name = puppet_malloc(len);
            snprintf(fact_name, len, "%s.%s", prefix, key);
        } else {
            fact_name = puppet_strdup(key);
        }
        
        if (value->type == JSON_VALUE_OBJECT) {
            // Recursively process nested objects
            puppet_facts_process_object(node, fact_name, value);
        } else {
            // Add leaf fact
            puppet_facts_add_fact(node, fact_name, value);
        }
        
        // Also add top-level key for direct access (e.g., $os instead of just $os.name)
        if (!prefix || strlen(prefix) == 0) {
            puppet_facts_add_fact(node, key, value);
        }
        
        puppet_free(fact_name);
    }
}

static int puppet_facts_load_facter_format(puppet_facts_db_t *facts_db, json_value_t *root) {
    if (!facts_db || !root || root->type != JSON_VALUE_OBJECT) return -1;
    
    // Facter format: single object with facts
    // Determine node name from hostname or use "localhost"
    json_value_t *hostname_val = json_object_get(root, "hostname");
    json_value_t *networking = json_object_get(root, "networking");
    json_value_t *fqdn_val = networking ? json_object_get(networking, "fqdn") : NULL;
    
    const char *node_name = "localhost";
    if (fqdn_val && fqdn_val->type == JSON_VALUE_STRING) {
        node_name = fqdn_val->data.string_value;
    } else if (hostname_val && hostname_val->type == JSON_VALUE_STRING) {
        node_name = hostname_val->data.string_value;
    }
    
    // Add node
    if (puppet_facts_db_add_node(facts_db, node_name, NULL) < 0) {
        return -1;
    }
    
    puppet_node_facts_t *node = &facts_db->nodes[facts_db->node_count - 1];
    
    // Process all facts
    puppet_facts_process_object(node, NULL, root);
    
    // Set as current node
    puppet_facts_db_set_current_node(facts_db, node_name);
    
    return 0;
}

static int puppet_facts_load_puppetdb_format(puppet_facts_db_t *facts_db, json_value_t *root) {
    if (!facts_db || !root || root->type != JSON_VALUE_ARRAY) return -1;

    // PuppetDB format: array of node objects
    for (size_t i = 0; i < root->data.array.count; i++) {
        json_value_t *node_obj = root->data.array.elements[i];
        if (node_obj->type != JSON_VALUE_OBJECT) continue;

        json_value_t *certname = json_object_get(node_obj, "certname");
        json_value_t *environment = json_object_get(node_obj, "environment");
        json_value_t *facts = json_object_get(node_obj, "facts");

        if (!certname || certname->type != JSON_VALUE_STRING || !facts) continue;

        const char *node_name = certname->data.string_value;
        const char *env_name = (environment && environment->type == JSON_VALUE_STRING) ?
                               environment->data.string_value : NULL;

        // Add node
        if (puppet_facts_db_add_node(facts_db, node_name, env_name) < 0) {
            continue;
        }

        puppet_node_facts_t *node = &facts_db->nodes[facts_db->node_count - 1];

        // Process facts object
        puppet_facts_process_object(node, NULL, facts);
    }

    return 0;
}

/* Load facts from YAML file */
static int puppet_facts_load_yaml(puppet_facts_db_t *facts_db, const char *filepath) {
    if (!facts_db || !filepath) return -1;

    puppet_value_t *root = puppet_hiera_load_yaml(filepath);
    if (!root) {
        puppet_error("Failed to parse YAML facts file: %s", filepath);
        return -1;
    }

    if (root->type != PUPPET_VALUE_HASH) {
        puppet_error("YAML facts file must be a hash: %s", filepath);
        puppet_value_destroy(root);
        return -1;
    }

    /* Check for multi-node format: { facts: { node1: {...}, node2: {...} } } */
    puppet_value_t *facts_root = puppet_hash_get(root->data.hash, "facts", 5);
    if (facts_root && facts_root->type == PUPPET_VALUE_HASH) {
        /* Multi-node format - iterate over all nodes */
        puppet_hash_t *nodes_hash = facts_root->data.hash;
        for (size_t b = 0; b < nodes_hash->bucket_count; b++) {
            puppet_hash_entry_t *entry = nodes_hash->buckets[b];
            while (entry) {
                const char *node_name = entry->key.data;
                puppet_value_t *node_facts = entry->value;

                if (node_facts && node_facts->type == PUPPET_VALUE_HASH) {
                    /* Add this node */
                    if (puppet_facts_db_add_node(facts_db, node_name, NULL) == 0) {
                        puppet_node_facts_t *node = &facts_db->nodes[facts_db->node_count - 1];
                        puppet_facts_process_value(node, NULL, node_facts);
                    }
                }
                entry = entry->next;
            }
        }
        puppet_value_destroy(root);
        return 0;
    }

    /* Single-node format - determine node name from fqdn or hostname */
    const char *node_name = "localhost";
    puppet_value_t *fqdn_val = puppet_hash_get(root->data.hash, "fqdn", 4);
    puppet_value_t *hostname_val = puppet_hash_get(root->data.hash, "hostname", 8);

    if (fqdn_val && fqdn_val->type == PUPPET_VALUE_STRING) {
        node_name = fqdn_val->data.string.data;
    } else if (hostname_val && hostname_val->type == PUPPET_VALUE_STRING) {
        node_name = hostname_val->data.string.data;
    }

    /* Add node */
    if (puppet_facts_db_add_node(facts_db, node_name, NULL) < 0) {
        puppet_value_destroy(root);
        return -1;
    }

    puppet_node_facts_t *node = &facts_db->nodes[facts_db->node_count - 1];

    /* Process all facts */
    puppet_facts_process_value(node, NULL, root);

    /* Set as current node */
    puppet_facts_db_set_current_node(facts_db, node_name);

    puppet_value_destroy(root);
    return 0;
}

/* Check if filepath has YAML extension */
static bool is_yaml_file(const char *filepath) {
    if (!filepath) return false;
    size_t len = strlen(filepath);
    if (len >= 5 && strcmp(filepath + len - 5, ".yaml") == 0) return true;
    if (len >= 4 && strcmp(filepath + len - 4, ".yml") == 0) return true;
    return false;
}

int puppet_facts_db_load_file(puppet_facts_db_t *facts_db, const char *filepath) {
    if (!facts_db || !filepath) return -1;

    /* Check for YAML file first */
    if (is_yaml_file(filepath)) {
        return puppet_facts_load_yaml(facts_db, filepath);
    }

    /* Try JSON parsing */
    json_value_t *root = json_parse_file(filepath);
    if (!root) {
        puppet_error("Failed to parse facts file: %s", filepath);
        return -1;
    }

    int result;
    if (root->type == JSON_VALUE_ARRAY) {
        // PuppetDB format
        result = puppet_facts_load_puppetdb_format(facts_db, root);
    } else if (root->type == JSON_VALUE_OBJECT) {
        // Facter format
        result = puppet_facts_load_facter_format(facts_db, root);
    } else {
        puppet_error("Unsupported facts file format");
        result = -1;
    }
    
    json_value_destroy(root);
    return result;
}

int puppet_facts_db_load_json(puppet_facts_db_t *facts_db, const char *certname,
                               void *facts_json_ptr) {
    json_value_t *facts_json = (json_value_t *)facts_json_ptr;
    if (!facts_db || !certname || !facts_json || facts_json->type != JSON_VALUE_OBJECT) {
        return -1;
    }

    /* Add node with the given certname */
    if (puppet_facts_db_add_node(facts_db, certname, NULL) < 0) {
        return -1;
    }

    puppet_node_facts_t *node = &facts_db->nodes[facts_db->node_count - 1];

    /* Process all facts from the JSON object */
    puppet_facts_process_object(node, NULL, facts_json);

    /* Set as current node */
    puppet_facts_db_set_current_node(facts_db, certname);

    return 0;
}

/**
 * @brief Load facts from the local host using facter
 *
 * Collects system facts from the local machine and adds them to the facts database
 * under the specified certname. This is used as a fallback when no facts file is
 * provided or the node is not found in the facts file.
 *
 * @param facts_db Facts database to add to
 * @param certname Node name to register the facts under
 * @return 0 on success, -1 on failure
 */
int puppet_facts_db_load_from_facter(puppet_facts_db_t *facts_db, const char *certname) {
    if (!facts_db || !certname) return -1;

    /* Create facter context and collect facts */
    facter_ctx_t *facter = facter_create();
    if (!facter) {
        puppet_warn("Failed to create facter context");
        return -1;
    }

    if (facter_collect(facter) != 0) {
        puppet_warn("Failed to collect facts from facter");
        facter_destroy(facter);
        return -1;
    }

    /* Get JSON representation of facts */
    char *json_str = facter_to_json(facter);
    if (!json_str) {
        puppet_warn("Failed to convert facter output to JSON");
        facter_destroy(facter);
        return -1;
    }

    /* Parse JSON */
    json_value_t *json = json_parse(json_str);
    puppet_free(json_str);

    if (!json) {
        puppet_warn("Failed to parse facter JSON output");
        facter_destroy(facter);
        return -1;
    }

    /* Add node to facts database */
    if (puppet_facts_db_add_node(facts_db, certname, NULL) < 0) {
        json_value_destroy(json);
        facter_destroy(facter);
        return -1;
    }

    puppet_node_facts_t *node = &facts_db->nodes[facts_db->node_count - 1];

    /* Process all facts from the JSON object */
    puppet_facts_process_object(node, NULL, json);

    /* Clean up */
    json_value_destroy(json);
    facter_destroy(facter);

    /* Set as current node */
    puppet_facts_db_set_current_node(facts_db, certname);

    return 0;
}

int puppet_facts_db_set_current_node(puppet_facts_db_t *facts_db, const char *certname) {
    if (!facts_db || !certname) return -1;

    // Find node index
    puppet_value_t *index_value = puppet_hash_get(facts_db->node_index, certname, strlen(certname));
    if (!index_value || index_value->type != PUPPET_VALUE_NUMBER) {
        puppet_debug("Node '%s' not found in facts database", certname);
        return -1;
    }

    size_t index = (size_t)index_value->data.number;
    if (index >= facts_db->node_count) {
        puppet_warn("Invalid node index for '%s'", certname);
        return -1;
    }

    // Set current node
    puppet_free(facts_db->current_node);
    facts_db->current_node = puppet_strdup(certname);

    return 0;
}

size_t puppet_facts_db_node_count(puppet_facts_db_t *facts_db) {
    if (!facts_db) return 0;
    return facts_db->node_count;
}

const char *puppet_facts_db_get_node_name(puppet_facts_db_t *facts_db, size_t index) {
    if (!facts_db || index >= facts_db->node_count) return NULL;
    return facts_db->nodes[index].certname;
}

puppet_value_t *puppet_facts_get(puppet_env_t *env, const char *fact_name) {
    if (!fact_name) {
        return NULL;
    }

    /* Hardcoded facts - puppetversion is always "puppetc" */
    if (strcmp(fact_name, "puppetversion") == 0) {
        return puppet_value_create_string("puppetc", 7);
    }

    if (!env || !env->prog->facts_db) {
        return NULL;
    }

    puppet_facts_db_t *facts_db = env->prog->facts_db;

    /* Use env->current_node_certname for thread-safe access, fall back to facts_db->current_node */
    const char *certname = env->current_node_certname ? env->current_node_certname : facts_db->current_node;
    if (!certname) {
        return NULL;
    }

    // Find current node by index
    puppet_value_t *index_value = puppet_hash_get(facts_db->node_index, certname, strlen(certname));
    if (!index_value || index_value->type != PUPPET_VALUE_NUMBER) {
        return NULL;
    }

    size_t index = (size_t)index_value->data.number;
    if (index >= facts_db->node_count) {
        return NULL;
    }

    puppet_node_facts_t *node = &facts_db->nodes[index];

    // Look up fact
    puppet_value_t *fact_value = puppet_hash_get(node->facts, fact_name, strlen(fact_name));
    if (!fact_value) {
        return NULL;
    }
    
    // Return copy to avoid double-free
    return puppet_value_copy(fact_value);
}

/**
 * @brief Get all facts as a nested hash for $facts access
 */
puppet_value_t *puppet_facts_get_all_as_hash(puppet_env_t *env) {
    if (!env || !env->prog->facts_db) {
        return NULL;
    }

    puppet_facts_db_t *facts_db = env->prog->facts_db;

    /* Use env->current_node_certname for thread-safe access, fall back to facts_db->current_node */
    const char *certname = env->current_node_certname ? env->current_node_certname : facts_db->current_node;
    if (!certname) return NULL;

    /* Find current node */
    puppet_value_t *index_value = puppet_hash_get(facts_db->node_index,
        certname, strlen(certname));
    if (!index_value || index_value->type != PUPPET_VALUE_NUMBER) return NULL;

    size_t index = (size_t)index_value->data.number;
    if (index >= facts_db->node_count) return NULL;

    puppet_node_facts_t *node = &facts_db->nodes[index];
    if (!node->facts) return NULL;

    /* Create root hash for $facts */
    puppet_value_t *root = puppet_value_create_hash();

    /* Iterate through all facts and build nested structure */
    for (size_t i = 0; i < node->facts->bucket_count; i++) {
        puppet_hash_entry_t *entry = node->facts->buckets[i];
        while (entry) {
            const char *fact_name = entry->key.data;
            puppet_value_t *fact_value = entry->value;

            /* Split dotted name and create nested hashes */
            puppet_value_t *current = root;
            char *name_copy = puppet_strdup(fact_name);
            char *saveptr;
            char *token = strtok_r(name_copy, ".", &saveptr);
            char *next_token = strtok_r(NULL, ".", &saveptr);

            while (token) {
                if (!next_token) {
                    /* Last token - set the value */
                    puppet_value_t *val_copy = puppet_value_copy(fact_value);
                    puppet_hash_set(current->data.hash, token, strlen(token), val_copy);
                } else {
                    /* Intermediate token - get or create nested hash */
                    puppet_value_t *nested = puppet_hash_get(current->data.hash,
                        token, strlen(token));
                    if (!nested || nested->type != PUPPET_VALUE_HASH) {
                        nested = puppet_value_create_hash();
                        puppet_hash_set(current->data.hash, token, strlen(token), nested);
                    }
                    current = nested;
                }
                token = next_token;
                next_token = strtok_r(NULL, ".", &saveptr);
            }

            puppet_free(name_copy);
            entry = entry->next;
        }
    }

    /* Add hardcoded facts */
    puppet_value_t *puppetversion = puppet_value_create_string("puppetc", 7);
    puppet_hash_set(root->data.hash, "puppetversion", 13, puppetversion);

    return root;
}

/* Item 33 / item 21 shared helper — resolve a dotted fact path
 * ("os.distro.codename", "mountpoints.0") for the current node. Fast path:
 * the facts store keeps dotted names flat, so try a direct lookup first; on
 * miss, walk the nested $facts structure segment by segment (numeric segments
 * index arrays). Returns an owned value, or NULL when any segment is missing. */
puppet_value_t *puppet_facts_lookup_dotted(puppet_env_t *env, const char *dotted) {
    if (!env || !dotted || !*dotted) return NULL;

    puppet_value_t *direct = puppet_facts_get(env, dotted);
    if (direct) return direct;

    puppet_value_t *current = puppet_facts_get_all_as_hash(env);
    if (!current) return NULL;

    char *path = puppet_strdup(dotted);
    char *saveptr = NULL;
    for (char *seg = strtok_r(path, ".", &saveptr); seg; seg = strtok_r(NULL, ".", &saveptr)) {
        puppet_value_t *next = NULL;
        if (current->type == PUPPET_VALUE_HASH) {
            puppet_value_t *v = puppet_hash_get(current->data.hash, seg, strlen(seg));
            if (v) next = puppet_value_copy(v);
        } else if (current->type == PUPPET_VALUE_ARRAY && current->data.array) {
            char *endp = NULL;
            long idx = strtol(seg, &endp, 10);
            if (endp && *endp == '\0' && idx >= 0 &&
                (size_t)idx < current->data.array->count) {
                next = puppet_value_copy(current->data.array->items[idx]);
            }
        }
        puppet_value_destroy(current);
        if (!next) {
            puppet_free(path);
            return NULL;
        }
        current = next;
    }
    puppet_free(path);
    return current;
}

int puppet_env_set_facts_db(puppet_env_t *env, puppet_facts_db_t *facts_db) {
    if (!env) return -1;

    if (env->prog->facts_db) {
        puppet_facts_db_destroy(env->prog->facts_db);
    }

    env->prog->facts_db = facts_db;
    return 0;
}

/*
 * ===========================================================================
 * CI VALIDATION TRACKING
 * ===========================================================================
 */

void puppet_env_get_stats(puppet_env_t *env, size_t *nodes_processed, size_t *nodes_failed,
                          size_t *nodes_skipped_regex, size_t *errors, size_t *warnings) {
    if (!env) {
        if (nodes_processed) *nodes_processed = 0;
        if (nodes_failed) *nodes_failed = 0;
        if (nodes_skipped_regex) *nodes_skipped_regex = 0;
        if (errors) *errors = 0;
        if (warnings) *warnings = 0;
        return;
    }
    if (nodes_processed) *nodes_processed = env->nodes_processed;
    if (nodes_failed) *nodes_failed = env->nodes_failed;
    if (nodes_skipped_regex) *nodes_skipped_regex = env->nodes_skipped_regex;
    if (errors) *errors = env->errors_count;
    if (warnings) *warnings = env->warnings_count;
}

void puppet_env_increment_error(puppet_env_t *env) {
    if (!env) return;
    env->errors_count++;
    env->current_node_failed = true;
}

void puppet_env_increment_warning(puppet_env_t *env) {
    if (!env) return;
    env->warnings_count++;
}

/*
 * ===========================================================================
 * PARALLEL NODE PROCESSING
 * ===========================================================================
 */

void puppet_env_set_parallel_nodes(puppet_env_t *env, bool parallel) {
    if (!env) return;
    env->parallel_nodes = parallel;
    /* ERB stays enabled in parallel mode: the native engine is thread-safe
     * (mutex-protected AST cache), and Ruby fallbacks serialise via
     * ruby_mutex inside puppet_erb_render. Skip-ERB remains an explicit
     * opt-in (env->prog->skip_erb), no longer coupled to -P. */

    /* Allocate stats mutex if enabling parallel mode */
    if (parallel && !env->stats_mutex) {
        env->stats_mutex = puppet_malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(env->stats_mutex, NULL);
    }
}

/**
 * @brief Helper to create a new hash table
 */
static puppet_hash_t *create_hash(size_t bucket_count) {
    puppet_hash_t *h = puppet_calloc(1, sizeof(puppet_hash_t));
    h->bucket_count = bucket_count;
    h->buckets = puppet_calloc(bucket_count, sizeof(puppet_hash_entry_t*));
    return h;
}

puppet_env_t *puppet_env_clone_for_node(puppet_env_t *source, const char *certname) {
    if (!source) return NULL;

    puppet_env_t *env = puppet_calloc(1, sizeof(puppet_env_t));

    /* Share the program-state with the source. The worker does NOT
     * own it; only the creator env destroys it. */
    env->prog = source->prog;
    env->owns_prog = false;

    /* Create new scopes (fresh per node) */
    env->global_scope = puppet_scope_create(NULL, "global");
    env->current_scope = env->global_scope;
    env->stack_capacity = 16;
    env->scope_stack = puppet_calloc(env->stack_capacity, sizeof(puppet_scope_t*));
    env->stack_depth = 0;
    env->node_scope = puppet_scope_create(env->global_scope, "node");
    env->class_scope = NULL;

    /* top_level_stmts lives in env->prog and is already inherited
     * via the shared prog pointer — no need to copy it here. */

    /* Copy global scope variables from source (e.g., top-level $jbossenv = 'preprod')
     * These are needed for Hiera hierarchy path interpolation like %{::jbossenv} */
    if (source->global_scope && source->global_scope->variables) {
        puppet_hash_t *src_vars = source->global_scope->variables;
        for (size_t i = 0; i < src_vars->bucket_count; i++) {
            puppet_hash_entry_t *entry = src_vars->buckets[i];
            while (entry) {
                /* Copy the variable value */
                puppet_value_t *value_copy = puppet_value_copy(entry->value);
                puppet_hash_set(env->global_scope->variables,
                    entry->key.data, entry->key.len, value_copy);
                entry = entry->next;
            }
        }
    }

    /* Share read-only data from source.
     * loader, facts_db, data_providers, deadcode all migrated to
     * env->prog and inherited through the shared prog pointer above. */

    /* Copy class definitions array (contains read-only AST pointers, but array itself must be per-thread) */
    env->class_def_capacity = source->class_def_capacity;
    env->class_def_count = source->class_def_count;
    env->class_definitions = puppet_calloc(env->class_def_capacity, sizeof(puppet_stmt_t*));
    memcpy(env->class_definitions, source->class_definitions, env->class_def_count * sizeof(puppet_stmt_t*));

    /* Copy node definitions array (contains read-only AST pointers, but array itself must be per-thread) */
    env->node_def_capacity = source->node_def_capacity;
    env->node_def_count = source->node_def_count;
    env->node_definitions = puppet_calloc(env->node_def_capacity, sizeof(puppet_stmt_t*));
    memcpy(env->node_definitions, source->node_definitions, env->node_def_count * sizeof(puppet_stmt_t*));
    env->defer_node_execution = false;

    /* Create fresh hashes for per-node state.
     * ruby_types lives in the shared prog so we don't recreate it
     * here — sharing the registry avoids re-scanning the modulepath
     * per worker. */
    env->class_scopes = create_hash(32);
    env->class_resource_decls = create_hash(32);
    env->resource_catalog = create_hash(64);
    env->virtual_resources = create_hash(64);
    env->defined_resources = create_hash(64);
    env->exported_resources = create_hash(64);
    env->classes_being_reexecuted = create_hash(32);
    env->class_reexecuting = false;

    /* Initialize deferred defines and pending realizes for this clone */
    env->deferred_defines = NULL;
    env->deferred_define_count = 0;
    env->deferred_define_capacity = 0;
    env->pending_realizes = create_hash(16);

    /* Copy define_types hash (shallow copy - AST pointers are shared but hash structure is per-thread) */
    env->define_types = puppet_calloc(1, sizeof(puppet_hash_t));
    env->define_types->bucket_count = source->define_types->bucket_count;
    env->define_types->buckets = puppet_calloc(env->define_types->bucket_count, sizeof(puppet_hash_entry_t*));
    for (size_t i = 0; i < source->define_types->bucket_count; i++) {
        puppet_hash_entry_t *src_entry = source->define_types->buckets[i];
        puppet_hash_entry_t **dst_ptr = &env->define_types->buckets[i];
        while (src_entry) {
            puppet_hash_entry_t *new_entry = puppet_calloc(1, sizeof(puppet_hash_entry_t));
            new_entry->key.data = puppet_malloc(src_entry->key.len + 1);
            memcpy(new_entry->key.data, src_entry->key.data, src_entry->key.len);
            new_entry->key.data[src_entry->key.len] = '\0';
            new_entry->key.len = src_entry->key.len;
            new_entry->value = src_entry->value;  /* Share AST pointer */
            new_entry->next = NULL;
            *dst_ptr = new_entry;
            dst_ptr = &new_entry->next;
            src_entry = src_entry->next;
        }
    }

    /* Copy user_functions hash (shallow, like define_types): the worker does
     * NOT re-run top-level statements, so a fresh empty hash would leave
     * user-defined functions unresolved under -P. Each entry stores its own
     * wrapper holding a borrowed AST stmt pointer (shared, never freed here).
     * A per-thread hash is essential — the source hash is read-mostly but
     * registration writes (PUPPET_STMT_FUNCTION_DEF) must not race. */
    env->user_functions = puppet_calloc(1, sizeof(puppet_hash_t));
    if (source->user_functions) {
        env->user_functions->bucket_count = source->user_functions->bucket_count;
        env->user_functions->buckets = puppet_calloc(env->user_functions->bucket_count, sizeof(puppet_hash_entry_t*));
        for (size_t i = 0; i < source->user_functions->bucket_count; i++) {
            for (puppet_hash_entry_t *se = source->user_functions->buckets[i]; se; se = se->next) {
                puppet_value_t *fn_ptr = puppet_calloc(1, sizeof(puppet_value_t));
                fn_ptr->type = PUPPET_VALUE_UNDEF;
                fn_ptr->data.string.data = se->value ? se->value->data.string.data : NULL; /* borrowed AST stmt */
                puppet_hash_set(env->user_functions, se->key.data, se->key.len, fn_ptr);
            }
        }
    } else {
        env->user_functions->bucket_count = 64;
        env->user_functions->buckets = puppet_calloc(env->user_functions->bucket_count, sizeof(puppet_hash_entry_t*));
    }

    /* Copy type_aliases (string values): top-level `type X = …` aliases are
     * registered on the source env before fan-out, and workers may also load
     * module aliases lazily into their own copy. Each entry owns its string. */
    env->type_aliases = puppet_calloc(1, sizeof(puppet_hash_t));
    if (source->type_aliases) {
        env->type_aliases->bucket_count = source->type_aliases->bucket_count;
        env->type_aliases->buckets = puppet_calloc(env->type_aliases->bucket_count, sizeof(puppet_hash_entry_t*));
        for (size_t i = 0; i < source->type_aliases->bucket_count; i++) {
            for (puppet_hash_entry_t *se = source->type_aliases->buckets[i]; se; se = se->next) {
                puppet_hash_set(env->type_aliases, se->key.data, se->key.len,
                                puppet_value_copy(se->value));
            }
        }
    } else {
        env->type_aliases->bucket_count = 64;
        env->type_aliases->buckets = puppet_calloc(env->type_aliases->bucket_count, sizeof(puppet_hash_entry_t*));
    }

    /* Fresh per-node module-metadata-checked "seen" set. It is populated and
     * cleared per node, so each worker must own a private one — a shared hash
     * would corrupt under concurrent puppet_hash_set + per-node clear. */
    env->modules_p8_checked = create_hash(16);

    /* Node-specific settings */
    env->node_name = certname ? puppet_strdup(certname) : NULL;
    env->execute_all_nodes = false;  /* Single node mode */
    env->node_matched = false;
    env->default_node = source->default_node;

    /* verbose lives on prog; inherited via the shared prog pointer. */
    env->template_output_target = NULL;
    env->template_output_found = false;

    /* Fresh catalog if needed (not typically used in --all-nodes mode) */
    env->catalog = NULL;
    env->build_catalog = false;

    /* Fresh CI tracking per node */
    env->nodes_processed = 0;
    env->nodes_failed = 0;
    env->nodes_skipped_regex = 0;
    env->errors_count = 0;
    env->warnings_count = 0;
    env->current_node_certname = certname ? puppet_strdup(certname) : NULL;
    env->current_node_failed = false;
    env->stop_on_error = false;

    /* Fresh compilation state */
    env->compilation_failed = false;
    env->failure_message = NULL;
    env->current_tags = NULL;
    env->in_hiera_interpolation = false;

    /* skip_erb lives on prog; inherited via shared prog pointer. */
    env->parallel_nodes = true;
    env->stats_mutex = source->stats_mutex;

    /* Initialize output buffer for ordered output in parallel mode */
    puppet_env_buffer_init(env);

    return env;
}

/**
 * @brief Destroy a cloned env (doesn't free shared data)
 */
static void puppet_env_destroy_clone(puppet_env_t *env) {
    if (!env) return;

    /* Free scopes */
    puppet_scope_destroy(env->node_scope);
    puppet_scope_destroy(env->global_scope);
    puppet_free(env->scope_stack);

    /* Free node name copies */
    puppet_free(env->node_name);
    puppet_free(env->current_node_certname);
    puppet_free(env->failure_message);

    /* Free per-node hashes (not the shared ones) */
    if (env->class_scopes) {
        for (size_t i = 0; i < env->class_scopes->bucket_count; i++) {
            puppet_hash_entry_t *e = env->class_scopes->buckets[i];
            while (e) {
                puppet_hash_entry_t *next = e->next;
                puppet_free(e->key.data);
                /* Destroy the stored scope (same as puppet_env_destroy) */
                puppet_scope_destroy((puppet_scope_t *)e->value);
                puppet_free(e);
                e = next;
            }
        }
        puppet_free(env->class_scopes->buckets);
        puppet_free(env->class_scopes);
    }

    if (env->class_resource_decls) {
        for (size_t i = 0; i < env->class_resource_decls->bucket_count; i++) {
            puppet_hash_entry_t *e = env->class_resource_decls->buckets[i];
            while (e) {
                puppet_hash_entry_t *next = e->next;
                puppet_free(e->key.data);
                puppet_value_destroy(e->value);
                puppet_free(e);
                e = next;
            }
        }
        puppet_free(env->class_resource_decls->buckets);
        puppet_free(env->class_resource_decls);
    }

    if (env->resource_catalog) {
        for (size_t i = 0; i < env->resource_catalog->bucket_count; i++) {
            puppet_hash_entry_t *e = env->resource_catalog->buckets[i];
            while (e) {
                puppet_hash_entry_t *next = e->next;
                puppet_free(e->key.data);
                puppet_value_destroy(e->value);
                puppet_free(e);
                e = next;
            }
        }
        puppet_free(env->resource_catalog->buckets);
        puppet_free(env->resource_catalog);
    }

    if (env->virtual_resources) {
        for (size_t i = 0; i < env->virtual_resources->bucket_count; i++) {
            puppet_hash_entry_t *e = env->virtual_resources->buckets[i];
            while (e) {
                puppet_hash_entry_t *next = e->next;
                puppet_free(e->key.data);
                /* Value stores a puppet_virtual_resource_t* via type-punning */
                if (e->value && e->value->data.string.data) {
                    puppet_virtual_resource_t *vres = (puppet_virtual_resource_t *)e->value->data.string.data;
                    puppet_free(vres->type);
                    puppet_free(vres->title);
                    for (size_t j = 0; j < vres->attr_count; j++) {
                        puppet_free(vres->attrs[j].name);
                        puppet_value_destroy(vres->attrs[j].value);
                    }
                    puppet_free(vres->attrs);
                    puppet_free(vres);
                }
                puppet_free(e->value);
                puppet_free(e);
                e = next;
            }
        }
        puppet_free(env->virtual_resources->buckets);
        puppet_free(env->virtual_resources);
    }

    if (env->exported_resources) {
        for (size_t i = 0; i < env->exported_resources->bucket_count; i++) {
            puppet_hash_entry_t *e = env->exported_resources->buckets[i];
            while (e) {
                puppet_hash_entry_t *next = e->next;
                puppet_free(e->key.data);
                /* Value stores a puppet_virtual_resource_t* via type-punning */
                if (e->value && e->value->data.string.data) {
                    puppet_virtual_resource_t *vres = (puppet_virtual_resource_t *)e->value->data.string.data;
                    puppet_free(vres->type);
                    puppet_free(vres->title);
                    for (size_t j = 0; j < vres->attr_count; j++) {
                        puppet_free(vres->attrs[j].name);
                        puppet_value_destroy(vres->attrs[j].value);
                    }
                    puppet_free(vres->attrs);
                    puppet_free(vres);
                }
                puppet_free(e->value);
                puppet_free(e);
                e = next;
            }
        }
        puppet_free(env->exported_resources->buckets);
        puppet_free(env->exported_resources);
    }

    if (env->defined_resources) {
        for (size_t i = 0; i < env->defined_resources->bucket_count; i++) {
            puppet_hash_entry_t *e = env->defined_resources->buckets[i];
            while (e) {
                puppet_hash_entry_t *next = e->next;
                puppet_free(e->key.data);
                puppet_value_destroy(e->value);
                puppet_free(e);
                e = next;
            }
        }
        puppet_free(env->defined_resources->buckets);
        puppet_free(env->defined_resources);
    }

    if (env->classes_being_reexecuted) {
        for (size_t i = 0; i < env->classes_being_reexecuted->bucket_count; i++) {
            puppet_hash_entry_t *e = env->classes_being_reexecuted->buckets[i];
            while (e) {
                puppet_hash_entry_t *next = e->next;
                puppet_free(e->key.data);
                puppet_value_destroy(e->value);
                puppet_free(e);
                e = next;
            }
        }
        puppet_free(env->classes_being_reexecuted->buckets);
        puppet_free(env->classes_being_reexecuted);
    }

    if (env->current_tags) {
        puppet_value_destroy(env->current_tags);
    }

    /* Free copied arrays (we own the array, but not the AST pointers inside) */
    puppet_free(env->class_definitions);
    puppet_free(env->node_definitions);

    /* Free copied define_types hash (we own the hash and keys, but not the AST pointers) */
    if (env->define_types) {
        for (size_t i = 0; i < env->define_types->bucket_count; i++) {
            puppet_hash_entry_t *e = env->define_types->buckets[i];
            while (e) {
                puppet_hash_entry_t *next = e->next;
                puppet_free(e->key.data);
                /* Don't destroy e->value - it's a shared AST pointer */
                puppet_free(e);
                e = next;
            }
        }
        puppet_free(env->define_types->buckets);
        puppet_free(env->define_types);
    }

    /* Free deferred defines array and each entry's owned data */
    if (env->deferred_defines) {
        for (size_t i = 0; i < env->deferred_define_count; i++) {
            puppet_free(env->deferred_defines[i].type_name);
            puppet_free(env->deferred_defines[i].title);
            puppet_free(env->deferred_defines[i].resource_id);
            if (env->deferred_defines[i].override_attrs) {
                for (size_t b = 0; b < env->deferred_defines[i].override_attrs->bucket_count; b++) {
                    puppet_hash_entry_t *entry = env->deferred_defines[i].override_attrs->buckets[b];
                    while (entry) {
                        puppet_hash_entry_t *next = entry->next;
                        puppet_free(entry->key.data);
                        puppet_value_destroy(entry->value);
                        puppet_free(entry);
                        entry = next;
                    }
                }
                puppet_free(env->deferred_defines[i].override_attrs->buckets);
                puppet_free(env->deferred_defines[i].override_attrs);
            }
        }
        puppet_free(env->deferred_defines);
    }

    /* Free copied user_functions hash (own the wrappers + keys, borrow the AST) */
    if (env->user_functions) {
        for (size_t i = 0; i < env->user_functions->bucket_count; i++) {
            puppet_hash_entry_t *e = env->user_functions->buckets[i];
            while (e) {
                puppet_hash_entry_t *next = e->next;
                puppet_free(e->key.data);
                puppet_free(e->value);  /* wrapper owned; AST stmt borrowed */
                puppet_free(e);
                e = next;
            }
        }
        puppet_free(env->user_functions->buckets);
        puppet_free(env->user_functions);
    }

    /* Free copied type_aliases hash (own the string values + keys) */
    if (env->type_aliases) {
        for (size_t i = 0; i < env->type_aliases->bucket_count; i++) {
            puppet_hash_entry_t *e = env->type_aliases->buckets[i];
            while (e) {
                puppet_hash_entry_t *next = e->next;
                puppet_free(e->key.data);
                puppet_value_destroy(e->value);
                puppet_free(e);
                e = next;
            }
        }
        puppet_free(env->type_aliases->buckets);
        puppet_free(env->type_aliases);
    }

    /* Free per-node module-metadata-checked set */
    if (env->modules_p8_checked) {
        for (size_t i = 0; i < env->modules_p8_checked->bucket_count; i++) {
            puppet_hash_entry_t *e = env->modules_p8_checked->buckets[i];
            while (e) {
                puppet_hash_entry_t *next = e->next;
                puppet_free(e->key.data);
                puppet_value_destroy(e->value);
                puppet_free(e);
                e = next;
            }
        }
        puppet_free(env->modules_p8_checked->buckets);
        puppet_free(env->modules_p8_checked);
    }

    /* Free pending realizes hash */
    if (env->pending_realizes) {
        for (size_t i = 0; i < env->pending_realizes->bucket_count; i++) {
            puppet_hash_entry_t *e = env->pending_realizes->buckets[i];
            while (e) {
                puppet_hash_entry_t *next = e->next;
                puppet_free(e->key.data);
                puppet_value_destroy(e->value);
                puppet_free(e);
                e = next;
            }
        }
        puppet_free(env->pending_realizes->buckets);
        puppet_free(env->pending_realizes);
    }

    /* Don't free shared data: loader, facts_db, data_providers, stats_mutex */

    puppet_free(env);
}

void puppet_env_merge_stats(puppet_env_t *target, puppet_env_t *source) {
    if (!target || !source) return;

    /* Use mutex if available for thread-safe updates */
    if (target->stats_mutex) {
        pthread_mutex_lock(target->stats_mutex);
    }

    target->nodes_processed += source->nodes_processed;
    target->nodes_failed += source->nodes_failed;
    target->nodes_skipped_regex += source->nodes_skipped_regex;
    target->errors_count += source->errors_count;
    target->warnings_count += source->warnings_count;

    if (target->stats_mutex) {
        pthread_mutex_unlock(target->stats_mutex);
    }
}

/*
 * ===========================================================================
 * BUFFERED OUTPUT FOR PARALLEL MODE
 * ===========================================================================
 */

#define OUTPUT_BUFFER_INITIAL_SIZE 8192

/**
 * @brief Initialize output buffer for parallel mode
 */
void puppet_env_buffer_init(puppet_env_t *env) {
    if (!env) return;
    env->output_buffer_capacity = OUTPUT_BUFFER_INITIAL_SIZE;
    env->output_buffer = puppet_malloc(env->output_buffer_capacity);
    env->output_buffer[0] = '\0';
    env->output_buffer_size = 0;
}

/**
 * @brief Free output buffer
 */
void puppet_env_buffer_free(puppet_env_t *env) {
    if (!env) return;
    puppet_free(env->output_buffer);
    env->output_buffer = NULL;
    env->output_buffer_size = 0;
    env->output_buffer_capacity = 0;
}

/**
 * @brief Printf to output buffer (grows as needed)
 */
void puppet_env_buffer_printf(puppet_env_t *env, const char *format, ...) {
    if (!env || !env->output_buffer) {
        /* Fallback to stderr if no buffer */
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
        return;
    }

    va_list args;
    va_start(args, format);

    /* Try to write to current buffer */
    size_t remaining = env->output_buffer_capacity - env->output_buffer_size;
    int written = vsnprintf(env->output_buffer + env->output_buffer_size, remaining, format, args);
    va_end(args);

    if (written < 0) return;  /* Error */

    if ((size_t)written >= remaining) {
        /* Need more space - grow buffer */
        size_t needed = env->output_buffer_size + written + 1;
        while (env->output_buffer_capacity < needed) {
            env->output_buffer_capacity *= 2;
        }
        env->output_buffer = puppet_realloc(env->output_buffer, env->output_buffer_capacity);

        /* Try again with bigger buffer */
        va_start(args, format);
        remaining = env->output_buffer_capacity - env->output_buffer_size;
        written = vsnprintf(env->output_buffer + env->output_buffer_size, remaining, format, args);
        va_end(args);
    }

    if (written > 0) {
        env->output_buffer_size += written;
    }
}

/**
 * @brief Flush output buffer to stderr
 */
void puppet_env_buffer_flush(puppet_env_t *env) {
    if (!env || !env->output_buffer || env->output_buffer_size == 0) return;
    fwrite(env->output_buffer, 1, env->output_buffer_size, stderr);
    env->output_buffer_size = 0;
    env->output_buffer[0] = '\0';
}

/**
 * @brief Thread worker data for parallel node processing
 */
typedef struct {
    puppet_env_t *source_env;      /**< Original environment with shared data */
    const char *certname;          /**< Node certname to process */
    puppet_stmt_t *node_stmt;      /**< Node statement to execute */
    size_t nodes_processed;        /**< Result: nodes processed */
    size_t nodes_failed;           /**< Result: nodes failed */
    size_t errors_count;           /**< Result: errors */
    size_t warnings_count;         /**< Result: warnings */
    char *output_buffer;           /**< Captured output for ordered printing */
    size_t output_buffer_size;     /**< Size of captured output */
} parallel_node_work_t;

/**
 * @brief Worker function for parallel node processing
 *
 * Uses pthreads for true parallelism of C code. Ruby/ERB calls are
 * serialized via mutex in puppet_erb.c to ensure thread safety.
 */
static void *parallel_node_worker(void *arg) {
    parallel_node_work_t *work = (parallel_node_work_t *)arg;

    /* Create cloned environment for this node */
    puppet_env_t *node_env = puppet_env_clone_for_node(work->source_env, work->certname);
    if (!node_env) {
        work->nodes_failed = 1;
        work->errors_count = 1;
        return NULL;
    }

    /* Note: facts access uses env->current_node_certname (set in clone_for_node)
     * instead of the shared facts_db->current_node for thread safety */

    /* Execute the node (header is printed inside puppet_exec_node_for_certname) */
    puppet_exec_node_for_certname(work->node_stmt, work->certname, node_env);

    /* Capture results */
    work->nodes_processed = node_env->nodes_processed;
    work->nodes_failed = node_env->nodes_failed;
    work->errors_count = node_env->errors_count;
    work->warnings_count = node_env->warnings_count;

    /* Transfer output buffer to work struct (for ordered printing later) */
    work->output_buffer = node_env->output_buffer;
    work->output_buffer_size = node_env->output_buffer_size;
    node_env->output_buffer = NULL;  /* Prevent double-free */
    node_env->output_buffer_size = 0;

    /* Cleanup cloned env (buffer already transferred) */
    puppet_env_destroy_clone(node_env);

    return NULL;
}

/**
 * @brief Execute nodes in parallel using pthreads
 *
 * Uses native pthreads for true parallelism. ERB rendering is enabled:
 * the native C engine handles the common subset under a short cache
 * mutex, and Ruby fallbacks are serialised through ruby_mutex inside
 * puppet_erb_render. Set env->prog->skip_erb explicitly if you want to
 * bypass ERB entirely (CI fast path).
 */
void puppet_exec_nodes_parallel(puppet_env_t *env, size_t node_count) {
    if (!env || node_count == 0) return;

    puppet_debug("Starting parallel execution of %zu nodes using pthreads", node_count);

    /* Allocate thread array and work items */
    pthread_t *threads = puppet_calloc(node_count, sizeof(pthread_t));
    parallel_node_work_t *work_items = puppet_calloc(node_count, sizeof(parallel_node_work_t));

    size_t threads_started = 0;

    /* Create threads for each node */
    for (size_t i = 0; i < node_count; i++) {
        const char *certname = puppet_facts_db_get_node_name(env->prog->facts_db, i);
        if (!certname) continue;

        /* Find matching node definition */
        puppet_stmt_t *matching_node = puppet_find_matching_node(env, certname);
        if (!matching_node) {
            puppet_warn("No matching node block found for '%s'", certname);
            continue;
        }

        /* Set up work item */
        work_items[threads_started].source_env = env;
        work_items[threads_started].certname = certname;
        work_items[threads_started].node_stmt = matching_node;
        work_items[threads_started].nodes_processed = 0;
        work_items[threads_started].nodes_failed = 0;
        work_items[threads_started].errors_count = 0;
        work_items[threads_started].warnings_count = 0;
        work_items[threads_started].output_buffer = NULL;
        work_items[threads_started].output_buffer_size = 0;

        /* Create thread */
        int ret = pthread_create(&threads[threads_started], NULL,
                                parallel_node_worker, &work_items[threads_started]);
        if (ret != 0) {
            puppet_error("Failed to create thread for node %s", certname);
            continue;
        }
        threads_started++;
    }

    /* Wait for all threads */
    for (size_t i = 0; i < threads_started; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Print output buffers in order (same order as facts file) and aggregate stats */
    for (size_t i = 0; i < threads_started; i++) {
        /* Print buffered output for this node */
        if (work_items[i].output_buffer && work_items[i].output_buffer_size > 0) {
            fwrite(work_items[i].output_buffer, 1, work_items[i].output_buffer_size, stderr);
        }
        puppet_free(work_items[i].output_buffer);

        /* Aggregate stats into main env */
        env->nodes_processed += work_items[i].nodes_processed;
        env->nodes_failed += work_items[i].nodes_failed;
        env->errors_count += work_items[i].errors_count;
        env->warnings_count += work_items[i].warnings_count;
    }

    puppet_debug("Parallel execution complete: %zu nodes processed", env->nodes_processed);

    puppet_free(threads);
    puppet_free(work_items);
}