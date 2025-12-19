/**
 * @file puppet_ast.h
 * @brief Abstract Syntax Tree (AST) definitions for Puppet language parser
 *
 * This header defines the complete AST structure for the Puppet configuration
 * language, including expressions, statements, values, and all supporting
 * data structures. The AST is designed to be memory-efficient, type-safe,
 * and suitable for both parsing and evaluation phases.
 *
 * The AST follows these design principles:
 * - Immutable structure once created (for thread safety)
 * - Explicit memory management with destroy functions
 * - Type-safe unions with explicit type tags
 * - Location tracking for error reporting
 * - Support for all Puppet language constructs
 */

#ifndef PUPPET_AST_H
#define PUPPET_AST_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * ===========================================================================
 * FUNDAMENTAL DATA TYPES
 * ===========================================================================
 */

/**
 * @brief String type with explicit length tracking
 * 
 * Puppet strings are not necessarily null-terminated and may contain
 * embedded nulls, so we track length explicitly. This is more efficient
 * than strlen() for large strings and handles binary content correctly.
 */
typedef struct puppet_string {
    char *data;         /**< String content (may contain nulls) */
    size_t len;         /**< Length in bytes (not including terminator) */
} puppet_string_t;

/**
 * @brief Source location information for error reporting and debugging
 * 
 * Tracks the original source location of AST nodes to provide meaningful
 * error messages and debugging information. The filename is typically
 * a pointer to a string owned by the lexer.
 */
typedef struct puppet_location {
    const char *filename;   /**< Source file name (borrowed pointer) */
    int line;              /**< Line number (1-based) */
    int column;            /**< Column number (1-based) */
} puppet_location_t;

/*
 * ===========================================================================
 * VALUE SYSTEM
 * ===========================================================================
 */

/**
 * @brief Puppet value types enumeration
 * 
 * Defines all possible types that can be stored in a puppet_value_t.
 * These correspond to the built-in data types in the Puppet language:
 * - Scalar types: undef, boolean, string, number
 * - Collection types: array, hash  
 * - Advanced types: regular expressions, type references
 */
typedef enum {
    PUPPET_VALUE_UNDEF,     /**< Undefined/null value */
    PUPPET_VALUE_BOOL,      /**< Boolean true/false */
    PUPPET_VALUE_STRING,    /**< String/text content */
    PUPPET_VALUE_NUMBER,    /**< Numeric value (integer or float) */
    PUPPET_VALUE_ARRAY,     /**< Ordered collection of values */
    PUPPET_VALUE_HASH,      /**< Key-value mapping */
    PUPPET_VALUE_REGEXP,    /**< Regular expression pattern */
    PUPPET_VALUE_TYPE       /**< Type reference (for type checking) */
} puppet_value_type_t;

typedef struct puppet_value puppet_value_t;
typedef struct puppet_array puppet_array_t;
typedef struct puppet_hash puppet_hash_t;

/**
 * @brief Dynamic array implementation for Puppet arrays
 * 
 * Implements a resizable array with automatic capacity management.
 * Items are stored as pointers to puppet_value_t for polymorphism.
 * The array owns all contained values and will destroy them when freed.
 */
struct puppet_array {
    puppet_value_t **items;  /**< Array of value pointers */
    size_t count;           /**< Current number of items */
    size_t capacity;        /**< Maximum items before reallocation */
};

/**
 * @brief Hash table entry for Puppet hash values
 * 
 * Uses chaining for collision resolution. Each entry contains a key-value
 * pair and a pointer to the next entry in the chain (if any).
 */
typedef struct puppet_hash_entry {
    puppet_string_t key;           /**< Hash key (string) */
    puppet_value_t *value;         /**< Associated value */
    struct puppet_hash_entry *next; /**< Next entry in collision chain */
} puppet_hash_entry_t;

/**
 * @brief Hash table implementation for Puppet hash values
 * 
 * Implements a hash table with chaining for collision resolution.
 * The hash function distributes keys across buckets for O(1) average
 * access time. The table automatically resizes when load factor exceeds
 * a threshold.
 */
struct puppet_hash {
    puppet_hash_entry_t **buckets; /**< Array of bucket heads */
    size_t bucket_count;          /**< Number of buckets */
    size_t entry_count;           /**< Total number of entries */
};

/**
 * @brief Puppet value - unified representation for all Puppet data types
 * 
 * This is the core value type that can represent any Puppet value.
 * Uses a tagged union to store different data types efficiently while
 * maintaining type safety. The type field determines which union member
 * is valid.
 * 
 * Memory management:
 * - Scalar values (bool, number): stored directly in union
 * - String values: puppet_string_t contains owned char* data
 * - Collections (array, hash): pointers to owned heap structures
 * - Complex values: various pointer types with specific cleanup needs
 */
struct puppet_value {
    puppet_value_type_t type;       /**< Discriminator for union */
    union {
        bool boolean;               /**< Boolean true/false value */
        puppet_string_t string;     /**< String content with length */
        double number;              /**< Numeric value (int or float) */
        puppet_array_t *array;      /**< Pointer to array structure */
        puppet_hash_t *hash;        /**< Pointer to hash table */
        puppet_string_t regexp;     /**< Regular expression pattern */
        void *type_ref;            /**< Type system reference */
    } data;
};

/*
 * ===========================================================================
 * EXPRESSION SYSTEM
 * ===========================================================================
 */

/**
 * @brief Expression types for the Puppet AST
 * 
 * Defines all possible expression types in the Puppet language.
 * Expressions represent computations that produce values, including:
 * - Literal values and variable references
 * - Operators (binary and unary)
 * - Function calls and method invocations
 * - Complex constructs (conditionals, lambdas)
 */
typedef enum {
    PUPPET_EXPR_VALUE,              /**< Literal value (string, number, etc.) */
    PUPPET_EXPR_VARIABLE,           /**< Variable reference ($var) */
    PUPPET_EXPR_BINOP,              /**< Binary operation (a + b, a == b, etc.) */
    PUPPET_EXPR_UNOP,               /**< Unary operation (!a, -a, etc.) */
    PUPPET_EXPR_FUNCALL,            /**< Function call (func(args)) */
    PUPPET_EXPR_INDEX,              /**< Array/hash indexing (obj[key]) */
    PUPPET_EXPR_DOT,                /**< Method call (obj.method) */
    PUPPET_EXPR_CONDITIONAL,        /**< Ternary conditional (a ? b : c) */
    PUPPET_EXPR_LAMBDA,             /**< Lambda/closure definition */
    PUPPET_EXPR_RESOURCE_REF,       /**< Resource reference (File['name']) */
    PUPPET_EXPR_INTERPOLATED_STRING /**< String with embedded expressions */
} puppet_expr_type_t;

/**
 * @brief Binary operators supported by Puppet
 * 
 * Covers arithmetic, comparison, logical, and Puppet-specific operators.
 * These map directly to operator tokens from the lexer and define the
 * semantics for binary expression evaluation.
 */
typedef enum {
    PUPPET_OP_ADD,          /**< Addition (+) */
    PUPPET_OP_SUB,          /**< Subtraction (-) */
    PUPPET_OP_MUL,          /**< Multiplication (*) */
    PUPPET_OP_DIV,          /**< Division (/) */
    PUPPET_OP_MOD,          /**< Modulo (%) */
    PUPPET_OP_EQ,           /**< Equality (==) */
    PUPPET_OP_NE,           /**< Inequality (!=) */
    PUPPET_OP_LT,           /**< Less than (<) */
    PUPPET_OP_LE,           /**< Less than or equal (<=) */
    PUPPET_OP_GT,           /**< Greater than (>) */
    PUPPET_OP_GE,           /**< Greater than or equal (>=) */
    PUPPET_OP_AND,          /**< Logical AND (and, &&) */
    PUPPET_OP_OR,           /**< Logical OR (or, ||) */
    PUPPET_OP_MATCH,        /**< Regular expression match (=~) */
    PUPPET_OP_NOT_MATCH,    /**< Regular expression non-match (!~) */
    PUPPET_OP_IN,           /**< Collection membership (in) */
    PUPPET_OP_LSHIFT,       /**< Left shift (<<) */
    PUPPET_OP_RSHIFT        /**< Right shift (>>) */
} puppet_binop_t;

/**
 * @brief Unary operators supported by Puppet
 * 
 * Defines prefix and postfix operators that take a single operand.
 * Includes logical, arithmetic, and Puppet-specific operators.
 */
typedef enum {
    PUPPET_UNOP_NOT,        /**< Logical negation (!, not) */
    PUPPET_UNOP_MINUS,      /**< Arithmetic negation (-) */
    PUPPET_UNOP_PLUS,       /**< Arithmetic positive (+) */
    PUPPET_UNOP_SPLAT       /**< Splat operator (*) for argument expansion */
} puppet_unop_t;

typedef struct puppet_expr puppet_expr_t;

/**
 * @brief Dynamic list of expressions
 * 
 * Used for function arguments, array literals, and other contexts
 * where multiple expressions are grouped together.
 */
typedef struct {
    puppet_expr_t **exprs;  /**< Array of expression pointers */
    size_t count;          /**< Number of expressions */
} puppet_expr_list_t;

typedef struct {
    puppet_string_t name;
    puppet_expr_t *default_value;
    puppet_value_t *type_constraint;
} puppet_param_t;

typedef struct {
    puppet_param_t *params;
    size_t count;
} puppet_param_list_t;

typedef struct {
    puppet_param_list_t params;
    puppet_expr_list_t body;
} puppet_lambda_t;

struct puppet_expr {
    puppet_expr_type_t type;
    puppet_location_t loc;
    union {
        puppet_value_t *value;
        puppet_string_t variable;
        struct {
            puppet_binop_t op;
            puppet_expr_t *left;
            puppet_expr_t *right;
        } binop;
        struct {
            puppet_unop_t op;
            puppet_expr_t *expr;
        } unop;
        struct {
            puppet_string_t name;
            puppet_expr_list_t args;
            puppet_lambda_t *lambda;
        } funcall;
        struct {
            puppet_expr_t *object;
            puppet_expr_t *index;
        } index;
        struct {
            puppet_expr_t *object;
            puppet_string_t field;
        } dot;
        struct {
            puppet_expr_t *condition;
            puppet_expr_t *then_expr;
            puppet_expr_t *else_expr;
        } conditional;
        puppet_lambda_t *lambda;
        struct {
            puppet_string_t type;
            puppet_expr_t *title;
        } resource_ref;
        struct {
            puppet_string_t *parts;
            puppet_expr_t **exprs;
            size_t count;
        } interpolated;
    } data;
};

/*
 * ===========================================================================
 * STATEMENT SYSTEM
 * ===========================================================================
 */

/**
 * @brief Statement types for the Puppet AST
 * 
 * Statements represent actions and declarations in Puppet code.
 * Unlike expressions, statements do not return values but perform
 * side effects like declaring resources, classes, or variables.
 */
typedef enum {
    PUPPET_STMT_RESOURCE,           /**< Resource declaration (file { ... }) */
    PUPPET_STMT_RESOURCE_DEFAULT,   /**< Resource defaults (File { ... }) */
    PUPPET_STMT_RESOURCE_OVERRIDE,  /**< Resource override (File['x'] { ... }) */
    PUPPET_STMT_RESOURCE_COLLECTOR, /**< Resource collector (File <| ... |>) */
    PUPPET_STMT_CLASS_DEF,          /**< Class definition */
    PUPPET_STMT_DEFINE,             /**< Defined type declaration */
    PUPPET_STMT_NODE,               /**< Node declaration */
    PUPPET_STMT_IF,                 /**< Conditional statement (if/elsif/else) */
    PUPPET_STMT_UNLESS,             /**< Negative conditional (unless) */
    PUPPET_STMT_CASE,               /**< Case/switch statement */
    PUPPET_STMT_ASSIGNMENT,         /**< Variable assignment ($var = value) */
    PUPPET_STMT_APPEND,             /**< Array append ($var += value) */
    PUPPET_STMT_FUNCTION_CALL,      /**< Function call as statement */
    PUPPET_STMT_RESOURCE_CHAIN,     /**< Resource chaining (A -> B) */
    PUPPET_STMT_CLASS_INSTANCE,     /**< Class instantiation (class { 'name': ... }) */
    PUPPET_STMT_INCLUDE,            /**< Class inclusion (include class) */
    PUPPET_STMT_REQUIRE,            /**< Class requirement (require class) */
    PUPPET_STMT_CONTAIN,            /**< Class containment (contain class) */
    PUPPET_STMT_TAG,                /**< Tag declaration (tag 'name') */
    PUPPET_STMT_IMPORT              /**< File import (import 'file') */
} puppet_stmt_type_t;

typedef struct puppet_stmt puppet_stmt_t;

typedef struct {
    puppet_stmt_t **stmts;
    size_t count;
} puppet_stmt_list_t;

typedef enum {
    PUPPET_RES_NORMAL,
    PUPPET_RES_VIRTUAL,
    PUPPET_RES_EXPORTED
} puppet_resource_style_t;

typedef struct {
    puppet_string_t name;
    puppet_expr_t *value;
} puppet_attribute_t;

typedef struct {
    puppet_attribute_t *attributes;
    size_t count;
} puppet_attribute_list_t;

typedef struct {
    puppet_expr_t *title;
    puppet_attribute_t *attributes;
    size_t attr_count;
} puppet_resource_instance_t;

typedef struct {
    puppet_resource_style_t style;
    puppet_string_t type;
    puppet_resource_instance_t *instances;
    size_t instance_count;
} puppet_resource_decl_t;

typedef struct {
    puppet_expr_t *test;
    puppet_stmt_list_t body;
} puppet_case_when_t;

typedef struct puppet_if_branch {
    puppet_expr_t *condition;
    puppet_stmt_list_t body;
    struct puppet_if_branch *next;
} puppet_if_branch_t;

struct puppet_stmt {
    puppet_stmt_type_t type;
    puppet_location_t loc;
    union {
        puppet_resource_decl_t resource;
        struct {
            puppet_string_t type;
            puppet_attribute_t *attributes;
            size_t attr_count;
        } resource_default;
        struct {
            puppet_expr_t *reference;
            puppet_attribute_t *attributes;
            size_t attr_count;
        } resource_override;
        struct {
            puppet_resource_style_t style;
            puppet_string_t type;
            puppet_expr_t *search_expr;
            puppet_attribute_t *attributes;
            size_t attr_count;
        } collector;
        struct {
            puppet_string_t name;
            puppet_param_list_t params;
            puppet_string_t *inherits;
            puppet_stmt_list_t body;
        } class_def;
        struct {
            puppet_string_t class_name;
            puppet_attribute_t *arguments;
            size_t arg_count;
        } class_instance;
        struct {
            puppet_string_t name;
            puppet_param_list_t params;
            puppet_stmt_list_t body;
        } define;
        struct {
            puppet_string_t name;
            puppet_stmt_list_t body;
        } node;
        struct {
            puppet_if_branch_t *branches;
            puppet_stmt_list_t *else_body;
        } if_stmt;
        struct {
            puppet_expr_t *condition;
            puppet_stmt_list_t body;
        } unless_stmt;
        struct {
            puppet_expr_t *expr;
            puppet_case_when_t *whens;
            size_t when_count;
            puppet_stmt_list_t *default_body;
        } case_stmt;
        struct {
            puppet_string_t variable;
            puppet_expr_t *value;
        } assignment;
        struct {
            puppet_string_t variable;
            puppet_expr_t *value;
        } append;
        puppet_expr_t *expr;
        struct {
            puppet_stmt_t *left;
            puppet_stmt_t *right;
            enum { CHAIN_NOTIFY, CHAIN_BEFORE } type;
        } chain;
        puppet_expr_list_t names;
        puppet_string_t import_pattern;
    } data;
};

typedef struct {
    puppet_stmt_list_t statements;
    puppet_location_t loc;
} puppet_program_t;

/*
 * ===========================================================================
 * PUBLIC API FUNCTIONS
 * ===========================================================================
 */

/* String management */
puppet_string_t puppet_string_create(const char *str);
void puppet_string_free(puppet_string_t str);

/* Value creation and management */
puppet_value_t *puppet_value_create_undef(void);
puppet_value_t *puppet_value_create_bool(bool value);
puppet_value_t *puppet_value_create_string(const char *str, size_t len);
puppet_value_t *puppet_value_create_number(double value);
puppet_value_t *puppet_value_create_array(void);
puppet_value_t *puppet_value_create_hash(void);
puppet_value_t *puppet_value_copy(puppet_value_t *value);
void puppet_value_destroy(puppet_value_t *value);

/* Collection manipulation */
void puppet_array_append(puppet_array_t *array, puppet_value_t *value);
void puppet_hash_set(puppet_hash_t *hash, const char *key, size_t key_len, puppet_value_t *value);
puppet_value_t *puppet_hash_get(puppet_hash_t *hash, const char *key, size_t key_len);

/* Expression construction */
puppet_expr_t *puppet_expr_create_value(puppet_value_t *value);
puppet_expr_t *puppet_expr_create_variable(const char *name);
puppet_expr_t *puppet_expr_create_binop(puppet_binop_t op, puppet_expr_t *left, puppet_expr_t *right);
puppet_expr_t *puppet_expr_create_unop(puppet_unop_t op, puppet_expr_t *operand);
void puppet_expr_destroy(puppet_expr_t *expr);

/* Statement construction */
puppet_stmt_t *puppet_stmt_create_resource(puppet_resource_decl_t decl);
puppet_stmt_t *puppet_stmt_create_assignment(const char *var, puppet_expr_t *value);
void puppet_stmt_destroy(puppet_stmt_t *stmt);

/* Program management */
void puppet_program_destroy(puppet_program_t *program);

#endif