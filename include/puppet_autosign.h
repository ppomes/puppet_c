/**
 * @file puppet_autosign.h
 * @brief Certificate auto-signing policy for Puppet CA
 *
 * This module provides auto-signing functionality for certificate signing requests (CSRs).
 * It supports multiple auto-signing modes:
 * - NONE: No auto-signing (manual approval only)
 * - NAIVE: Auto-sign all CSRs (insecure, testing only)
 * - WHITELIST: Auto-sign if certname is in whitelist file
 * - POLICY: Auto-sign based on policy executable
 *
 * Features:
 * - Configuration file parsing for auto-signing settings
 * - Policy-based validation via external executable
 * - Whitelist file support with pattern matching
 * - CSR validation and evaluation
 */

#ifndef PUPPET_AUTOSIGN_H
#define PUPPET_AUTOSIGN_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ===========================================================================
 * CONFIGURATION
 * ===========================================================================
 */

/**
 * @brief Maximum path length for policy/whitelist files
 */
#define PUPPET_AUTOSIGN_MAX_PATH 4096

/**
 * @brief Maximum error message length
 */
#define PUPPET_AUTOSIGN_MAX_ERROR 256

/**
 * @brief Maximum certname length
 */
#define PUPPET_AUTOSIGN_MAX_CERTNAME 256

/**
 * @brief Maximum line length in whitelist file
 */
#define PUPPET_AUTOSIGN_MAX_LINE 512

/*
 * ===========================================================================
 * DATA STRUCTURES
 * ===========================================================================
 */

/**
 * @brief Auto-signing policy modes
 */
typedef enum {
    PUPPET_AUTOSIGN_NONE,       /**< No auto-signing (manual approval only) */
    PUPPET_AUTOSIGN_NAIVE,      /**< Auto-sign all CSRs (insecure, testing only) */
    PUPPET_AUTOSIGN_WHITELIST,  /**< Auto-sign if certname in whitelist file */
    PUPPET_AUTOSIGN_POLICY      /**< Auto-sign based on policy executable */
} puppet_autosign_mode_t;

/**
 * @brief Auto-signing configuration
 */
typedef struct puppet_autosign_config {
    puppet_autosign_mode_t mode; /**< Auto-signing mode */
    char *policy_path;          /**< Path to policy executable (for POLICY mode) */
    char *whitelist_path;       /**< Path to whitelist file (for WHITELIST mode) */
    char last_error[PUPPET_AUTOSIGN_MAX_ERROR]; /**< Last error message */
} puppet_autosign_config_t;

/**
 * @brief Certificate signing request information
 */
typedef struct puppet_csr_info {
    char *certname;             /**< Certificate name (CN from CSR) */
    char *subject;              /**< Full subject DN */
    char *public_key_pem;       /**< Public key in PEM format */
    char *fingerprint;          /**< CSR fingerprint (SHA-256) */
    time_t request_time;        /**< When the CSR was received */
} puppet_csr_info_t;

/*
 * ===========================================================================
 * INITIALIZATION AND CLEANUP
 * ===========================================================================
 */

/**
 * @brief Initialize auto-signing configuration
 *
 * Creates a new auto-signing configuration based on the provided config file.
 * The config file determines the auto-signing mode and associated settings.
 *
 * Configuration file format (INI-style):
 *   [autosign]
 *   mode = policy|whitelist|naive|none
 *   policy_path = /path/to/policy/executable  (for policy mode)
 *   whitelist_path = /path/to/whitelist.txt   (for whitelist mode)
 *
 * @param config_path Path to autosign configuration file
 * @return New auto-signing configuration, or NULL on error
 */
puppet_autosign_config_t *puppet_autosign_init(const char *config_path);

/**
 * @brief Free auto-signing configuration
 *
 * Releases all resources associated with an auto-signing configuration.
 *
 * @param config Auto-signing configuration to free
 */
void puppet_autosign_free(puppet_autosign_config_t *config);

/*
 * ===========================================================================
 * AUTO-SIGNING EVALUATION
 * ===========================================================================
 */

/**
 * @brief Check if a CSR should be auto-signed
 *
 * Evaluates whether a CSR should be automatically signed based on the
 * configured auto-signing policy.
 *
 * For POLICY mode: Executes the policy script with CSR details on stdin
 *   - Policy script receives JSON: {"certname":"...","subject":"...","fingerprint":"..."}
 *   - Exit code 0 = approve, non-zero = reject
 *
 * For WHITELIST mode: Checks if certname is in the whitelist file
 *   - One certname per line
 *   - Comments start with #
 *   - Empty lines ignored
 *
 * For NAIVE mode: Always returns true (insecure, testing only)
 * For NONE mode: Always returns false
 *
 * @param config Auto-signing configuration
 * @param csr_info CSR information to evaluate
 * @return true if CSR should be auto-signed, false otherwise
 */
bool puppet_autosign_should_sign(puppet_autosign_config_t *config,
                                  puppet_csr_info_t *csr_info);

/*
 * ===========================================================================
 * POLICY EXECUTION
 * ===========================================================================
 */

/**
 * @brief Execute policy script for CSR evaluation
 *
 * Executes the configured policy script with CSR information passed via stdin.
 * The script should exit with code 0 to approve the CSR, or non-zero to reject.
 *
 * @param config Auto-signing configuration (must be in POLICY mode)
 * @param csr_info CSR information to evaluate
 * @return true if policy approves (exit code 0), false otherwise
 */
bool puppet_autosign_execute_policy(puppet_autosign_config_t *config,
                                     puppet_csr_info_t *csr_info);

/**
 * @brief Check if certname is in whitelist file
 *
 * Reads the whitelist file and checks if the certname matches any entry.
 * Supports exact matches and simple wildcard patterns.
 *
 * @param config Auto-signing configuration (must be in WHITELIST mode)
 * @param certname Certificate name to check
 * @return true if certname is whitelisted, false otherwise
 */
bool puppet_autosign_check_whitelist(puppet_autosign_config_t *config,
                                      const char *certname);

/*
 * ===========================================================================
 * CSR INFORMATION MANAGEMENT
 * ===========================================================================
 */

/**
 * @brief Create CSR information structure
 *
 * Allocates and initializes a new CSR information structure.
 *
 * @param certname Certificate name (CN)
 * @param subject Full subject DN
 * @param public_key_pem Public key in PEM format
 * @param fingerprint CSR fingerprint (SHA-256)
 * @return New CSR information structure, or NULL on error
 */
puppet_csr_info_t *puppet_csr_info_create(const char *certname,
                                           const char *subject,
                                           const char *public_key_pem,
                                           const char *fingerprint);

/**
 * @brief Free CSR information structure
 *
 * Releases all memory associated with a CSR information structure.
 *
 * @param info CSR information to free
 */
void puppet_csr_info_free(puppet_csr_info_t *info);

/*
 * ===========================================================================
 * ERROR HANDLING
 * ===========================================================================
 */

/**
 * @brief Get the last error message from an auto-signing configuration
 *
 * Returns the last error message recorded by the auto-signing configuration.
 *
 * @param config Auto-signing configuration
 * @return Error message string (do not free)
 */
const char *puppet_autosign_get_error(puppet_autosign_config_t *config);

/*
 * ===========================================================================
 * UTILITY FUNCTIONS
 * ===========================================================================
 */

/**
 * @brief Parse auto-signing mode from string
 *
 * Converts a mode string ("policy", "whitelist", "naive", "none") to
 * the corresponding enum value.
 *
 * @param mode_str Mode string
 * @return Auto-signing mode, or PUPPET_AUTOSIGN_NONE if invalid
 */
puppet_autosign_mode_t puppet_autosign_parse_mode(const char *mode_str);

/**
 * @brief Convert auto-signing mode to string
 *
 * Converts an auto-signing mode enum to its string representation.
 *
 * @param mode Auto-signing mode
 * @return Mode string (do not free)
 */
const char *puppet_autosign_mode_to_string(puppet_autosign_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* PUPPET_AUTOSIGN_H */
