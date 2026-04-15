/**
 * @file puppet_lint.c
 * @brief Puppet 8 compatibility checker
 *
 * Detects deprecated and removed language features for Puppet 8 migration.
 * Two-phase checking:
 * 1. AST walk: deprecated functions, legacy variables, class inheritance
 * 2. File scan: ERB templates, Ruby functions, metadata.json
 */

#include "puppet_lint.h"
#include "puppet_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>

/* ============================================================================
 * Lint message helpers
 * ============================================================================ */

static void lint_error(puppet_lint_result_t *r, puppet_location_t loc,
                       const char *format, ...) __attribute__((format(printf, 3, 4)));

static void lint_error(puppet_lint_result_t *r, puppet_location_t loc,
                       const char *format, ...) {
    r->errors++;
    fprintf(stderr, "\033[1;31merror\033[0m[puppet8]: ");
    if (loc.filename) {
        fprintf(stderr, "%s:%d: ", loc.filename, loc.line);
    }
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

static void lint_warning(puppet_lint_result_t *r, puppet_location_t loc,
                         const char *format, ...) __attribute__((format(printf, 3, 4)));

static void lint_warning(puppet_lint_result_t *r, puppet_location_t loc,
                         const char *format, ...) {
    r->warnings++;
    fprintf(stderr, "\033[1;33mwarning\033[0m[puppet8]: ");
    if (loc.filename) {
        fprintf(stderr, "%s:%d: ", loc.filename, loc.line);
    }
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

/* Helper for file-based warnings (filename + line number) */
static void lint_file_error(puppet_lint_result_t *r, const char *file, int line,
                            const char *format, ...) __attribute__((format(printf, 4, 5)));

static void lint_file_error(puppet_lint_result_t *r, const char *file, int line,
                            const char *format, ...) {
    r->errors++;
    fprintf(stderr, "\033[1;31merror\033[0m[puppet8]: %s:%d: ", file, line);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

static void lint_file_warning(puppet_lint_result_t *r, const char *file, int line,
                              const char *format, ...) __attribute__((format(printf, 4, 5)));

static void lint_file_warning(puppet_lint_result_t *r, const char *file, int line,
                              const char *format, ...) {
    r->warnings++;
    fprintf(stderr, "\033[1;33mwarning\033[0m[puppet8]: %s:%d: ", file, line);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

/* ============================================================================
 * Deprecated/removed function tables
 * ============================================================================ */

typedef struct {
    const char *name;
    const char *replacement;
    bool removed;  /* true = error (removed), false = warning (deprecated) */
} deprecated_func_t;

static const deprecated_func_t deprecated_functions[] = {
    /* Hiera 3 functions - removed in Puppet 8 */
    {"hiera",         "lookup()",                true},
    {"hiera_array",   "lookup() with merge strategy",  true},
    {"hiera_hash",    "lookup() with merge strategy",   true},
    {"hiera_include", "lookup() with include",          true},

    /* Legacy validate_* functions - deprecated, use Puppet type system */
    {"validate_re",            "Pattern[] type or match()",      false},
    {"validate_hash",          "Hash type constraint",           false},
    {"validate_string",        "String type constraint",         false},
    {"validate_array",         "Array type constraint",          false},
    {"validate_bool",          "Boolean type constraint",        false},
    {"validate_integer",       "Integer type constraint",        false},
    {"validate_numeric",       "Numeric type constraint",        false},
    {"validate_slength",       "String[min,max] type constraint",false},
    {"validate_absolute_path", "Stdlib::Absolutepath type",      false},
    {"validate_augeas",        "Puppet type system",             false},
    {"validate_cmd",           "Puppet type system",             false},
    {"validate_ipv4_address",  "Stdlib::IP::Address::V4 type",  false},
    {"validate_ipv6_address",  "Stdlib::IP::Address::V6 type",  false},

    /* Legacy is_* functions - deprecated, use Puppet type system */
    {"is_string",     "$var =~ String",          false},
    {"is_array",      "$var =~ Array",           false},
    {"is_hash",       "$var =~ Hash",            false},
    {"is_bool",       "$var =~ Boolean",         false},
    {"is_integer",    "$var =~ Integer",          false},
    {"is_numeric",    "$var =~ Numeric",          false},
    {"is_float",      "$var =~ Float",            false},
    {"is_function_available", "defined() function", false},

    /* Legacy type conversion - prefer Puppet type system */
    {"str2bool",      "Boolean($var)",           false},
    {"str2saltedsha512", "Puppet type system",    false},
    {"bool2str",      "String($var)",            false},
    {"bool2num",      "Integer($var)",           false},
    {"num2bool",      "Boolean($var)",           false},

    /* Legacy any2* conversion */
    {"any2array",     "Array($var, true)",       false},
    {"any2bool",      "Boolean($var)",           false},

    /* Other deprecated functions */
    {"defined_with_params", "defined() function", false},
    {"type",          "type() built-in (Puppet 4+) or $var.type()", false},

    /* Deprecated patterns */
    {"create_resources", "each loop or resource iteration", false},

    {NULL, NULL, false}  /* sentinel */
};

/* ============================================================================
 * Legacy top-scope facts ($::fact -> $facts[...])
 * ============================================================================ */

typedef struct {
    const char *old_name;     /* without :: prefix */
    const char *replacement;
} legacy_fact_t;

static const legacy_fact_t legacy_facts[] = {
    /* Networking facts */
    {"ipaddress",             "$facts['networking']['ip']"},
    {"ipaddress_eth0",        "$facts['networking']['interfaces']['eth0']['ip']"},
    {"ipaddress_lo",          "$facts['networking']['interfaces']['lo']['ip']"},
    {"fqdn",                  "$facts['networking']['fqdn']"},
    {"hostname",              "$facts['networking']['hostname']"},
    {"domain",                "$facts['networking']['domain']"},
    {"macaddress",            "$facts['networking']['mac']"},

    /* OS facts */
    {"osfamily",              "$facts['os']['family']"},
    {"operatingsystem",       "$facts['os']['name']"},
    {"operatingsystemrelease","$facts['os']['release']['full']"},
    {"operatingsystemmajrelease","$facts['os']['release']['major']"},
    {"lsbdistcodename",       "$facts['os']['distro']['codename']"},
    {"lsbdistid",             "$facts['os']['distro']['id']"},
    {"lsbdistdescription",    "$facts['os']['distro']['description']"},
    {"lsbdistrelease",        "$facts['os']['distro']['release']['full']"},
    {"lsbmajdistrelease",     "$facts['os']['distro']['release']['major']"},

    /* Hardware facts */
    {"processorcount",        "$facts['processors']['count']"},
    {"physicalprocessorcount","$facts['processors']['physicalcount']"},
    {"processor0",            "$facts['processors']['models'][0]"},
    {"memorysize",            "$facts['memory']['system']['total']"},
    {"memoryfree",            "$facts['memory']['system']['available']"},
    {"memorysize_mb",         "$facts['memory']['system']['total_bytes'] / 1048576"},
    {"swapsize",              "$facts['memory']['swap']['total']"},
    {"swapfree",              "$facts['memory']['swap']['available']"},
    {"blockdevice_sda_size",  "$facts['disks']['sda']['size']"},

    /* System facts */
    {"architecture",          "$facts['os']['architecture']"},
    {"hardwaremodel",         "$facts['os']['hardware']"},
    {"kernel",                "$facts['kernel']"},
    {"kernelrelease",         "$facts['kernelrelease']"},
    {"kernelversion",         "$facts['kernelversion']"},
    {"kernelmajversion",      "$facts['kernelmajversion']"},
    {"uptime_seconds",        "$facts['system_uptime']['seconds']"},
    {"uptime_hours",          "$facts['system_uptime']['hours']"},
    {"uptime_days",           "$facts['system_uptime']['days']"},
    {"uptime",                "$facts['system_uptime']['uptime']"},

    /* Identity */
    {"id",                    "$facts['identity']['user']"},
    {"gid",                   "$facts['identity']['group']"},

    /* Puppet facts */
    {"puppetversion",         "$facts['puppetversion']"},
    {"clientcert",            "$facts['clientcert']"},
    {"clientversion",         "$facts['clientversion']"},
    {"environment",           "$facts['environment']"},

    /* Path/location */
    {"selinux",               "$facts['os']['selinux']['enabled']"},
    {"selinux_enforced",      "$facts['os']['selinux']['enforced']"},
    {"timezone",              "$facts['timezone']"},
    {"virtual",               "$facts['virtual']"},
    {"is_virtual",            "$facts['is_virtual']"},

    {NULL, NULL}  /* sentinel */
};

/* ============================================================================
 * AST walker
 * ============================================================================ */

static void lint_expr(puppet_lint_result_t *r, puppet_expr_t *expr);
static void lint_stmt(puppet_lint_result_t *r, puppet_stmt_t *stmt);
static void lint_stmt_list(puppet_lint_result_t *r, puppet_stmt_list_t *list);

static void lint_check_funcall(puppet_lint_result_t *r, const char *name,
                               puppet_location_t loc) {
    for (const deprecated_func_t *f = deprecated_functions; f->name; f++) {
        if (strcmp(name, f->name) == 0) {
            if (f->removed) {
                lint_error(r, loc, "'%s' was removed in Puppet 8, use %s instead",
                          name, f->replacement);
            } else {
                lint_warning(r, loc, "'%s' is deprecated, use %s instead",
                            name, f->replacement);
            }
            return;
        }
    }
}

static void lint_check_variable(puppet_lint_result_t *r, const char *name,
                                puppet_location_t loc) {
    if (!name) return;

    /* Check for top-scope fact variables: $::factname or just ::factname */
    const char *fact_name = NULL;
    if (strncmp(name, "::", 2) == 0) {
        fact_name = name + 2;
    }
    if (!fact_name) return;

    /* Skip $::facts, $::trusted, $::server_facts — those are fine */
    if (strcmp(fact_name, "facts") == 0 ||
        strcmp(fact_name, "trusted") == 0 ||
        strcmp(fact_name, "server_facts") == 0) {
        return;
    }

    /* Check against known legacy facts */
    for (const legacy_fact_t *f = legacy_facts; f->old_name; f++) {
        if (strcmp(fact_name, f->old_name) == 0) {
            lint_error(r, loc, "$::%s is removed in Puppet 8, use %s",
                      fact_name, f->replacement);
            return;
        }
    }

    /* Any other $::var is suspicious — it's a top-scope variable access */
    /* Only warn if it looks like a well-known fact pattern */
    if (strncmp(fact_name, "ipaddress_", 10) == 0 ||
        strncmp(fact_name, "macaddress_", 11) == 0 ||
        strncmp(fact_name, "netmask_", 8) == 0 ||
        strncmp(fact_name, "network_", 8) == 0 ||
        strncmp(fact_name, "blockdevice_", 12) == 0 ||
        strncmp(fact_name, "processor", 9) == 0) {
        lint_error(r, loc,
                  "$::%s uses a legacy fact format removed in Puppet 8, "
                  "use $facts['...'] structured format instead", fact_name);
    }
}

static void lint_expr(puppet_lint_result_t *r, puppet_expr_t *expr) {
    if (!expr) return;

    switch (expr->type) {
        case PUPPET_EXPR_VARIABLE:
            /* Check for legacy top-scope fact variables */
            if (expr->data.variable.data) {
                lint_check_variable(r, expr->data.variable.data, expr->loc);
            }
            break;

        case PUPPET_EXPR_FUNCALL:
            /* Check function name against deprecation list */
            if (expr->data.funcall.name.data) {
                lint_check_funcall(r, expr->data.funcall.name.data, expr->loc);
            }
            /* Walk arguments */
            for (size_t i = 0; i < expr->data.funcall.args.count; i++) {
                lint_expr(r, expr->data.funcall.args.exprs[i]);
            }
            /* Walk lambda body if present */
            if (expr->data.funcall.lambda) {
                if (expr->data.funcall.lambda->body) {
                    lint_stmt_list(r, expr->data.funcall.lambda->body);
                }
                if (expr->data.funcall.lambda->expr_body) {
                    lint_expr(r, expr->data.funcall.lambda->expr_body);
                }
            }
            break;

        case PUPPET_EXPR_BINOP:
            lint_expr(r, expr->data.binop.left);
            lint_expr(r, expr->data.binop.right);
            break;

        case PUPPET_EXPR_UNOP:
            lint_expr(r, expr->data.unop.expr);
            break;

        case PUPPET_EXPR_INDEX:
            lint_expr(r, expr->data.index.object);
            lint_expr(r, expr->data.index.index);
            break;

        case PUPPET_EXPR_DOT:
            lint_expr(r, expr->data.dot.object);
            break;

        case PUPPET_EXPR_CONDITIONAL:
            lint_expr(r, expr->data.conditional.condition);
            lint_expr(r, expr->data.conditional.then_expr);
            lint_expr(r, expr->data.conditional.else_expr);
            break;

        case PUPPET_EXPR_SELECTOR:
            lint_expr(r, expr->data.selector.control);
            for (size_t i = 0; i < expr->data.selector.case_count; i++) {
                lint_expr(r, expr->data.selector.cases[i].match);
                lint_expr(r, expr->data.selector.cases[i].value);
            }
            lint_expr(r, expr->data.selector.default_value);
            break;

        case PUPPET_EXPR_LAMBDA:
            if (expr->data.lambda) {
                if (expr->data.lambda->body) {
                    lint_stmt_list(r, expr->data.lambda->body);
                }
                if (expr->data.lambda->expr_body) {
                    lint_expr(r, expr->data.lambda->expr_body);
                }
            }
            break;

        case PUPPET_EXPR_ARRAY:
            for (size_t i = 0; i < expr->data.array_items.count; i++) {
                lint_expr(r, expr->data.array_items.items[i]);
            }
            break;

        case PUPPET_EXPR_HASH:
            for (size_t i = 0; i < expr->data.hash_entries.count; i++) {
                lint_expr(r, expr->data.hash_entries.keys[i]);
                lint_expr(r, expr->data.hash_entries.values[i]);
            }
            break;

        case PUPPET_EXPR_INTERPOLATED_STRING:
            for (size_t i = 0; i < expr->data.interpolated.count; i++) {
                if (expr->data.interpolated.exprs)
                    lint_expr(r, expr->data.interpolated.exprs[i]);
            }
            break;

        default:
            break;
    }
}

static void lint_resource_attrs(puppet_lint_result_t *r,
                                puppet_attribute_t *attrs, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (attrs[i].value) {
            lint_expr(r, attrs[i].value);
        }
    }
}

static void lint_stmt(puppet_lint_result_t *r, puppet_stmt_t *stmt) {
    if (!stmt) return;

    switch (stmt->type) {
        case PUPPET_STMT_IMPORT:
            lint_error(r, stmt->loc,
                      "'import' was removed in Puppet 4 and is not supported in Puppet 8");
            break;

        case PUPPET_STMT_NODE:
            lint_stmt_list(r, &stmt->data.node.body);
            break;

        case PUPPET_STMT_CLASS_DEF:
            /* Class inheritance */
            if (stmt->data.class_def.inherits &&
                stmt->data.class_def.inherits->data) {
                lint_warning(r, stmt->loc,
                            "class inheritance (inherits '%s') is deprecated, "
                            "use composition with include/contain instead",
                            stmt->data.class_def.inherits->data);
            }
            for (size_t i = 0; i < stmt->data.class_def.params.count; i++) {
                if (stmt->data.class_def.params.params[i].default_value) {
                    lint_expr(r, stmt->data.class_def.params.params[i].default_value);
                }
            }
            lint_stmt_list(r, &stmt->data.class_def.body);
            break;

        case PUPPET_STMT_DEFINE:
            for (size_t i = 0; i < stmt->data.define.params.count; i++) {
                if (stmt->data.define.params.params[i].default_value) {
                    lint_expr(r, stmt->data.define.params.params[i].default_value);
                }
            }
            lint_stmt_list(r, &stmt->data.define.body);
            break;

        case PUPPET_STMT_RESOURCE:
            for (size_t i = 0; i < stmt->data.resource.instance_count; i++) {
                puppet_resource_instance_t *inst = &stmt->data.resource.instances[i];
                if (inst->title) lint_expr(r, inst->title);
                lint_resource_attrs(r, inst->attributes, inst->attr_count);
            }
            break;

        case PUPPET_STMT_RESOURCE_DEFAULT:
            lint_resource_attrs(r, stmt->data.resource_default.attributes,
                               stmt->data.resource_default.attr_count);
            break;

        case PUPPET_STMT_RESOURCE_OVERRIDE:
            if (stmt->data.resource_override.reference) {
                lint_expr(r, stmt->data.resource_override.reference);
            }
            lint_resource_attrs(r, stmt->data.resource_override.attributes,
                               stmt->data.resource_override.attr_count);
            break;

        case PUPPET_STMT_CLASS_INSTANCE:
            lint_resource_attrs(r, stmt->data.class_instance.arguments,
                               stmt->data.class_instance.arg_count);
            break;

        case PUPPET_STMT_IF: {
            puppet_if_branch_t *branch = stmt->data.if_stmt.branches;
            while (branch) {
                lint_expr(r, branch->condition);
                lint_stmt_list(r, &branch->body);
                branch = branch->next;
            }
            if (stmt->data.if_stmt.else_body) {
                lint_stmt_list(r, stmt->data.if_stmt.else_body);
            }
            break;
        }

        case PUPPET_STMT_UNLESS:
            if (stmt->data.unless_stmt.condition) {
                lint_expr(r, stmt->data.unless_stmt.condition);
            }
            lint_stmt_list(r, &stmt->data.unless_stmt.body);
            break;

        case PUPPET_STMT_CASE:
            if (stmt->data.case_stmt.expr) {
                lint_expr(r, stmt->data.case_stmt.expr);
            }
            for (size_t i = 0; i < stmt->data.case_stmt.when_count; i++) {
                lint_expr(r, stmt->data.case_stmt.whens[i].test);
                lint_stmt_list(r, &stmt->data.case_stmt.whens[i].body);
            }
            if (stmt->data.case_stmt.default_body) {
                lint_stmt_list(r, stmt->data.case_stmt.default_body);
            }
            break;

        case PUPPET_STMT_ASSIGNMENT:
        case PUPPET_STMT_APPEND:
            if (stmt->data.assignment.value) {
                lint_expr(r, stmt->data.assignment.value);
            }
            break;

        case PUPPET_STMT_FUNCTION_CALL:
        case PUPPET_STMT_EXPRESSION:
            lint_expr(r, stmt->data.expr);
            break;

        case PUPPET_STMT_RESOURCE_CHAIN:
        case PUPPET_STMT_RESOURCE_COLLECTOR:
        case PUPPET_STMT_INCLUDE:
        case PUPPET_STMT_REQUIRE:
        case PUPPET_STMT_CONTAIN:
        case PUPPET_STMT_TAG:
        case PUPPET_STMT_TYPE_ALIAS:
            break;
    }
}

static void lint_stmt_list(puppet_lint_result_t *r, puppet_stmt_list_t *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        lint_stmt(r, list->stmts[i]);
    }
}

/* ============================================================================
 * Phase 2: File-based scanning (ERB templates, Ruby files, metadata.json)
 * ============================================================================ */

/**
 * @brief Scan a single ERB template file for Puppet 8 issues
 */
static void lint_erb_file(puppet_lint_result_t *r, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[4096];
    int lineno = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;

        /* Check for scope.lookupvar() — removed in Puppet 8 */
        if (strstr(line, "scope.lookupvar")) {
            lint_file_error(r, path, lineno,
                "scope.lookupvar() is removed in Puppet 8, "
                "pass variables as template parameters and use @variable");
        }

        /* Note: scope['var'] is the recommended modern syntax in Puppet 8,
         * replacing the older scope.lookupvar('var'). Do not flag it. */

        /* Check for variables without @ prefix: <%= variable %> (not @variable) */
        char *pos = line;
        while ((pos = strstr(pos, "<%=")) != NULL) {
            pos += 3;
            /* Skip whitespace */
            while (*pos == ' ' || *pos == '\t') pos++;
            /* Check if it's a bare variable (no @, no function call, no expression) */
            if (*pos && *pos != '@' && *pos != '-' && *pos != '%' &&
                *pos != '(' && *pos != '"' && *pos != '\'' &&
                *pos != '[' && *pos != '{' && *pos != '#') {
                /* It's likely a bare variable — but skip Ruby expressions */
                /* Only warn if it's a simple identifier */
                char *end = pos;
                while (*end && *end != ' ' && *end != '%' && *end != '.' &&
                       *end != '(' && *end != '[') end++;
                if (end > pos && (strstr(pos, "%>") != NULL)) {
                    /* Check it's not a method call or keyword */
                    size_t len = end - pos;
                    char varname[256];
                    if (len < sizeof(varname)) {
                        memcpy(varname, pos, len);
                        varname[len] = '\0';
                        /* Skip Ruby keywords and common patterns */
                        if (strcmp(varname, "if") != 0 && strcmp(varname, "end") != 0 &&
                            strcmp(varname, "else") != 0 && strcmp(varname, "elsif") != 0 &&
                            strcmp(varname, "unless") != 0 && strcmp(varname, "nil") != 0 &&
                            strcmp(varname, "true") != 0 && strcmp(varname, "false") != 0 &&
                            strcmp(varname, "scope") != 0 &&
                            varname[0] != '#') {
                            lint_file_warning(r, path, lineno,
                                "ERB variable '%s' should use @ prefix: <%= @%s %%>",
                                varname, varname);
                        }
                    }
                }
            }
        }
    }

    fclose(f);
}

/**
 * @brief Scan a Ruby function file for Puppet 8 issues
 */
static void lint_ruby_file(puppet_lint_result_t *r, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[4096];
    int lineno = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;

        /* Old Puppet 3.x function API — removed in Puppet 8 */
        if (strstr(line, "Puppet::Parser::Functions.newfunction")) {
            lint_file_error(r, path, lineno,
                "Puppet::Parser::Functions.newfunction is removed in Puppet 8, "
                "rewrite using Puppet::Functions.create_function (API 4.x+)");
        }

        /* Ruby 3.x compatibility issues */
        if (strstr(line, "File.exists?")) {
            lint_file_warning(r, path, lineno,
                "File.exists? is removed in Ruby 3.2+ (used by Puppet 8), "
                "use File.exist? instead");
        }

        if (strstr(line, "URI.escape")) {
            lint_file_warning(r, path, lineno,
                "URI.escape is removed in Ruby 3.0+ (used by Puppet 8), "
                "use CGI.escape or ERB::Util.url_encode instead");
        }

        if (strstr(line, "URI.unescape")) {
            lint_file_warning(r, path, lineno,
                "URI.unescape is removed in Ruby 3.0+ (used by Puppet 8), "
                "use CGI.unescape instead");
        }

        if (strstr(line, "PSON")) {
            lint_file_warning(r, path, lineno,
                "PSON is removed in Puppet 8, use JSON instead");
        }

        /* Dir.exists? removed in Ruby 3.2 */
        if (strstr(line, "Dir.exists?")) {
            lint_file_warning(r, path, lineno,
                "Dir.exists? is removed in Ruby 3.2+ (used by Puppet 8), "
                "use Dir.exist? instead");
        }
    }

    fclose(f);
}

/**
 * @brief Scan metadata.json for incompatible version constraints
 */
static void lint_metadata_json(puppet_lint_result_t *r, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[4096];
    int lineno = 0;
    bool in_requirements = false;

    while (fgets(line, sizeof(line), f)) {
        lineno++;

        /* Simple check for puppet version requirement */
        if (strstr(line, "\"name\"") && strstr(line, "\"puppet\"")) {
            in_requirements = true;
        }

        if (in_requirements && strstr(line, "\"version_requirement\"")) {
            /* Check for version constraints that exclude Puppet 8 */
            if (strstr(line, "< 4.0.0") || strstr(line, "< 5.0.0") ||
                strstr(line, "< 6.0.0") || strstr(line, "< 7.0.0") ||
                strstr(line, "<4.0.0") || strstr(line, "<5.0.0") ||
                strstr(line, "<6.0.0") || strstr(line, "<7.0.0") ||
                strstr(line, "3.x") || strstr(line, "4.x")) {
                lint_file_error(r, path, lineno,
                    "Puppet version constraint excludes Puppet 8, update to '>= 7.0.0 < 9.0.0' or similar");
            }
            in_requirements = false;
        }
    }

    fclose(f);
}

/**
 * @brief Recursively scan a directory for ERB/Ruby/metadata files
 */
static void lint_scan_directory(puppet_lint_result_t *r, const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char path[2048];
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            /* Skip hidden dirs and common non-Puppet dirs */
            if (strcmp(entry->d_name, "spec") == 0 ||
                strcmp(entry->d_name, "test") == 0 ||
                strcmp(entry->d_name, "tests") == 0 ||
                strcmp(entry->d_name, ".git") == 0 ||
                strcmp(entry->d_name, ".svn") == 0) {
                continue;
            }
            lint_scan_directory(r, path);
        } else if (S_ISREG(st.st_mode)) {
            size_t namelen = strlen(entry->d_name);

            /* ERB templates */
            if (namelen > 4 && strcmp(entry->d_name + namelen - 4, ".erb") == 0) {
                lint_erb_file(r, path);
            }
            /* Ruby files in lib/ */
            else if (namelen > 3 && strcmp(entry->d_name + namelen - 3, ".rb") == 0) {
                lint_ruby_file(r, path);
            }
            /* metadata.json */
            else if (strcmp(entry->d_name, "metadata.json") == 0) {
                lint_metadata_json(r, path);
            }
        }
    }

    closedir(dir);
}

/* ============================================================================
 * Public API
 * ============================================================================ */

puppet_lint_result_t puppet_lint_puppet8(puppet_program_t *program) {
    puppet_lint_result_t result = {0, 0};

    if (!program) return result;

    /* Phase 1: AST walk */
    lint_stmt_list(&result, &program->statements);

    /* Print summary */
    if (result.errors == 0 && result.warnings == 0) {
        fprintf(stderr, "\033[1;32mPuppet 8 compatibility: OK\033[0m - no issues found\n");
    } else {
        fprintf(stderr, "\n\033[1mPuppet 8 compatibility summary:\033[0m ");
        if (result.errors > 0) {
            fprintf(stderr, "\033[1;31m%d error%s\033[0m",
                    result.errors, result.errors == 1 ? "" : "s");
        }
        if (result.errors > 0 && result.warnings > 0) {
            fprintf(stderr, ", ");
        }
        if (result.warnings > 0) {
            fprintf(stderr, "\033[1;33m%d warning%s\033[0m",
                    result.warnings, result.warnings == 1 ? "" : "s");
        }
        fputc('\n', stderr);
    }

    return result;
}

puppet_lint_result_t puppet_lint_puppet8_directory(const char *dir_path) {
    puppet_lint_result_t result = {0, 0};

    if (!dir_path) return result;

    /* Scan for ERB templates, Ruby files, and metadata.json */
    lint_scan_directory(&result, dir_path);

    return result;
}
