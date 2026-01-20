/**
 * @file puppet_ca.h
 * @brief Certificate Authority for Puppet C implementation
 *
 * This module provides Certificate Authority functionality for the Puppet server,
 * including CA certificate generation, CSR signing, and certificate management.
 *
 * Features:
 * - CA certificate and private key generation
 * - Certificate signing request (CSR) handling
 * - X.509 certificate signing with serial number management
 * - Signed certificate storage and retrieval
 * - Auto-signing policy support (policy-based, whitelist, naive)
 * - OpenSSL EVP API for OpenSSL 3.0+ compatibility
 */

#ifndef PUPPET_CA_H
#define PUPPET_CA_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>

#include "puppet_autosign.h"  /* For puppet_csr_info_t, puppet_autosign_config_t */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ===========================================================================
 * CA CONFIGURATION
 * ===========================================================================
 */

/**
 * @brief Default CA certificate validity period (10 years)
 */
#define PUPPET_CA_VALIDITY_DAYS (365 * 10)

/**
 * @brief Default signed certificate validity period (5 years)
 */
#define PUPPET_CA_CERT_VALIDITY_DAYS (365 * 5)

/**
 * @brief Default RSA key size for CA and certificates
 */
#define PUPPET_CA_KEY_SIZE 2048

/**
 * @brief Maximum path length
 */
#define PUPPET_CA_MAX_PATH 4096

/**
 * @brief Maximum error message length
 */
#define PUPPET_CA_MAX_ERROR 256

/**
 * @brief Maximum certname (common name) length
 */
#define PUPPET_CA_MAX_CERTNAME 256

/**
 * @brief Serial number file name
 */
#define PUPPET_CA_SERIAL_FILE "serial"

/**
 * @brief CA certificate file name
 */
#define PUPPET_CA_CERT_FILE "ca_crt.pem"

/**
 * @brief CA private key file name
 */
#define PUPPET_CA_KEY_FILE "ca_key.pem"

/**
 * @brief Directory for signed certificates
 */
#define PUPPET_CA_SIGNED_DIR "signed"

/*
 * ===========================================================================
 * DATA STRUCTURES
 * ===========================================================================
 */

/**
 * @brief Certificate Authority context
 */
typedef struct puppet_ca_ctx {
    char *ca_dir;               /**< CA directory path (e.g., /etc/puppetc/ssl/ca) */
    X509 *ca_cert;              /**< CA certificate */
    EVP_PKEY *ca_key;           /**< CA private key */
    long serial_number;         /**< Current serial number for certificate signing */
    char last_error[PUPPET_CA_MAX_ERROR]; /**< Last error message */
} puppet_ca_ctx_t;

/* puppet_csr_info_t, puppet_autosign_mode_t, puppet_autosign_config_t
 * are defined in puppet_autosign.h */

/*
 * ===========================================================================
 * CA INITIALIZATION AND CLEANUP
 * ===========================================================================
 */

/**
 * @brief Initialize a Certificate Authority context
 *
 * Creates a new CA context and loads existing CA certificate and private key
 * if they exist. If they don't exist, the CA must be generated using
 * puppet_ca_generate().
 *
 * @param ca_dir Path to CA directory (e.g., /etc/puppetc/ssl/ca)
 * @return New CA context, or NULL on error
 */
puppet_ca_ctx_t *puppet_ca_init(const char *ca_dir);

/**
 * @brief Free a Certificate Authority context
 *
 * Releases all resources associated with a CA context.
 *
 * @param ctx CA context to free
 */
void puppet_ca_free(puppet_ca_ctx_t *ctx);

/*
 * ===========================================================================
 * CA GENERATION AND MANAGEMENT
 * ===========================================================================
 */

/**
 * @brief Generate a new Certificate Authority
 *
 * Generates a new CA certificate and private key with the specified validity
 * period. The CA certificate is self-signed and can be used to sign client
 * certificates.
 *
 * @param ctx CA context
 * @param subject CA subject DN (e.g., "CN=Puppet CA: puppet-server")
 * @param validity_days Number of days the CA certificate is valid
 * @return 0 on success, -1 on error
 */
int puppet_ca_generate(puppet_ca_ctx_t *ctx, const char *subject, int validity_days);

/**
 * @brief Load existing CA certificate and private key
 *
 * Loads the CA certificate and private key from disk.
 *
 * @param ctx CA context
 * @return 0 on success, -1 on error
 */
int puppet_ca_load(puppet_ca_ctx_t *ctx);

/**
 * @brief Save CA certificate and private key to disk
 *
 * Saves the CA certificate and private key in PEM format.
 * The private key is saved with 0600 permissions (owner read/write only).
 *
 * @param ctx CA context
 * @return 0 on success, -1 on error
 */
int puppet_ca_save(puppet_ca_ctx_t *ctx);

/**
 * @brief Check if CA certificate and key exist
 *
 * Checks whether the CA certificate and private key files exist in the
 * CA directory.
 *
 * @param ctx CA context
 * @return true if both files exist, false otherwise
 */
bool puppet_ca_exists(puppet_ca_ctx_t *ctx);

/**
 * @brief Generate a server certificate for TLS
 *
 * Generates a server certificate signed by the CA, suitable for HTTPS/TLS.
 * The certificate is stored as server_crt.pem and server_key.pem in the
 * CA directory. The SAN includes the hostname and any additional IP addresses.
 *
 * @param ctx CA context (must have loaded CA)
 * @param hostname Server hostname for the certificate CN
 * @param ip_addresses NULL-terminated array of IP addresses to include in SAN (can be NULL)
 * @param validity_days Number of days the certificate is valid
 * @return 0 on success, -1 on error
 */
int puppet_ca_generate_server_cert(puppet_ca_ctx_t *ctx,
                                    const char *hostname,
                                    const char **ip_addresses,
                                    int validity_days);

/**
 * @brief Check if server certificate exists
 *
 * @param ctx CA context
 * @return true if server certificate and key exist, false otherwise
 */
bool puppet_ca_server_cert_exists(puppet_ca_ctx_t *ctx);

/*
 * ===========================================================================
 * CERTIFICATE SIGNING
 * ===========================================================================
 */

/**
 * @brief Sign a certificate signing request
 *
 * Signs a CSR and generates a signed X.509 certificate. The certificate
 * will be valid for the specified number of days and will include the
 * certname as the Common Name (CN).
 *
 * @param ctx CA context
 * @param csr_pem CSR in PEM format
 * @param certname Certificate name (used for storage and validation)
 * @param validity_days Number of days the certificate is valid
 * @param signed_cert_pem Output buffer for signed certificate in PEM format (caller must free)
 * @return 0 on success, -1 on error
 */
int puppet_ca_sign_csr(puppet_ca_ctx_t *ctx,
                       const char *csr_pem,
                       const char *certname,
                       int validity_days,
                       char **signed_cert_pem);

/**
 * @brief Parse CSR and extract information
 *
 * Parses a certificate signing request and extracts useful information
 * such as the certname (CN), subject DN, and public key.
 *
 * @param csr_pem CSR in PEM format
 * @param info Output CSR information structure (caller must free with puppet_csr_info_free)
 * @return 0 on success, -1 on error
 */
int puppet_ca_parse_csr(const char *csr_pem, puppet_csr_info_t **info);

/* puppet_csr_info_free is declared in puppet_autosign.h */

/*
 * ===========================================================================
 * CERTIFICATE STORAGE AND RETRIEVAL
 * ===========================================================================
 */

/**
 * @brief Save a signed certificate to disk
 *
 * Saves a signed certificate to the CA's signed certificates directory.
 * The certificate is saved as <certname>.pem.
 *
 * @param ctx CA context
 * @param certname Certificate name
 * @param cert_pem Certificate in PEM format
 * @return 0 on success, -1 on error
 */
int puppet_ca_save_signed_cert(puppet_ca_ctx_t *ctx,
                                const char *certname,
                                const char *cert_pem);

/**
 * @brief Load a signed certificate from disk
 *
 * Loads a previously signed certificate from the CA's signed certificates
 * directory.
 *
 * @param ctx CA context
 * @param certname Certificate name
 * @param cert_pem Output buffer for certificate in PEM format (caller must free)
 * @return 0 on success, -1 on error (including if certificate doesn't exist)
 */
int puppet_ca_load_signed_cert(puppet_ca_ctx_t *ctx,
                                const char *certname,
                                char **cert_pem);

/**
 * @brief Check if a certificate has been signed
 *
 * Checks whether a certificate with the given certname has already been
 * signed and stored.
 *
 * @param ctx CA context
 * @param certname Certificate name
 * @return true if certificate exists, false otherwise
 */
bool puppet_ca_cert_exists(puppet_ca_ctx_t *ctx, const char *certname);

/*
 * ===========================================================================
 * SERIAL NUMBER MANAGEMENT
 * ===========================================================================
 */

/**
 * @brief Get the next serial number for certificate signing
 *
 * Retrieves the next available serial number and increments the stored
 * serial number on disk.
 *
 * @param ctx CA context
 * @return Next serial number, or -1 on error
 */
long puppet_ca_get_next_serial(puppet_ca_ctx_t *ctx);

/**
 * @brief Save serial number to disk
 *
 * Saves the current serial number to the serial file.
 *
 * @param ctx CA context
 * @return 0 on success, -1 on error
 */
int puppet_ca_save_serial(puppet_ca_ctx_t *ctx);

/**
 * @brief Load serial number from disk
 *
 * Loads the serial number from the serial file. If the file doesn't exist,
 * initializes the serial number to 1.
 *
 * @param ctx CA context
 * @return 0 on success, -1 on error
 */
int puppet_ca_load_serial(puppet_ca_ctx_t *ctx);

/* Auto-signing functions (puppet_autosign_init, puppet_autosign_free,
 * puppet_autosign_should_sign, puppet_autosign_get_error) are declared
 * in puppet_autosign.h */

/*
 * ===========================================================================
 * CSR (CERTIFICATE SIGNING REQUEST) MANAGEMENT
 * ===========================================================================
 */

/**
 * @brief Directory for pending CSRs
 */
#define PUPPET_CA_REQUESTS_DIR "requests"

/**
 * @brief Save a pending CSR to disk
 *
 * Saves a CSR to the CA's requests directory for later manual signing.
 *
 * @param ctx CA context
 * @param certname Certificate name
 * @param csr_pem CSR in PEM format
 * @return 0 on success, -1 on error
 */
int puppet_ca_save_csr(puppet_ca_ctx_t *ctx,
                       const char *certname,
                       const char *csr_pem);

/**
 * @brief Load a pending CSR from disk
 *
 * @param ctx CA context
 * @param certname Certificate name
 * @param csr_pem Output buffer for CSR in PEM format (caller must free)
 * @return 0 on success, -1 on error
 */
int puppet_ca_load_csr(puppet_ca_ctx_t *ctx,
                       const char *certname,
                       char **csr_pem);

/**
 * @brief Check if a pending CSR exists
 *
 * @param ctx CA context
 * @param certname Certificate name
 * @return true if CSR exists, false otherwise
 */
bool puppet_ca_csr_exists(puppet_ca_ctx_t *ctx, const char *certname);

/**
 * @brief Delete a pending CSR
 *
 * @param ctx CA context
 * @param certname Certificate name
 * @return 0 on success, -1 on error
 */
int puppet_ca_delete_csr(puppet_ca_ctx_t *ctx, const char *certname);

/**
 * @brief List all pending CSRs
 *
 * Returns a NULL-terminated array of certnames with pending CSRs.
 * Caller must free the array and each string.
 *
 * @param ctx CA context
 * @param count Output: number of pending CSRs
 * @return Array of certnames, or NULL on error
 */
char **puppet_ca_list_pending_csrs(puppet_ca_ctx_t *ctx, int *count);

/**
 * @brief List all signed certificates
 *
 * Returns a NULL-terminated array of certnames with signed certificates.
 * Caller must free the array and each string.
 *
 * @param ctx CA context
 * @param count Output: number of signed certificates
 * @return Array of certnames, or NULL on error
 */
char **puppet_ca_list_signed_certs(puppet_ca_ctx_t *ctx, int *count);

/**
 * @brief Delete a signed certificate
 *
 * @param ctx CA context
 * @param certname Certificate name
 * @return 0 on success, -1 on error
 */
int puppet_ca_delete_signed_cert(puppet_ca_ctx_t *ctx, const char *certname);

/*
 * ===========================================================================
 * ERROR HANDLING
 * ===========================================================================
 */

/**
 * @brief Get the last error message from a CA context
 *
 * Returns the last error message recorded by the CA context.
 *
 * @param ctx CA context
 * @return Error message string (do not free)
 */
const char *puppet_ca_get_error(puppet_ca_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* PUPPET_CA_H */
