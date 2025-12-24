/**
 * @file puppet_provider.h
 * @brief Resource provider abstraction for multi-platform support
 *
 * Provides a generic interface for resource management with
 * platform-specific implementations (Debian, RedHat, etc.)
 */

#ifndef PUPPET_PROVIDER_H
#define PUPPET_PROVIDER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Provider Types and Structures
 * ============================================================================ */

/**
 * @brief OS family enumeration
 */
typedef enum {
    OS_FAMILY_UNKNOWN = 0,
    OS_FAMILY_DEBIAN,      /* Debian, Ubuntu, Mint */
    OS_FAMILY_REDHAT,      /* RHEL, CentOS, Fedora, Rocky, Alma */
    OS_FAMILY_SUSE,        /* SLES, openSUSE */
    OS_FAMILY_ARCHLINUX,   /* Arch, Manjaro */
    OS_FAMILY_GENTOO,
    OS_FAMILY_ALPINE
} os_family_t;

/**
 * @brief Resource state
 */
typedef enum {
    RESOURCE_STATE_UNKNOWN = 0,
    RESOURCE_STATE_PRESENT,
    RESOURCE_STATE_ABSENT,
    RESOURCE_STATE_RUNNING,
    RESOURCE_STATE_STOPPED,
    RESOURCE_STATE_INSTALLED,
    RESOURCE_STATE_PURGED
} resource_state_t;

/**
 * @brief Apply result
 */
typedef enum {
    APPLY_SUCCESS = 0,
    APPLY_NOOP,            /* No changes needed */
    APPLY_CHANGED,         /* Resource was modified */
    APPLY_FAILED,          /* Application failed */
    APPLY_SKIPPED          /* Skipped (noop mode or dependency) */
} apply_result_t;

/**
 * @brief Resource parameter (key-value pair)
 */
typedef struct {
    char *name;
    char *value;
} resource_param_t;

/**
 * @brief Resource definition from catalog
 */
typedef struct {
    char *type;            /* Resource type (file, package, service, etc.) */
    char *title;           /* Resource title/name */
    resource_param_t *params;
    size_t param_count;
} resource_t;

/**
 * @brief Apply context
 */
typedef struct {
    os_family_t os_family;
    bool noop;             /* No-op mode - don't make changes */
    bool verbose;
    char *last_error;
    int changes_made;
    int failures;
} apply_context_t;

/* ============================================================================
 * Provider Interface
 * ============================================================================ */

/**
 * @brief Provider function types
 */
typedef apply_result_t (*provider_apply_fn)(const resource_t *resource, apply_context_t *ctx);
typedef resource_state_t (*provider_check_fn)(const resource_t *resource, apply_context_t *ctx);
typedef void (*provider_cleanup_fn)(void);

/**
 * @brief Provider definition
 */
typedef struct {
    const char *name;           /* Provider name (e.g., "apt", "dnf", "systemd") */
    const char *resource_type;  /* Resource type this handles */
    os_family_t os_family;      /* OS family (0 for generic) */
    provider_apply_fn apply;    /* Apply resource */
    provider_check_fn check;    /* Check current state */
    provider_cleanup_fn cleanup;/* Cleanup function */
} provider_t;

/* ============================================================================
 * Provider Registry Functions
 * ============================================================================ */

/**
 * @brief Initialize provider system
 * @param os_family The OS family to use for provider selection
 */
void providers_init(os_family_t os_family);

/**
 * @brief Shutdown provider system
 */
void providers_shutdown(void);

/**
 * @brief Register a provider
 * @param provider Provider to register
 * @return 0 on success, -1 on failure
 */
int provider_register(const provider_t *provider);

/**
 * @brief Get provider for a resource type
 * @param resource_type Resource type (file, package, service, etc.)
 * @return Provider or NULL if not found
 */
const provider_t *provider_get(const char *resource_type);

/**
 * @brief Detect OS family from string
 * @param os_family_str OS family string from facter
 * @return OS family enum value
 */
os_family_t os_family_from_string(const char *os_family_str);

/**
 * @brief Get OS family name
 * @param family OS family enum
 * @return String name
 */
const char *os_family_to_string(os_family_t family);

/* ============================================================================
 * Resource Functions
 * ============================================================================ */

/**
 * @brief Create a resource from catalog data
 * @param type Resource type
 * @param title Resource title
 * @return New resource or NULL
 */
resource_t *resource_create(const char *type, const char *title);

/**
 * @brief Add parameter to resource
 * @param resource Resource to modify
 * @param name Parameter name
 * @param value Parameter value
 * @return 0 on success
 */
int resource_add_param(resource_t *resource, const char *name, const char *value);

/**
 * @brief Get parameter value from resource
 * @param resource Resource to query
 * @param name Parameter name
 * @return Parameter value or NULL
 */
const char *resource_get_param(const resource_t *resource, const char *name);

/**
 * @brief Free a resource
 * @param resource Resource to free
 */
void resource_free(resource_t *resource);

/* ============================================================================
 * Apply Context Functions
 * ============================================================================ */

/**
 * @brief Create apply context
 * @param os_family OS family
 * @param noop No-op mode flag
 * @param verbose Verbose output flag
 * @return New context or NULL
 */
apply_context_t *apply_context_create(os_family_t os_family, bool noop, bool verbose);

/**
 * @brief Free apply context
 * @param ctx Context to free
 */
void apply_context_free(apply_context_t *ctx);

/**
 * @brief Set error message in context
 * @param ctx Context
 * @param fmt Format string
 */
void apply_context_set_error(apply_context_t *ctx, const char *fmt, ...);

/* ============================================================================
 * High-Level Apply Functions
 * ============================================================================ */

/**
 * @brief Apply a single resource
 * @param resource Resource to apply
 * @param ctx Apply context
 * @return Apply result
 */
apply_result_t resource_apply(const resource_t *resource, apply_context_t *ctx);

/**
 * @brief Apply all resources from a catalog JSON
 * @param catalog_json Catalog JSON string
 * @param ctx Apply context
 * @return Number of failures (0 = success)
 */
int catalog_apply(const char *catalog_json, apply_context_t *ctx);

/* ============================================================================
 * Built-in Provider Registration
 * ============================================================================ */

/**
 * @brief Register file provider
 */
void provider_file_register(void);

/**
 * @brief Register package providers (apt, dnf, etc.)
 */
void provider_package_register(void);

/**
 * @brief Register service provider (systemd)
 */
void provider_service_register(void);

/**
 * @brief Register notify provider
 */
void provider_notify_register(void);

/**
 * @brief Register exec provider
 */
void provider_exec_register(void);

#ifdef __cplusplus
}
#endif

#endif /* PUPPET_PROVIDER_H */
