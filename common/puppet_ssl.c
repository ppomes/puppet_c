/*
 * Puppet SSL/TLS Support
 *
 * This module implements SSL/TLS functionality for secure communication
 * between Puppet agents and servers using mutual TLS authentication.
 *
 * Key features:
 * - OpenSSL 3.0+ compatible EVP API usage
 * - Mutual TLS (mTLS) authentication support
 * - TLS 1.2+ with strong cipher suites
 * - Certificate validation and verification
 */

#include "puppet_ssl.h"
#include "puppet_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/bio.h>

/* Thread-local error message buffer */
static __thread char ssl_error_buffer[PUPPET_SSL_MAX_ERROR];

/*
 * ===========================================================================
 * INITIALIZATION AND CLEANUP
 * ===========================================================================
 */

/**
 * Initialize OpenSSL library
 */
int puppet_ssl_init(void) {
    /* OpenSSL 1.1.0+ initializes automatically, but we call these for compatibility */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
#endif

    memset(ssl_error_buffer, 0, sizeof(ssl_error_buffer));
    return 0;
}

/**
 * Cleanup OpenSSL library
 */
void puppet_ssl_cleanup(void) {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    EVP_cleanup();
    ERR_free_strings();
#endif
}

/*
 * ===========================================================================
 * SSL CONTEXT MANAGEMENT
 * ===========================================================================
 */

/**
 * Create a new SSL context
 */
puppet_ssl_ctx_t *puppet_ssl_ctx_new(bool is_server) {
    puppet_ssl_ctx_t *ctx = puppet_malloc(sizeof(puppet_ssl_ctx_t));
    if (!ctx) {
        snprintf(ssl_error_buffer, sizeof(ssl_error_buffer),
                "Failed to allocate SSL context");
        return NULL;
    }

    memset(ctx, 0, sizeof(puppet_ssl_ctx_t));

    /* Create SSL context using TLS method */
    const SSL_METHOD *method = is_server ? TLS_server_method() : TLS_client_method();
    ctx->ssl_ctx = SSL_CTX_new(method);
    if (!ctx->ssl_ctx) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to create SSL context");
        puppet_free(ctx);
        return NULL;
    }

    /* Set minimum TLS version to TLS 1.2 */
    SSL_CTX_set_min_proto_version(ctx->ssl_ctx, PUPPET_SSL_MIN_TLS_VERSION);

    /* Set default verification mode (require peer certificate) */
    ctx->verify_peer = true;
    SSL_CTX_set_verify(ctx->ssl_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);

    return ctx;
}

/**
 * Free an SSL context
 */
void puppet_ssl_ctx_free(puppet_ssl_ctx_t *ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->ssl_ctx) {
        SSL_CTX_free(ctx->ssl_ctx);
    }

    puppet_free(ctx->ca_cert_path);
    puppet_free(ctx->cert_path);
    puppet_free(ctx->key_path);
    puppet_free(ctx);
}

/**
 * Load CA certificate for verification
 */
int puppet_ssl_ctx_load_ca_cert(puppet_ssl_ctx_t *ctx, const char *ca_cert_path) {
    if (!ctx || !ca_cert_path) {
        return -1;
    }

    /* Check if file exists */
    if (access(ca_cert_path, R_OK) != 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "CA certificate file not readable: %s", ca_cert_path);
        return -1;
    }

    /* Load CA certificate */
    if (SSL_CTX_load_verify_locations(ctx->ssl_ctx, ca_cert_path, NULL) != 1) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to load CA certificate: %s", ca_cert_path);
        return -1;
    }

    /* Store path for reference */
    puppet_free(ctx->ca_cert_path);
    ctx->ca_cert_path = puppet_strdup(ca_cert_path);

    return 0;
}

/**
 * Load certificate and private key
 */
int puppet_ssl_ctx_load_cert_and_key(puppet_ssl_ctx_t *ctx,
                                      const char *cert_path,
                                      const char *key_path) {
    if (!ctx || !cert_path || !key_path) {
        return -1;
    }

    /* Check if files exist */
    if (access(cert_path, R_OK) != 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Certificate file not readable: %s", cert_path);
        return -1;
    }

    if (access(key_path, R_OK) != 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Private key file not readable: %s", key_path);
        return -1;
    }

    /* Load certificate */
    if (SSL_CTX_use_certificate_file(ctx->ssl_ctx, cert_path, SSL_FILETYPE_PEM) != 1) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to load certificate: %s", cert_path);
        return -1;
    }

    /* Load private key */
    if (SSL_CTX_use_PrivateKey_file(ctx->ssl_ctx, key_path, SSL_FILETYPE_PEM) != 1) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to load private key: %s", key_path);
        return -1;
    }

    /* Verify that certificate and private key match */
    if (SSL_CTX_check_private_key(ctx->ssl_ctx) != 1) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Certificate and private key do not match");
        return -1;
    }

    /* Store paths for reference */
    puppet_free(ctx->cert_path);
    puppet_free(ctx->key_path);
    ctx->cert_path = puppet_strdup(cert_path);
    ctx->key_path = puppet_strdup(key_path);

    return 0;
}

/**
 * Set peer verification mode
 */
void puppet_ssl_ctx_set_verify(puppet_ssl_ctx_t *ctx, bool verify) {
    if (!ctx) {
        return;
    }

    ctx->verify_peer = verify;

    if (verify) {
        SSL_CTX_set_verify(ctx->ssl_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
    } else {
        SSL_CTX_set_verify(ctx->ssl_ctx, SSL_VERIFY_NONE, NULL);
    }
}

/**
 * Set cipher suite list
 */
int puppet_ssl_ctx_set_cipher_list(puppet_ssl_ctx_t *ctx, const char *cipher_list) {
    if (!ctx || !cipher_list) {
        return -1;
    }

    if (SSL_CTX_set_cipher_list(ctx->ssl_ctx, cipher_list) != 1) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to set cipher list: %s", cipher_list);
        return -1;
    }

    return 0;
}

/*
 * ===========================================================================
 * SSL CONNECTION MANAGEMENT
 * ===========================================================================
 */

/**
 * Create a new SSL connection
 */
puppet_ssl_conn_t *puppet_ssl_conn_new(puppet_ssl_ctx_t *ctx,
                                        int socket_fd,
                                        bool is_server) {
    if (!ctx || socket_fd < 0) {
        return NULL;
    }

    puppet_ssl_conn_t *conn = puppet_malloc(sizeof(puppet_ssl_conn_t));
    if (!conn) {
        return NULL;
    }

    memset(conn, 0, sizeof(puppet_ssl_conn_t));
    conn->socket_fd = socket_fd;
    conn->is_server = is_server;

    /* Create SSL connection */
    conn->ssl = SSL_new(ctx->ssl_ctx);
    if (!conn->ssl) {
        snprintf(conn->last_error, sizeof(conn->last_error),
                "Failed to create SSL connection");
        puppet_free(conn);
        return NULL;
    }

    /* Attach socket to SSL connection */
    if (SSL_set_fd(conn->ssl, socket_fd) != 1) {
        snprintf(conn->last_error, sizeof(conn->last_error),
                "Failed to attach socket to SSL connection");
        SSL_free(conn->ssl);
        puppet_free(conn);
        return NULL;
    }

    return conn;
}

/**
 * Free an SSL connection
 */
void puppet_ssl_conn_free(puppet_ssl_conn_t *conn) {
    if (!conn) {
        return;
    }

    if (conn->ssl) {
        SSL_shutdown(conn->ssl);
        SSL_free(conn->ssl);
    }

    puppet_free(conn);
}

/**
 * Perform SSL handshake
 */
int puppet_ssl_conn_handshake(puppet_ssl_conn_t *conn) {
    if (!conn || !conn->ssl) {
        return -1;
    }

    int ret;
    if (conn->is_server) {
        ret = SSL_accept(conn->ssl);
    } else {
        ret = SSL_connect(conn->ssl);
    }

    if (ret != 1) {
        unsigned long err = ERR_get_error();
        ERR_error_string_n(err, conn->last_error, sizeof(conn->last_error));
        return -1;
    }

    /* A successful handshake only means the crypto succeeded; with
     * SSL_VERIFY_PEER the chain result is reported separately. Enforce it
     * (and, when an expected hostname was set via
     * puppet_ssl_conn_set_verify_hostname, this also covers the CN/SAN
     * match — X509_V_ERR_HOSTNAME_MISMATCH). Without this a client would
     * accept any cert that merely chains to a trusted CA. */
    if (!conn->is_server) {
        long vr = SSL_get_verify_result(conn->ssl);
        if (vr != X509_V_OK) {
            snprintf(conn->last_error, sizeof(conn->last_error),
                    "Peer certificate verification failed: %s",
                    X509_verify_cert_error_string(vr));
            return -1;
        }
    }

    return 0;
}

/**
 * Set the hostname the peer certificate must match (client side).
 *
 * Must be called before puppet_ssl_conn_handshake(). Enables RFC 6125
 * CN/SAN matching inside OpenSSL's verification so that a certificate which
 * merely chains to the CA but was issued for a different identity is
 * rejected. Also sets the SNI server name.
 */
int puppet_ssl_conn_set_verify_hostname(puppet_ssl_conn_t *conn,
                                        const char *hostname) {
    if (!conn || !conn->ssl || !hostname) {
        return -1;
    }

    SSL_set_hostflags(conn->ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    if (SSL_set1_host(conn->ssl, hostname) != 1) {
        snprintf(conn->last_error, sizeof(conn->last_error),
                "Failed to set expected hostname '%s' for verification",
                hostname);
        return -1;
    }

    /* Server Name Indication (best-effort) */
    SSL_set_tlsext_host_name(conn->ssl, hostname);
    return 0;
}

/**
 * Read data from SSL connection
 */
int puppet_ssl_conn_read(puppet_ssl_conn_t *conn, void *buf, size_t len) {
    if (!conn || !conn->ssl || !buf) {
        return -1;
    }

    int ret = SSL_read(conn->ssl, buf, len);
    if (ret < 0) {
        unsigned long err = ERR_get_error();
        ERR_error_string_n(err, conn->last_error, sizeof(conn->last_error));
        return -1;
    }

    return ret;
}

/**
 * Write data to SSL connection
 */
int puppet_ssl_conn_write(puppet_ssl_conn_t *conn, const void *buf, size_t len) {
    if (!conn || !conn->ssl || !buf) {
        return -1;
    }

    int ret = SSL_write(conn->ssl, buf, len);
    if (ret < 0) {
        unsigned long err = ERR_get_error();
        ERR_error_string_n(err, conn->last_error, sizeof(conn->last_error));
        return -1;
    }

    return ret;
}

/*
 * ===========================================================================
 * CERTIFICATE UTILITIES
 * ===========================================================================
 */

/**
 * Load X.509 certificate from file
 */
X509 *puppet_ssl_load_cert(const char *cert_path) {
    if (!cert_path) {
        return NULL;
    }

    FILE *fp = fopen(cert_path, "r");
    if (!fp) {
        snprintf(ssl_error_buffer, sizeof(ssl_error_buffer),
                "Failed to open certificate file: %s", cert_path);
        return NULL;
    }

    X509 *cert = PEM_read_X509(fp, NULL, NULL, NULL);
    fclose(fp);

    if (!cert) {
        snprintf(ssl_error_buffer, sizeof(ssl_error_buffer),
                "Failed to parse certificate: %s", cert_path);
        return NULL;
    }

    return cert;
}

/**
 * Load private key from file
 */
EVP_PKEY *puppet_ssl_load_key(const char *key_path) {
    if (!key_path) {
        return NULL;
    }

    FILE *fp = fopen(key_path, "r");
    if (!fp) {
        snprintf(ssl_error_buffer, sizeof(ssl_error_buffer),
                "Failed to open private key file: %s", key_path);
        return NULL;
    }

    EVP_PKEY *key = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
    fclose(fp);

    if (!key) {
        snprintf(ssl_error_buffer, sizeof(ssl_error_buffer),
                "Failed to parse private key: %s", key_path);
        return NULL;
    }

    return key;
}

/**
 * Extract certificate information
 */
puppet_cert_info_t *puppet_ssl_get_cert_info(X509 *cert) {
    if (!cert) {
        return NULL;
    }

    puppet_cert_info_t *info = puppet_malloc(sizeof(puppet_cert_info_t));
    if (!info) {
        return NULL;
    }

    memset(info, 0, sizeof(puppet_cert_info_t));

    /* Get subject */
    char *subject = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
    if (subject) {
        info->subject = puppet_strdup(subject);
        OPENSSL_free(subject);
    }

    /* Get issuer */
    char *issuer = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
    if (issuer) {
        info->issuer = puppet_strdup(issuer);
        OPENSSL_free(issuer);
    }

    /* Get common name (CN) from subject */
    X509_NAME *subject_name = X509_get_subject_name(cert);
    int cn_index = X509_NAME_get_index_by_NID(subject_name, NID_commonName, -1);
    if (cn_index >= 0) {
        X509_NAME_ENTRY *cn_entry = X509_NAME_get_entry(subject_name, cn_index);
        if (cn_entry) {
            ASN1_STRING *cn_asn1 = X509_NAME_ENTRY_get_data(cn_entry);
            if (cn_asn1) {
                unsigned char *cn_str = NULL;
                int cn_len = ASN1_STRING_to_UTF8(&cn_str, cn_asn1);
                if (cn_len > 0 && cn_str) {
                    info->common_name = puppet_strdup((char *)cn_str);
                    OPENSSL_free(cn_str);
                }
            }
        }
    }

    /* Get validity period */
    const ASN1_TIME *not_before = X509_get0_notBefore(cert);
    const ASN1_TIME *not_after = X509_get0_notAfter(cert);

    /* Convert ASN1_TIME to time_t (simplified - assumes generalized time) */
    struct tm tm_before = {0}, tm_after = {0};
    if (not_before) {
        ASN1_TIME_to_tm(not_before, &tm_before);
        info->not_before = mktime(&tm_before);
    }

    if (not_after) {
        ASN1_TIME_to_tm(not_after, &tm_after);
        info->not_after = mktime(&tm_after);
    }

    /* Check if certificate is currently valid */
    time_t now = time(NULL);
    info->is_valid = (now >= info->not_before && now <= info->not_after);

    return info;
}

/**
 * Free certificate information structure
 */
void puppet_ssl_cert_info_free(puppet_cert_info_t *info) {
    if (!info) {
        return;
    }

    puppet_free(info->subject);
    puppet_free(info->issuer);
    puppet_free(info->common_name);
    puppet_free(info);
}

/**
 * Verify certificate against CA
 */
bool puppet_ssl_verify_cert(X509 *cert, X509 *ca_cert) {
    if (!cert || !ca_cert) {
        return false;
    }

    /* Create X509_STORE and add CA certificate */
    X509_STORE *store = X509_STORE_new();
    if (!store) {
        return false;
    }

    if (X509_STORE_add_cert(store, ca_cert) != 1) {
        X509_STORE_free(store);
        return false;
    }

    /* Create store context for verification */
    X509_STORE_CTX *ctx = X509_STORE_CTX_new();
    if (!ctx) {
        X509_STORE_free(store);
        return false;
    }

    if (X509_STORE_CTX_init(ctx, store, cert, NULL) != 1) {
        X509_STORE_CTX_free(ctx);
        X509_STORE_free(store);
        return false;
    }

    /* Perform verification */
    int ret = X509_verify_cert(ctx);

    /* Cleanup */
    X509_STORE_CTX_free(ctx);
    X509_STORE_free(store);

    return (ret == 1);
}

/**
 * Get peer certificate from connection
 */
X509 *puppet_ssl_get_peer_cert(puppet_ssl_conn_t *conn) {
    if (!conn || !conn->ssl) {
        return NULL;
    }

    return SSL_get_peer_certificate(conn->ssl);
}

/*
 * ===========================================================================
 * ERROR HANDLING
 * ===========================================================================
 */

/**
 * Get last SSL error message
 */
const char *puppet_ssl_get_error(void) {
    unsigned long err = ERR_get_error();
    if (err == 0) {
        return ssl_error_buffer;
    }

    ERR_error_string_n(err, ssl_error_buffer, sizeof(ssl_error_buffer));
    return ssl_error_buffer;
}

/**
 * Get last error from SSL context
 */
const char *puppet_ssl_ctx_get_error(puppet_ssl_ctx_t *ctx) {
    if (!ctx) {
        return "Invalid SSL context";
    }

    return ctx->last_error;
}

/**
 * Get last error from SSL connection
 */
const char *puppet_ssl_conn_get_error(puppet_ssl_conn_t *conn) {
    if (!conn) {
        return "Invalid SSL connection";
    }

    return conn->last_error;
}

/**
 * Print OpenSSL error queue
 */
void puppet_ssl_print_errors(void) {
    unsigned long err;
    char err_buf[256];

    while ((err = ERR_get_error()) != 0) {
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        fprintf(stderr, "OpenSSL Error: %s\n", err_buf);
    }
}
