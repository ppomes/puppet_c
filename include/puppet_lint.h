/**
 * @file puppet_lint.h
 * @brief Puppet 8 compatibility checker
 *
 * Walks the AST to detect language constructs and function calls
 * that are deprecated or removed in Puppet 8.
 */

#ifndef PUPPET_LINT_H
#define PUPPET_LINT_H

#include "puppet_ast.h"
#include <stdbool.h>

/**
 * @brief Lint result statistics
 */
typedef struct puppet_lint_result {
    int errors;       /**< Removed features (will break in Puppet 8) */
    int warnings;     /**< Deprecated features (should migrate) */
} puppet_lint_result_t;

/**
 * @brief Run Puppet 8 compatibility checks on a parsed program
 *
 * Walks all statements and nested expressions to detect:
 * - Removed language constructs (import, node inheritance)
 * - Deprecated/removed functions (hiera, validate_*)
 * - Deprecated patterns (class inheritance, resource defaults)
 *
 * @param program The parsed program to check
 * @return Lint result with error/warning counts
 */
puppet_lint_result_t puppet_lint_puppet8(puppet_program_t *program);

/**
 * @brief Treat legacy top-scope fact reads ($hostname / $::osfamily) as errors
 *        instead of warnings (item 13). Off by default; set from
 *        --puppet8-strict-facts. Call before puppet_lint_puppet8().
 */
void puppet_lint_set_strict_facts(bool strict);

/**
 * @brief Look up a legacy fact name (without :: prefix) in the removed-facts
 *        table. Returns the structured $facts['...'] replacement string, or
 *        NULL if the name is not a known legacy fact. Shared with the ERB
 *        scans (items 24-26), which flag the same facts in templates.
 */
const char *puppet_lint_lookup_legacy_fact(const char *name);

/**
 * @brief Facts-only lint walk (items 13/27/28): flags legacy top-scope fact
 *        reads (bare, ::-prefixed, in param defaults and interpolations) and
 *        nothing else. Run once per lazily-loaded module manifest, which the
 *        entry program's full puppet_lint_puppet8() never sees.
 */
puppet_lint_result_t puppet_lint_legacy_facts(puppet_program_t *program);

/**
 * @brief Scan a directory tree for Puppet 8 issues in non-Puppet files
 *
 * Scans for:
 * - ERB templates: scope.lookupvar(), scope[], variables without @
 * - Ruby files: old function API (Puppet::Parser::Functions), Ruby 3.x issues
 * - metadata.json: version constraints incompatible with Puppet 8
 *
 * @param dir_path Root directory to scan (typically modules/ or the project root)
 * @return Lint result with error/warning counts
 */
puppet_lint_result_t puppet_lint_puppet8_directory(const char *dir_path);

#endif /* PUPPET_LINT_H */
