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
        struct puppet_env *env,    /**< Current environment/scope */
        void *provider_data        /**< Provider-specific data */
    );
    
    /** Check if key exists (optional optimization) */
    bool (*has_key)(const char *key, struct puppet_env *env, void *data);
    
    /** Initialize provider (load config, connect, etc.) */
    int (*init)(void **provider_data, const char *config);
    
    /** Cleanup provider */
    void (*cleanup)(void *provider_data);
    
    void *data;                   /**< Provider-specific data */
} puppet_data_provider_t;

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
 * @brief Execution environment with scope management
 * 
 * The environment maintains the runtime state for Puppet evaluation,
 * including the current variable scope, scope stack for nested contexts,
 * and global scope for top-level variables. Provides the context needed
 * for expression evaluation and statement execution.
 */
typedef struct puppet_env {
    puppet_scope_t *global_scope;   /**< Top-level variables */
    puppet_scope_t *current_scope;  /**< Currently active scope */
    puppet_scope_t **scope_stack;   /**< Stack of nested scopes */
    size_t stack_depth;            /**< Current stack depth */
    size_t stack_capacity;         /**< Maximum stack capacity */
    struct puppet_loader *loader;   /**< Module loader for includes */
    char *node_name;               /**< Current node name (for filtering) */
    bool execute_all_nodes;        /**< Execute all nodes regardless of name */
    
    /* Enhanced variable system */
    puppet_data_provider_t **data_providers;  /**< External data providers (Hiera, etc.) */
    size_t data_provider_count;               /**< Number of registered providers */
    size_t data_provider_capacity;            /**< Provider array capacity */
    puppet_scope_t *node_scope;               /**< Node-specific variables */
    puppet_scope_t *class_scope;              /**< Current class scope */
} puppet_env_t;

/*
 * ===========================================================================
 * PUBLIC API FUNCTIONS
 * ===========================================================================
 */

/* Environment management */
puppet_env_t *puppet_env_create(void);
void puppet_env_destroy(puppet_env_t *env);

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

/* Expression evaluation */
puppet_value_t *puppet_eval_expr(puppet_expr_t *expr, puppet_env_t *env);
puppet_value_t *puppet_eval_variable(const char *name, puppet_env_t *env);
puppet_value_t *puppet_eval_binop(puppet_binop_t op, puppet_value_t *left, puppet_value_t *right);
puppet_value_t *puppet_eval_unop(puppet_unop_t op, puppet_value_t *operand);

/* Statement execution */
void puppet_exec_stmt(puppet_stmt_t *stmt, puppet_env_t *env);
void puppet_exec_stmt_list(puppet_stmt_list_t *stmts, puppet_env_t *env);
void puppet_exec_assignment(const char *var, puppet_expr_t *value, puppet_env_t *env);
void puppet_exec_class_def(puppet_stmt_t *class_stmt, puppet_env_t *env);
void puppet_exec_include(puppet_stmt_t *include_stmt, puppet_env_t *env);
void puppet_exec_node(puppet_stmt_t *node_stmt, puppet_env_t *env);

/* Program execution */
void puppet_exec_program(puppet_program_t *program, puppet_env_t *env);

/* Module loader integration */
void puppet_env_set_loader(puppet_env_t *env, struct puppet_loader *loader);

/* Node filtering */
void puppet_env_set_node(puppet_env_t *env, const char *node_name);
void puppet_env_set_execute_all_nodes(puppet_env_t *env, bool execute_all);

#endif