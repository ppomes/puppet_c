/**
 * @file facter.h
 * @brief Native fact collection library for Puppet
 *
 * libfacter_c collects system facts from the local machine:
 * - System: hostname, fqdn, domain, uptime
 * - OS: name, family, release, architecture
 * - Kernel: name, version, release
 * - Networking: interfaces, IP addresses, MAC addresses
 * - Memory: total, free, used
 * - Processors: count, models
 * - Virtual: hypervisor detection
 */

#ifndef FACTER_H
#define FACTER_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque facter context handle
 */
typedef struct facter_ctx facter_ctx_t;

/**
 * @brief Fact value types
 */
typedef enum {
    FACTER_VALUE_STRING,
    FACTER_VALUE_INTEGER,
    FACTER_VALUE_FLOAT,
    FACTER_VALUE_BOOLEAN,
    FACTER_VALUE_ARRAY,
    FACTER_VALUE_HASH
} facter_value_type_t;

/**
 * @brief Fact value structure
 */
typedef struct facter_value {
    facter_value_type_t type;
    union {
        char *string_val;
        long integer_val;
        double float_val;
        bool boolean_val;
        struct {
            struct facter_value **items;
            size_t count;
        } array_val;
        struct {
            char **keys;
            struct facter_value **values;
            size_t count;
        } hash_val;
    } data;
} facter_value_t;

/**
 * @brief Network interface information
 */
typedef struct {
    char *name;          /* Interface name (eth0, ens33, etc.) */
    char *ip;            /* IPv4 address */
    char *ip6;           /* IPv6 address */
    char *mac;           /* MAC address */
    char *netmask;       /* Network mask */
    int mtu;             /* MTU */
    bool up;             /* Interface is up */
} facter_interface_t;

/* ============================================================================
 * Context Management
 * ============================================================================ */

/**
 * @brief Create a new facter context
 * @return New context or NULL on failure
 */
facter_ctx_t *facter_create(void);

/**
 * @brief Destroy a facter context and free all resources
 * @param ctx Context to destroy
 */
void facter_destroy(facter_ctx_t *ctx);

/**
 * @brief Collect all facts
 * @param ctx Facter context
 * @return 0 on success, -1 on failure
 */
int facter_collect(facter_ctx_t *ctx);

/**
 * @brief Collect specific fact category
 * @param ctx Facter context
 * @param category Category name ("os", "networking", "memory", etc.)
 * @return 0 on success, -1 on failure
 */
int facter_collect_category(facter_ctx_t *ctx, const char *category);

/* ============================================================================
 * Fact Access
 * ============================================================================ */

/**
 * @brief Get a fact value by name
 * @param ctx Facter context
 * @param name Fact name (e.g., "hostname", "os.name", "networking.ip")
 * @return Fact value or NULL if not found
 */
const facter_value_t *facter_get(facter_ctx_t *ctx, const char *name);

/**
 * @brief Get a string fact value
 * @param ctx Facter context
 * @param name Fact name
 * @return String value or NULL if not found or not a string
 */
const char *facter_get_string(facter_ctx_t *ctx, const char *name);

/**
 * @brief Get an integer fact value
 * @param ctx Facter context
 * @param name Fact name
 * @param value Pointer to store the value
 * @return 0 on success, -1 if not found or not an integer
 */
int facter_get_integer(facter_ctx_t *ctx, const char *name, long *value);

/**
 * @brief Get a boolean fact value
 * @param ctx Facter context
 * @param name Fact name
 * @param value Pointer to store the value
 * @return 0 on success, -1 if not found or not a boolean
 */
int facter_get_boolean(facter_ctx_t *ctx, const char *name, bool *value);

/**
 * @brief Check if a fact exists
 * @param ctx Facter context
 * @param name Fact name
 * @return true if fact exists, false otherwise
 */
bool facter_has(facter_ctx_t *ctx, const char *name);

/**
 * @brief Get list of all fact names
 * @param ctx Facter context
 * @param count Pointer to store the count
 * @return Array of fact names (do not free)
 */
const char **facter_list(facter_ctx_t *ctx, size_t *count);

/**
 * @brief Set a string fact value
 * @param ctx Facter context
 * @param name Fact name
 * @param value Fact value
 * @return 0 on success, -1 on failure
 */
int facter_set_string(facter_ctx_t *ctx, const char *name, const char *value);

/* ============================================================================
 * Output Formats
 * ============================================================================ */

/**
 * @brief Export all facts as JSON
 * @param ctx Facter context
 * @return JSON string (caller must free)
 */
char *facter_to_json(facter_ctx_t *ctx);

/**
 * @brief Export all facts as YAML
 * @param ctx Facter context
 * @return YAML string (caller must free)
 */
char *facter_to_yaml(facter_ctx_t *ctx);

/**
 * @brief Export facts in facter-compatible format (key=value)
 * @param ctx Facter context
 * @return Text string (caller must free)
 */
char *facter_to_text(facter_ctx_t *ctx);

/* ============================================================================
 * Direct Fact Collectors (for specific facts without full collection)
 * ============================================================================ */

/**
 * @brief Get hostname
 * @return Hostname string (caller must free)
 */
char *facter_hostname(void);

/**
 * @brief Get fully qualified domain name
 * @return FQDN string (caller must free)
 */
char *facter_fqdn(void);

/**
 * @brief Get domain name
 * @return Domain string (caller must free)
 */
char *facter_domain(void);

/**
 * @brief Get OS name
 * @return OS name string (caller must free)
 */
char *facter_os_name(void);

/**
 * @brief Get OS family
 * @return OS family string (caller must free)
 */
char *facter_os_family(void);

/**
 * @brief Get OS release version
 * @return OS release string (caller must free)
 */
char *facter_os_release(void);

/**
 * @brief Get hardware architecture
 * @return Architecture string (caller must free)
 */
char *facter_architecture(void);

/**
 * @brief Get kernel name
 * @return Kernel name string (caller must free)
 */
char *facter_kernel(void);

/**
 * @brief Get kernel version
 * @return Kernel version string (caller must free)
 */
char *facter_kernelversion(void);

/**
 * @brief Get kernel release
 * @return Kernel release string (caller must free)
 */
char *facter_kernelrelease(void);

/**
 * @brief Get primary IP address
 * @return IP address string (caller must free)
 */
char *facter_ipaddress(void);

/**
 * @brief Get primary MAC address
 * @return MAC address string (caller must free)
 */
char *facter_macaddress(void);

/**
 * @brief Get total memory in bytes
 * @return Total memory in bytes
 */
unsigned long facter_memory_total(void);

/**
 * @brief Get free memory in bytes
 * @return Free memory in bytes
 */
unsigned long facter_memory_free(void);

/**
 * @brief Get processor count
 * @return Number of processors
 */
int facter_processor_count(void);

/**
 * @brief Get system uptime in seconds
 * @return Uptime in seconds
 */
long facter_uptime_seconds(void);

/**
 * @brief Check if running in a virtual machine
 * @return true if virtual, false if physical
 */
bool facter_is_virtual(void);

/**
 * @brief Get hypervisor name if virtual
 * @return Hypervisor name or NULL if physical (caller must free)
 */
char *facter_virtual(void);

/**
 * @brief Get network interfaces
 * @param count Pointer to store interface count
 * @return Array of interface structures (caller must free with facter_interfaces_free)
 */
facter_interface_t *facter_interfaces(size_t *count);

/**
 * @brief Free interface array
 * @param interfaces Array to free
 * @param count Number of interfaces
 */
void facter_interfaces_free(facter_interface_t *interfaces, size_t count);

/* ============================================================================
 * Value Helpers
 * ============================================================================ */

/**
 * @brief Create a string value
 * @param str String to copy
 * @return New value or NULL on failure
 */
facter_value_t *facter_value_string(const char *str);

/**
 * @brief Create an integer value
 * @param val Integer value
 * @return New value or NULL on failure
 */
facter_value_t *facter_value_integer(long val);

/**
 * @brief Create a boolean value
 * @param val Boolean value
 * @return New value or NULL on failure
 */
facter_value_t *facter_value_boolean(bool val);

/**
 * @brief Free a fact value
 * @param value Value to free
 */
void facter_value_free(facter_value_t *value);

/* ============================================================================
 * Library Info
 * ============================================================================ */

/**
 * @brief Get library version
 * @return Version string
 */
const char *facter_version(void);

#ifdef __cplusplus
}
#endif

#endif /* FACTER_H */
