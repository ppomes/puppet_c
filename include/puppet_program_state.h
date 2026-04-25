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

    /* Pointer back to the parsed top-level statement list, so each
     * per-node compilation can replay top-level $var = $hostname?{...}
     * style assignments with the node's facts bound. Not owned. */
    puppet_stmt_list_t *top_level_stmts;

    /* Mutex protecting writes to the registries below. Reads on
     * read-mostly data may go un-locked once parse is complete; any
     * mutation (autoload of a previously-unseen class, for example)
     * must hold this lock. */
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
