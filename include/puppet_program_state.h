/**
 * @file puppet_program_state.h
 * @brief Shared, parse-once state used by every per-node compilation
 *
 * Captures everything that must be visible to multiple node
 * compilations but is logically a property of "the program being
 * compiled" rather than "this particular node". Patterned after
 * puppetresources' fresh-compiler-per-catalog model: each node gets
 * its own puppet_env_t (transient), but they all share the same
 * puppet_program_state_t (read-mostly).
 *
 * Migration approach: fields are moved here one at a time from
 * puppet_env_t. Code that previously read env->X will read
 * env->prog->X. Mutators that touch shared state must take
 * prog->reg_mutex if they can race across worker threads.
 */

#ifndef PUPPET_PROGRAM_STATE_H
#define PUPPET_PROGRAM_STATE_H

#include "puppet_ast.h"
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

/* Forward-declare types defined in other headers to avoid circulars. */
struct puppet_loader;
struct puppet_data_provider;
struct puppet_facts_db;
struct puppet_deadcode;
struct puppet_hash;

typedef struct puppet_program_state {
    /* Module loader. Owns its own internal mutex for the autoload
     * cache, so it can be shared as-is across workers. Read via
     * env->prog->loader. */
    struct puppet_loader *loader;

    /* Facts database. Read-only once loaded from yaml/json/puppetdb;
     * shared across all per-node compilations. The active node is
     * tracked separately in env->current_node_certname (per-node)
     * because puppet_facts_db_set_current_node mutates a shared
     * field and isn't thread-safe in parallel mode. */
    struct puppet_facts_db *facts_db;

    /* External data providers (Hiera and friends). Set up once at
     * configure-time; read by every per-node hiera() call. Provider
     * implementations must be thread-safe. */
    struct puppet_data_provider **data_providers;
    size_t data_provider_count;
    size_t data_provider_capacity;

    /* Dead-code tracker. NULL unless --dead-code mode is active.
     * Owns its own mutex for cross-thread mark_*_used calls. */
    struct puppet_deadcode *deadcode;

    /* Pointer to the parsed program's top-level statement list, so
     * each per-node compilation can replay top-level $var =
     * $hostname?{...} style assignments with that node's facts
     * bound. Not owned by program-state; the parser owns the AST. */
    puppet_stmt_list_t *top_level_stmts;

    /* Ruby type registry (lazy-initialised) — populated by scanning
     * each module's lib/puppet/type for Puppet::Type.newtype(:name).
     * Lookups happen during the unknown-type check; lazy init must
     * take reg_mutex to avoid races between worker threads. */
    struct puppet_hash *ruby_types;
    bool ruby_types_initialized;

    /* Pure config flags, set once before any per-node compile and
     * read everywhere. NOT per-worker overridable — these are the
     * ones that genuinely belong to "the run", not "this node":
     *  - skip_erb: explicit opt-in for "render no ERB at all" (CI fast
     *              path). No longer coupled to --parallel — that mode
     *              now uses the thread-safe native ERB engine with a
     *              ruby_mutex-serialised fallback for unsupported
     *              templates.
     *  - verbose:  -v / --verbose CLI flag */
    bool skip_erb;
    bool verbose;
    bool strict_erb;   /* --strict-erb: promote ERB @class::var sugar warnings to errors */

    /* Item 30 — opt-in per-tree resource policy, loaded lazily from
     * <base_path>/.puppetc-policy.json under reg_mutex. When the file is
     * absent the whole feature is a no-op. Entries flag deprecated resource
     * declarations (e.g. apt::source['openvox7'] on a branch that targets
     * OpenVox 8). policy_warned dedups to one diagnostic per type[title]. */
    struct puppet_policy_entry {
        char *type;          /* resource type, matched case-insensitively */
        char *title;         /* exact title to match, or NULL */
        char *title_pattern; /* POSIX ERE for the title, or NULL */
        char *reason;        /* appended to the diagnostic, may be NULL */
        bool  level_error;   /* true: error; false: warning (default) */
    } *policy_entries;
    size_t policy_entry_count;
    bool policy_loaded;
    char **policy_warned;
    size_t policy_warned_count, policy_warned_cap;

    /* Mutex protecting concurrent writes against parallel reads.
     * Currently only used by puppet_init_ruby_types' lazy population.
     *
     * Phase 1F attempted to migrate class_definitions, node_definitions
     * and define_types here too. The difficulty: those three are
     * written from autoload paths *during* compilation, and a coarse-
     * grain mutex around every puppet_hash_get/set proved fragile in
     * parallel mode (the hash carriers point into AST that other
     * workers may also be inspecting). Proper fix is per-compile
     * autoload caches (Phase 2: a puppet_compile_t separate from
     * puppet_env_t) rather than a shared registry; until then those
     * three stay in puppet_env_t and are copied on clone_for_node. */
    pthread_mutex_t reg_mutex;

    /* True if reg_mutex was successfully initialised. */
    bool reg_mutex_initialised;
} puppet_program_state_t;

/**
 * @brief Allocate and initialise a new program-state.
 * @return New state with mutex initialised, or NULL on failure.
 */
puppet_program_state_t *puppet_program_state_create(void);

/**
 * @brief Destroy a program-state and release its mutex.
 *
 * Pointed-to data (loader, top_level_stmts) is NOT freed here; their
 * lifetimes are managed by the caller.
 */
void puppet_program_state_destroy(puppet_program_state_t *prog);

#endif /* PUPPET_PROGRAM_STATE_H */
