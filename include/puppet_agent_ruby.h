/**
 * @file puppet_agent_ruby.h
 * @brief Ruby VM embedding for puppetc-agent
 *
 * Provides Ruby support for custom types, providers, and facts in the agent.
 *
 * NOTE: This header uses void pointers to avoid conflicts between ruby.h
 * and puppet_provider.h. Cast to proper types in implementation.
 */

#ifndef PUPPET_AGENT_RUBY_H
#define PUPPET_AGENT_RUBY_H

#include <stdbool.h>

/**
 * Ruby context for agent
 */
typedef struct agent_ruby_context {
    int initialized;
    char *libdir;           /* Plugin library directory */
    void *ruby_vm;          /* Opaque pointer to Ruby VM state */
} agent_ruby_context_t;

/**
 * Initialize Ruby VM for agent
 *
 * @param libdir Plugin library directory (e.g., /var/lib/puppetc/lib)
 * @return Initialized Ruby context, or NULL on failure
 */
agent_ruby_context_t *agent_ruby_init(const char *libdir);

/**
 * Clean up Ruby context and release resources
 *
 * @param ctx Ruby context to clean up
 */
void agent_ruby_cleanup(agent_ruby_context_t *ctx);

/**
 * Add a path to Ruby's $LOAD_PATH
 *
 * @param ctx Ruby context
 * @param path Path to add
 * @return 0 on success, -1 on failure
 */
int agent_ruby_add_loadpath(agent_ruby_context_t *ctx, const char *path);

/**
 * Run custom facts from lib/facter/*.rb files
 *
 * @param ctx Ruby context
 * @param facts Facter context to add facts to (facter_ctx_t*)
 * @return Number of facts added, or -1 on failure
 */
int agent_ruby_run_custom_facts(agent_ruby_context_t *ctx, void *facts);

/**
 * Check if a Ruby type is available
 *
 * @param ctx Ruby context
 * @param type_name Resource type name (e.g., "custom_resource")
 * @return true if type exists in lib/puppet/type/
 */
bool agent_ruby_has_type(agent_ruby_context_t *ctx, const char *type_name);

/**
 * Check if a Ruby provider is available
 *
 * @param ctx Ruby context
 * @param type_name Resource type name
 * @param provider_name Provider name (NULL for default)
 * @return true if provider exists
 */
bool agent_ruby_has_provider(agent_ruby_context_t *ctx,
                             const char *type_name,
                             const char *provider_name);

/**
 * Apply result codes (matching puppet_provider.h)
 */
typedef enum {
    RUBY_APPLY_SUCCESS = 0,
    RUBY_APPLY_CHANGED = 1,
    RUBY_APPLY_FAILED = 2,
    RUBY_APPLY_SKIPPED = 3,
    RUBY_APPLY_NOOP = 4
} ruby_apply_result_t;

/**
 * Call a Ruby provider method
 *
 * @param ctx Ruby context
 * @param type_name Resource type name
 * @param provider_name Provider name (NULL for default)
 * @param action Action to perform: "exists", "create", "destroy", "refresh"
 * @param resource Resource to apply (resource_t*)
 * @param apply_ctx Apply context (apply_context_t*)
 * @return Apply result code
 */
ruby_apply_result_t agent_ruby_call_provider(agent_ruby_context_t *ctx,
                                             const char *type_name,
                                             const char *provider_name,
                                             const char *action,
                                             const void *resource,
                                             void *apply_ctx);

/**
 * Call a Ruby function (for Deferred evaluation)
 *
 * @param ctx Ruby context
 * @param func_name Function name
 * @param args_json Arguments as JSON string
 * @return Result as string (caller must free), or NULL on error
 */
char *agent_ruby_call_function(agent_ruby_context_t *ctx,
                               const char *func_name,
                               const char *args_json);

/**
 * Check if Ruby is available and initialized
 *
 * @param ctx Ruby context
 * @return true if Ruby is ready
 */
bool agent_ruby_available(agent_ruby_context_t *ctx);

/**
 * Get Ruby version string
 *
 * @return Ruby version string
 */
const char *agent_ruby_version(void);

#endif /* PUPPET_AGENT_RUBY_H */
