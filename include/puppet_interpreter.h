/**
 * @file puppet_interpreter.h
 * @brief Puppet language interpreter and evaluation engine
 *
 * This header defines the evaluation engine for executing Puppet AST nodes.
 * The interpreter provides:
 * - Variable scoping and environment management
 * - Expression evaluation (arithmetic, logic, function calls)
 * - Statement execution (resources, classes, conditionals)
 * - Built-in function integration
 *
 * Architecture:
 * - Environment manages variable scoping with stack-based scopes
 * - Evaluation functions recursively process expression trees
 * - Execution functions handle statements and side effects
 * - Function registry provides built-in and user-defined functions
 */

#ifndef PUPPET_INTERPRETER_H
#define PUPPET_INTERPRETER_H

#include "puppet_ast.h"
#include "puppet_catalog.h"
#include <pthread.h>

/* Forward declarations */
typedef struct puppet_env puppet_env_t;
typedef struct puppetdb puppetdb_t;
struct puppet_loader;  /* forward decl, see puppet_loader.h */

/*
 * ===========================================================================
 * SCOPING AND ENVIRONMENT SYSTEM
 * ===========================================================================
 */

/**
 * @brief Variable scope types for enhanced lookup
 */
typedef enum {
    PUPPET_VAR_LOCAL,     /**< Local scope variable ($var) */
    PUPPET_VAR_CLASS,     /**< Class scope variable ($::class::var) */
    PUPPET_VAR_GLOBAL,    /**< Global scope variable ($::var) */
    PUPPET_VAR_NODE,      /**< Node scope variable */
    PUPPET_VAR_FACT       /**< System fact ($facts['name']) */
} puppet_var_scope_t;

/**
 * @brief Data provider interface for external data sources (Hiera, etc.)
 */
typedef struct puppet_data_provider {
    char *name;                    /**< Provider name (e.g., "hiera", "consul") */
    
    /** Core lookup function */
    puppet_value_t *(*lookup)(
        const char *key,           /**< Variable name to look up */
        puppet_env_t *env,         /**< Current environment/scope */
        void *provider_data        /**< Provider-specific data */
    );
    
    /** Check if key exists (optional optimization) */
    bool (*has_key)(const char *key, puppet_env_t *env, void *data);
    
    /** Initialize provider (load config, connect, etc.) */
    int (*init)(void **provider_data, const char *config);
    
    /** Cleanup provider */
    void (*cleanup)(void *provider_data);
    
    void *data;                   /**< Provider-specific data */
} puppet_data_provider_t;

/**
 * @brief Facts storage for a single node
 */
typedef struct puppet_node_facts {
    char *certname;               /**< Node certificate name */
    char *environment;            /**< Node environment (optional) */
    puppet_hash_t *facts;         /**< Fact name → value mapping */
} puppet_node_facts_t;

/**
 * @brief Facts database for multiple nodes
 */
typedef struct puppet_facts_db {
    puppet_node_facts_t *nodes;   /**< Array of node facts */
    size_t node_count;           /**< Number of nodes */
    size_t node_capacity;        /**< Node array capacity */
    puppet_hash_t *node_index;   /**< certname → node_facts mapping for fast lookup */
    char *current_node;          /**< Currently selected node (if any) */
} puppet_facts_db_t;

/**
 * @brief Variable scope for lexical scoping
 * 
 * Implements lexical scoping for Puppet variables. Each scope contains
 * a hash table of variable bindings and links to parent scope for
 * variable lookup. Scopes are organized in a tree structure that
 * mirrors the syntactic nesting of Puppet code.
 */
typedef struct puppet_scope {
    struct puppet_scope *parent;    /**< Parent scope for variable lookup */
    puppet_hash_t *variables;       /**< Variable name → value mapping */
    puppet_string_t name;          /**< Scope identifier for debugging */
} puppet_scope_t;

/**
 * @brief Pre-evaluated attribute for virtual resources
 */
typedef struct puppet_virtual_attr {
    char *name;                     /**< Attribute name */
    puppet_value_t *value;          /**< Pre-evaluated value */
} puppet_virtual_attr_t;

/**
 * @brief Pre-evaluated virtual resource
 *
 * Stores all evaluated attribute values at declaration time so they
 * can be realized later even if the original scope is gone.
 */
typedef struct puppet_virtual_resource {
    char *type;                     /**< Resource type (e.g., "file") */
    char *title;                    /**< Resource title */
    puppet_virtual_attr_t *attrs;   /**< Pre-evaluated attributes */
    size_t attr_count;              /**< Number of attributes */
    bool realized;                  /**< Whether already realized */
} puppet_virtual_resource_t;

/**
 * @brief Deferred define type instance awaiting execution
 *
 * When a defined type resource is declared, execution is deferred until
 * the end of the current statement list, so that resource overrides
 * can be applied before the define body runs.
 */
typedef struct puppet_deferred_define {
    puppet_stmt_t *define_stmt;
    puppet_stmt_t *resource_stmt;
    puppet_resource_instance_t *instance;
    char *type_name;
    char *title;
    char *resource_id;
    puppet_hash_t *override_attrs;  /* attr_name -> puppet_value_t* */
    bool class_reexecuting;         /* preserve class_reexecuting state at deferral time */
} puppet_deferred_define_t;

/**
 * @brief Execution environment with scope management
 *
 * The environment maintains the runtime state for Puppet evaluation,
 * including the current variable scope, scope stack for nested contexts,
 * and global scope for top-level variables. Provides the context needed
 * for expression evaluation and statement execution.
 */
/* Forward-declare the shared program-state container (see
 * puppet_program_state.h). Each puppet_env_t holds a pointer to
 * one; the "creator" env owns it and frees it in env_destroy,
 * worker envs (created by puppet_env_clone_for_node) just borrow
 * the pointer. */
struct puppet_program_state;

typedef struct puppet_env {
    /* Shared, parse-once state (loader, top-level stmt pointer,
     * autoload-cache mutex, etc.). Borrowed by worker envs. */
    struct puppet_program_state *prog;
    /* True iff this env owns *prog and must destroy it. */
    bool owns_prog;

    puppet_scope_t *global_scope;   /**< Top-level variables */
    puppet_scope_t *current_scope;  /**< Currently active scope */
    puppet_scope_t **scope_stack;   /**< Stack of nested scopes */
    size_t stack_depth;            /**< Current stack depth */
    size_t stack_capacity;         /**< Maximum stack capacity */
    /* loader migrated to env->prog->loader */
    char *node_name;               /**< Current node name (for filtering) */
    bool execute_all_nodes;        /**< Execute all nodes regardless of name */
    bool node_matched;             /**< Whether a matching node was found/executed */
    puppet_stmt_t *default_node;   /**< Pointer to 'default' node for fallback */
    
    /* Enhanced variable system */
    puppet_data_provider_t **data_providers;  /**< External data providers (Hiera, etc.) */
    size_t data_provider_count;               /**< Number of registered providers */
    size_t data_provider_capacity;            /**< Provider array capacity */
    puppet_scope_t *node_scope;               /**< Node-specific variables */
    puppet_scope_t *class_scope;              /**< Current class scope */
    
    /* Class definition registry */
    puppet_stmt_t **class_definitions;        /**< Array of class definition statements */
    size_t class_def_count;                   /**< Number of registered class definitions */
    size_t class_def_capacity;                /**< Class definition array capacity */

    /* Defined type registry */
    puppet_hash_t *define_types;              /**< define_name → stmt mapping */

    /* Node definition registry (for facts_db iteration mode) */
    puppet_stmt_t **node_definitions;         /**< Array of node definition statements */
    size_t node_def_count;                    /**< Number of registered node definitions */
    size_t node_def_capacity;                 /**< Node definition array capacity */
    bool defer_node_execution;                /**< Defer node execution for facts_db iteration */

    /* Class scope registry - stores scopes for included classes for $class::var lookups */
    puppet_hash_t *class_scopes;              /**< class_name → scope mapping */

    /* Resource-style class declarations - tracks class { } syntax (non-idempotent) */
    puppet_hash_t *class_resource_decls;      /**< class_name → true for class {} declarations */
    
    /* Facts database */
    puppet_facts_db_t *facts_db;              /**< Facts database for node-specific facts */
    
    /* Resource catalog for duplicate detection */
    puppet_hash_t *resource_catalog;          /**< Track declared resources (type::title → true) */

    /* Class re-execution tracking */
    puppet_hash_t *classes_being_reexecuted;  /**< Classes currently being re-executed (to prevent loops) */
    bool class_reexecuting;                   /**< True during class body re-execution (allows resource overwrites) */

    /* Virtual resources storage */
    puppet_hash_t *virtual_resources;         /**< Virtual resources awaiting realization (type::title → stmt) */

    /* Top-level statement list — shared with the parsed program so
     * per-node compilation can re-run site.pp top-level assignments
     * with that node's facts bound (matches puppetresources' "fresh
     * compiler per catalog" model). Not owned. */
    puppet_stmt_list_t *top_level_stmts;

    /* Ruby type registry (lazy-initialised).
     * Populated by scanning each module's lib/puppet/type directory for
     * Puppet::Type.newtype(:name) declarations. Keys are lowercased.
     * Used by the "Unknown resource type" check to avoid false positives
     * for types registered by Ruby modules outside our define_types hash. */
    puppet_hash_t *ruby_types;
    bool ruby_types_initialized;
    
    /* Template output mode */
    char *template_output_target;             /**< Resource title to output template for (NULL = disabled) */
    bool template_output_found;               /**< Whether we found the template target */
    
    /* Core function support */
    puppet_hash_t *defined_resources;         /**< Track defined resources for defined() function */
    puppet_value_t *current_tags;             /**< Current tags for tag()/tagged() functions */
    bool compilation_failed;                  /**< Flag for fail() function */
    char *failure_message;                    /**< Error message from fail() function */

    /* Output control */
    bool verbose;                             /**< Enable verbose/debug output */

    /* Hiera recursion guard */
    bool in_hiera_interpolation;              /**< Prevent recursive hiera lookups in path interpolation */

    /* Catalog building */
    puppet_catalog_t *catalog;                /**< Resource catalog (NULL if not building) */
    bool build_catalog;                       /**< Whether to build a catalog */

    /* CI validation tracking */
    size_t nodes_processed;                   /**< Number of nodes processed */
    size_t nodes_failed;                      /**< Number of nodes with errors */
    size_t nodes_skipped_regex;               /**< Number of regex nodes skipped */
    size_t errors_count;                      /**< Total errors encountered */
    size_t warnings_count;                    /**< Total warnings encountered */
    char *current_node_certname;              /**< Current node being processed (for error tracking) */
    bool current_node_failed;                 /**< Whether current node has errors */
    bool stop_on_error;                       /**< Stop processing on first error (fail-fast) */

    /* Parallel node processing */
    bool parallel_nodes;                      /**< Process nodes in parallel */
    bool skip_erb;                            /**< Skip ERB template rendering (for parallel mode) */

    /* Output buffering for parallel mode (ordered output) */
    char *output_buffer;                      /**< Buffer for capturing output */
    size_t output_buffer_size;                /**< Current content size in buffer */
    size_t output_buffer_capacity;            /**< Allocated buffer capacity */

    /* Module context for hiera lookups */
    char *caller_module_name;                 /**< Current module name for hiera $module_name */
    pthread_mutex_t *stats_mutex;             /**< Mutex for stats updates (shared between threads) */

    /* Exported resources (PuppetDB integration) */
    puppetdb_t *puppetdb;                     /**< PuppetDB for storing/querying exported resources */
    puppet_hash_t *exported_resources;        /**< Local cache of exported resources awaiting storage */

    /* Deferred define type execution */
    puppet_deferred_define_t *deferred_defines;   /**< Array of deferred define instances */
    size_t deferred_define_count;                 /**< Number of deferred defines */
    size_t deferred_define_capacity;              /**< Capacity of deferred defines array */

    /* Pending realizes for deferred virtual resources */
    puppet_hash_t *pending_realizes;              /**< resource_key -> true for deferred realize() calls */

    /* Dead-code tracker (optional) */
    struct puppet_deadcode *deadcode;             /**< NULL unless --dead-code mode is active */
} puppet_env_t;

/* Global verbose flag for use before environment is created */
extern bool puppet_verbose;

/*
 * ===========================================================================
 * PUBLIC API FUNCTIONS
 * ===========================================================================
 */

/* Environment management */
puppet_env_t *puppet_env_create(void);
void puppet_env_destroy(puppet_env_t *env);
void puppet_env_set_verbose(puppet_env_t *env, bool verbose);

/* Debug output macros - only output when verbose mode is enabled */
#define puppet_debug(fmt, ...) do { \
    if (puppet_verbose) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__); \
} while(0)

#define puppet_warn(fmt, ...) do { \
    fprintf(stderr, "[WARN] " fmt "\n", ##__VA_ARGS__); \
} while(0)

#define puppet_error(fmt, ...) do { \
    fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__); \
} while(0)

/* Scope management */
puppet_scope_t *puppet_scope_create(puppet_scope_t *parent, const char *name);
void puppet_scope_destroy(puppet_scope_t *scope);
void puppet_scope_push(puppet_env_t *env, puppet_scope_t *scope);
puppet_scope_t *puppet_scope_pop(puppet_env_t *env);

/* Variable operations */
void puppet_env_set_var(puppet_env_t *env, const char *name, puppet_value_t *value);
puppet_value_t *puppet_env_get_var(puppet_env_t *env, const char *name);
void puppet_scope_set_var(puppet_scope_t *scope, const char *name, puppet_value_t *value);
puppet_value_t *puppet_scope_get_var(puppet_scope_t *scope, const char *name, bool recursive);

/* Enhanced variable operations */
puppet_value_t *puppet_variable_lookup_chain(puppet_env_t *env, const char *name);
puppet_value_t *puppet_variable_lookup_scoped(puppet_env_t *env, const char *name, puppet_var_scope_t scope);
void puppet_env_set_scoped_var(puppet_env_t *env, const char *name, puppet_value_t *value, puppet_var_scope_t scope);

/* Data provider management */
int puppet_register_data_provider(puppet_env_t *env, puppet_data_provider_t *provider);
void puppet_unregister_data_provider(puppet_env_t *env, const char *name);
puppet_data_provider_t *puppet_get_data_provider(puppet_env_t *env, const char *name);

/* Class definition management */
int puppet_register_class_def(puppet_env_t *env, puppet_stmt_t *class_def);
puppet_stmt_t *puppet_find_class_def(puppet_env_t *env, const char *class_name);

/* Facts database management */
puppet_facts_db_t *puppet_facts_db_create(void);
void puppet_facts_db_destroy(puppet_facts_db_t *facts_db);
int puppet_facts_db_load_file(puppet_facts_db_t *facts_db, const char *filepath);
int puppet_facts_db_load_json(puppet_facts_db_t *facts_db, const char *certname,
                               void *facts_json);  /* json_value_t* */
int puppet_facts_db_load_from_facter(puppet_facts_db_t *facts_db, const char *certname);
int puppet_facts_db_set_current_node(puppet_facts_db_t *facts_db, const char *certname);
puppet_value_t *puppet_facts_get(puppet_env_t *env, const char *fact_name);
puppet_value_t *puppet_facts_get_all_as_hash(puppet_env_t *env);
int puppet_env_set_facts_db(puppet_env_t *env, puppet_facts_db_t *facts_db);
size_t puppet_facts_db_node_count(puppet_facts_db_t *facts_db);
const char *puppet_facts_db_get_node_name(puppet_facts_db_t *facts_db, size_t index);

/* Expression evaluation */
puppet_value_t *puppet_eval_expr(puppet_expr_t *expr, puppet_env_t *env);
puppet_value_t *puppet_eval_variable(const char *name, puppet_location_t loc, puppet_env_t *env);
puppet_value_t *puppet_eval_binop(puppet_binop_t op, puppet_value_t *left, puppet_value_t *right);
puppet_value_t *puppet_eval_unop(puppet_unop_t op, puppet_value_t *operand);

/* Statement execution */
void puppet_exec_stmt(puppet_stmt_t *stmt, puppet_env_t *env);
void puppet_exec_stmt_list(puppet_stmt_list_t *stmts, puppet_env_t *env);
void puppet_exec_assignment(const char *var, puppet_expr_t *value, puppet_env_t *env);

/* Tag management */
void puppet_apply_current_tags(puppet_env_t *env, const char *type, const char *title);
void puppet_exec_class_def(puppet_stmt_t *class_stmt, puppet_env_t *env);
void puppet_exec_class_instance(puppet_stmt_t *class_instance_stmt, puppet_env_t *env);
void puppet_exec_include(puppet_stmt_t *include_stmt, puppet_env_t *env);
void puppet_exec_node(puppet_stmt_t *node_stmt, puppet_env_t *env);

/* Program execution */
void puppet_exec_program(puppet_program_t *program, puppet_env_t *env);

/* Module loader integration */
void puppet_env_set_loader(puppet_env_t *env, struct puppet_loader *loader);

/* Node filtering */
void puppet_env_set_node(puppet_env_t *env, const char *node_name);
void puppet_env_set_execute_all_nodes(puppet_env_t *env, bool execute_all);

/* Template output */
void puppet_env_set_template_output(puppet_env_t *env, const char *template_target);

/* Catalog building */
void puppet_env_enable_catalog(puppet_env_t *env, const char *certname, const char *environment);
puppet_catalog_t *puppet_env_get_catalog(puppet_env_t *env);

/* PuppetDB integration for exported resources */
void puppet_env_set_puppetdb(puppet_env_t *env, puppetdb_t *pdb);

/* CI validation tracking */
void puppet_env_get_stats(puppet_env_t *env, size_t *nodes_processed, size_t *nodes_failed,
                          size_t *nodes_skipped_regex, size_t *errors, size_t *warnings);
void puppet_env_increment_error(puppet_env_t *env);
void puppet_env_increment_warning(puppet_env_t *env);

/* Parallel node processing */
void puppet_env_set_parallel_nodes(puppet_env_t *env, bool parallel);
puppet_env_t *puppet_env_clone_for_node(puppet_env_t *source, const char *certname);
void puppet_env_merge_stats(puppet_env_t *target, puppet_env_t *source);

/* Buffered output for parallel mode */
void puppet_env_buffer_init(puppet_env_t *env);
void puppet_env_buffer_free(puppet_env_t *env);
void puppet_env_buffer_printf(puppet_env_t *env, const char *format, ...);
void puppet_env_buffer_flush(puppet_env_t *env);

#endif