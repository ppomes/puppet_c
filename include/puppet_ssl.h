/**
 * @file puppet_ssl.h
 * @brief SSL/TLS support for Puppet C implementation
 *
 * This module provides SSL/TLS functionality for secure communication
 * between Puppet agents and servers using mutual TLS authentication.
 *
 * Features:
 * - OpenSSL initialization and cleanup
 * - SSL context creation and management
 * - Certificate and private key loading
 * - Certificate validation and verification
 * - TLS 1.2+ support with strong cipher suites
 * - Mutual TLS (mTLS) authentication support
 */

#ifndef PUPPET_SSL_H
#define PUPPET_SSL_H

#include <stdbool.h>
#include <stddef.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/evp.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ===========================================================================
 * SSL/TLS CONFIGURATION
 * ===========================================================================
 */

/**
 * @brief Default minimum TLS version
 */
#define PUPPET_SSL_MIN_TLS_VERSION TLS1_2_VERSION

/**
 * @brief Maximum certificate path length
 */
#define PUPPET_SSL_MAX_PATH 4096

/**
 * @brief Maximum error message length
 */
#define PUPPET_SSL_MAX_ERROR 256

/*
 * ===========================================================================
 * DATA STRUCTURES
 * ===========================================================================
 */

/**
 * @brief SSL context wrapper for Puppet SSL operations
 */
typedef struct puppet_ssl_ctx {
    SSL_CTX *ssl_ctx;           /**< OpenSSL context */
    char *ca_cert_path;         /**< Path to CA certificate */
    char *cert_path;            /**< Path to client/server certificate */
    char *key_path;             /**< Path to private key */
    bool verify_peer;           /**< Whether to verify peer certificate */
    char last_error[PUPPET_SSL_MAX_ERROR]; /**< Last error message */
} puppet_ssl_ctx_t;

/**
 * @brief SSL connection wrapper
 */
typedef struct puppet_ssl_conn {
    SSL *ssl;                   /**< OpenSSL connection */
    int socket_fd;              /**< Underlying socket file descriptor */
    bool is_server;             /**< True if this is a server connection */
    char last_error[PUPPET_SSL_MAX_ERROR]; /**< Last error message */
} puppet_ssl_conn_t;

/**
 * @brief Certificate information structure
 */
typedef struct puppet_cert_info {
    char *subject;              /**< Certificate subject (DN) */
    char *issuer;               /**< Certificate issuer (DN) */
    char *common_name;          /**< Common name (CN) from subject */
    time_t not_before;          /**< Valid from timestamp */
    time_t not_after;           /**< Valid until timestamp */
    bool is_valid;              /**< Whether certificate is currently valid */
} puppet_cert_info_t;

/*
 * ===========================================================================
 * INITIALIZATION AND CLEANUP
 * ===========================================================================
 */

/**
 * @brief Initialize OpenSSL library
 *
 * Initializes the OpenSSL library, loads error strings, and sets up
 * algorithms. Should be called once at program startup before any
 * SSL operations.
 *
 * @return 0 on success, -1 on error
 */
int puppet_ssl_init(void);

/**
 * @brief Cleanup OpenSSL library
 *
 * Cleans up OpenSSL resources and frees error strings. Should be
 * called at program shutdown after all SSL operations are complete.
 */
void puppet_ssl_cleanup(void);

/*
 * ===========================================================================
 * SSL CONTEXT MANAGEMENT
 * ===========================================================================
 */

/**
 * @brief Create a new SSL context
 *
 * Creates and initializes an SSL context for client or server use.
 * Sets minimum TLS version to TLS 1.2 by default.
 *
 * @param is_server True for server context, false for client context
 * @return New SSL context, or NULL on error
 */
puppet_ssl_ctx_t *puppet_ssl_ctx_new(bool is_server);

/**
 * @brief Free an SSL context
 *
 * Releases all resources associated with an SSL context.
 *
 * @param ctx SSL context to free
 */
void puppet_ssl_ctx_free(puppet_ssl_ctx_t *ctx);

/**
 * @brief Load CA certificate for verification
 *
 * Loads a CA certificate used to verify peer certificates.
 *
 * @param ctx SSL context
 * @param ca_cert_path Path to CA certificate file (PEM format)
 * @return 0 on success, -1 on error
 */
int puppet_ssl_ctx_load_ca_cert(puppet_ssl_ctx_t *ctx, const char *ca_cert_path);

/**
 * @brief Load certificate and private key
 *
 * Loads the certificate and private key for this endpoint.
 * Both must be in PEM format.
 *
 * @param ctx SSL context
 * @param cert_path Path to certificate file
 * @param key_path Path to private key file
 * @return 0 on success, -1 on error
 */
int puppet_ssl_ctx_load_cert_and_key(puppet_ssl_ctx_t *ctx,
                                      const char *cert_path,
                                      const char *key_path);

/**
 * @brief Set peer verification mode
 *
 * Enables or disables peer certificate verification.
 *
 * @param ctx SSL context
 * @param verify True to enable verification (recommended)
 */
void puppet_ssl_ctx_set_verify(puppet_ssl_ctx_t *ctx, bool verify);

/**
 * @brief Set cipher suite list
 *
 * Sets the allowed cipher suites for TLS connections.
 *
 * @param ctx SSL context
 * @param cipher_list OpenSSL cipher list string
 * @return 0 on success, -1 on error
 */
int puppet_ssl_ctx_set_cipher_list(puppet_ssl_ctx_t *ctx, const char *cipher_list);

/*
 * ===========================================================================
 * SSL CONNECTION MANAGEMENT
 * ===========================================================================
 */

/**
 * @brief Create a new SSL connection
 *
 * Creates an SSL connection wrapper for the given socket.
 *
 * @param ctx SSL context to use
 * @param socket_fd Socket file descriptor
 * @param is_server True if this is a server connection
 * @return New SSL connection, or NULL on error
 */
puppet_ssl_conn_t *puppet_ssl_conn_new(puppet_ssl_ctx_t *ctx,
                                        int socket_fd,
                                        bool is_server);

/**
 * @brief Free an SSL connection
 *
 * Releases all resources associated with an SSL connection.
 * Does not close the underlying socket.
 *
 * @param conn SSL connection to free
 */
void puppet_ssl_conn_free(puppet_ssl_conn_t *conn);

/**
 * @brief Perform SSL handshake
 *
 * Performs the SSL/TLS handshake (either client or server side).
 *
 * @param conn SSL connection
 * @return 0 on success, -1 on error
 */
int puppet_ssl_conn_handshake(puppet_ssl_conn_t *conn);

/**
 * @brief Read data from SSL connection
 *
 * Reads data from an established SSL connection.
 *
 * @param conn SSL connection
 * @param buf Buffer to read into
 * @param len Maximum number of bytes to read
 * @return Number of bytes read, or -1 on error
 */
int puppet_ssl_conn_read(puppet_ssl_conn_t *conn, void *buf, size_t len);

/**
 * @brief Write data to SSL connection
 *
 * Writes data to an established SSL connection.
 *
 * @param conn SSL connection
 * @param buf Buffer to write from
 * @param len Number of bytes to write
 * @return Number of bytes written, or -1 on error
 */
int puppet_ssl_conn_write(puppet_ssl_conn_t *conn, const void *buf, size_t len);

/*
 * ===========================================================================
 * CERTIFICATE UTILITIES
 * ===========================================================================
 */

/**
 * @brief Load X.509 certificate from file
 *
 * Loads a PEM-encoded X.509 certificate from a file.
 *
 * @param cert_path Path to certificate file
 * @return X509 certificate object, or NULL on error
 */
X509 *puppet_ssl_load_cert(const char *cert_path);

/**
 * @brief Load private key from file
 *
 * Loads a PEM-encoded private key from a file.
 *
 * @param key_path Path to private key file
 * @return EVP_PKEY object, or NULL on error
 */
EVP_PKEY *puppet_ssl_load_key(const char *key_path);

/**
 * @brief Extract certificate information
 *
 * Extracts useful information from an X.509 certificate.
 *
 * @param cert X.509 certificate
 * @return Certificate info structure, or NULL on error (caller must free)
 */
puppet_cert_info_t *puppet_ssl_get_cert_info(X509 *cert);

/**
 * @brief Free certificate information structure
 *
 * @param info Certificate info to free
 */
void puppet_ssl_cert_info_free(puppet_cert_info_t *info);

/**
 * @brief Verify certificate against CA
 *
 * Verifies that a certificate was signed by the given CA certificate.
 *
 * @param cert Certificate to verify
 * @param ca_cert CA certificate
 * @return true if verification succeeds, false otherwise
 */
bool puppet_ssl_verify_cert(X509 *cert, X509 *ca_cert);

/**
 * @brief Get peer certificate from connection
 *
 * Retrieves the peer's certificate from an established SSL connection.
 *
 * @param conn SSL connection
 * @return X509 certificate object, or NULL if not available
 */
X509 *puppet_ssl_get_peer_cert(puppet_ssl_conn_t *conn);

/*
 * ===========================================================================
 * ERROR HANDLING
 * ===========================================================================
 */

/**
 * @brief Get last SSL error message
 *
 * Returns a human-readable description of the last SSL error.
 *
 * @return Error message string (do not free)
 */
const char *puppet_ssl_get_error(void);

/**
 * @brief Get last error from SSL context
 *
 * Returns the last error message stored in an SSL context.
 *
 * @param ctx SSL context
 * @return Error message string (do not free)
 */
const char *puppet_ssl_ctx_get_error(puppet_ssl_ctx_t *ctx);

/**
 * @brief Get last error from SSL connection
 *
 * Returns the last error message stored in an SSL connection.
 *
 * @param conn SSL connection
 * @return Error message string (do not free)
 */
const char *puppet_ssl_conn_get_error(puppet_ssl_conn_t *conn);

/**
 * @brief Print OpenSSL error queue
 *
 * Prints all pending errors from the OpenSSL error queue to stderr.
 * Useful for debugging SSL/TLS issues.
 */
void puppet_ssl_print_errors(void);

#ifdef __cplusplus
}
#endif

#endif /* PUPPET_SSL_H */
