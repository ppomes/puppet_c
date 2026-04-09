/**
 * @file puppet_lint.c
 * @brief Puppet 8 compatibility checker
 *
 * Detects deprecated and removed language features for Puppet 8 migration.
 * Operates on the parsed AST without requiring evaluation.
 */

#include "puppet_lint.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

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

    {NULL, NULL, false}  /* sentinel */
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

static void lint_expr(puppet_lint_result_t *r, puppet_expr_t *expr) {
    if (!expr) return;

    switch (expr->type) {
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
            /* Node inheritance was removed - but our AST doesn't store it.
             * The grammar parses it; if it were stored we'd check here.
             * For now, node definitions themselves are fine. */
            lint_stmt_list(r, &stmt->data.node.body);
            break;

        case PUPPET_STMT_CLASS_DEF:
            /* Class inheritance - warn (still works but discouraged) */
            if (stmt->data.class_def.inherits &&
                stmt->data.class_def.inherits->data) {
                lint_warning(r, stmt->loc,
                            "class inheritance (inherits '%s') is deprecated, "
                            "use composition with include/contain instead",
                            stmt->data.class_def.inherits->data);
            }
            /* Walk parameter defaults */
            for (size_t i = 0; i < stmt->data.class_def.params.count; i++) {
                if (stmt->data.class_def.params.params[i].default_value) {
                    lint_expr(r, stmt->data.class_def.params.params[i].default_value);
                }
            }
            /* Walk class body */
            lint_stmt_list(r, &stmt->data.class_def.body);
            break;

        case PUPPET_STMT_DEFINE:
            /* Walk parameter defaults */
            for (size_t i = 0; i < stmt->data.define.params.count; i++) {
                if (stmt->data.define.params.params[i].default_value) {
                    lint_expr(r, stmt->data.define.params.params[i].default_value);
                }
            }
            lint_stmt_list(r, &stmt->data.define.body);
            break;

        case PUPPET_STMT_RESOURCE:
            /* Walk resource attribute expressions */
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
            /* Function call stored as expression — walk it via lint_expr */
            lint_expr(r, stmt->data.expr);
            break;

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
            /* These are fine in Puppet 8, walk their expressions */
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
 * Public API
 * ============================================================================ */

puppet_lint_result_t puppet_lint_puppet8(puppet_program_t *program) {
    puppet_lint_result_t result = {0, 0};

    if (!program) return result;

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
