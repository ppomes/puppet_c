/*
 * Puppet Certificate Authority
 *
 * This module implements Certificate Authority functionality for the Puppet server,
 * including CA certificate generation, CSR signing, and certificate management.
 *
 * Key features:
 * - CA certificate and private key generation
 * - Certificate signing request (CSR) handling
 * - X.509 certificate signing with serial number management
 * - Signed certificate storage and retrieval
 * - Auto-signing policy support (policy-based, whitelist, naive)
 * - OpenSSL EVP API for OpenSSL 3.0+ compatibility
 */

#include "puppet_ca.h"
#include "puppet_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

/* Thread-local error message buffer */
static __thread char ca_error_buffer[PUPPET_CA_MAX_ERROR];

/*
 * ===========================================================================
 * UTILITY FUNCTIONS
 * ===========================================================================
 */

/**
 * Create directory with proper permissions if it doesn't exist
 */
static int ensure_directory(const char *path, mode_t mode) {
    struct stat st;

    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        } else {
            errno = ENOTDIR;
            return -1;
        }
    }

    if (mkdir(path, mode) != 0) {
        return -1;
    }

    return 0;
}

/**
 * Set file permissions
 */
static int set_file_permissions(const char *path, mode_t mode) {
    if (chmod(path, mode) != 0) {
        return -1;
    }
    return 0;
}

/**
 * Build full path by joining directory and filename
 */
static char *build_path(const char *dir, const char *filename) {
    size_t dir_len = strlen(dir);
    size_t file_len = strlen(filename);
    size_t total_len = dir_len + file_len + 2; /* +1 for '/', +1 for '\0' */

    char *path = puppet_malloc(total_len);
    if (!path) {
        return NULL;
    }

    snprintf(path, total_len, "%s/%s", dir, filename);
    return path;
}

/*
 * ===========================================================================
 * CA INITIALIZATION AND CLEANUP
 * ===========================================================================
 */

/**
 * Initialize a Certificate Authority context
 */
puppet_ca_ctx_t *puppet_ca_init(const char *ca_dir) {
    if (!ca_dir) {
        snprintf(ca_error_buffer, sizeof(ca_error_buffer),
                "CA directory path is NULL");
        return NULL;
    }

    puppet_ca_ctx_t *ctx = puppet_malloc(sizeof(puppet_ca_ctx_t));
    if (!ctx) {
        snprintf(ca_error_buffer, sizeof(ca_error_buffer),
                "Failed to allocate CA context");
        return NULL;
    }

    memset(ctx, 0, sizeof(puppet_ca_ctx_t));

    /* Store CA directory path */
    ctx->ca_dir = puppet_strdup(ca_dir);
    if (!ctx->ca_dir) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to allocate CA directory path");
        puppet_free(ctx);
        return NULL;
    }

    /* Create CA directory if it doesn't exist */
    if (ensure_directory(ca_dir, 0755) != 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to create CA directory: %s", strerror(errno));
        puppet_free(ctx->ca_dir);
        puppet_free(ctx);
        return NULL;
    }

    /* Create signed certificates directory */
    char *signed_dir = build_path(ca_dir, PUPPET_CA_SIGNED_DIR);
    if (!signed_dir) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to build signed directory path");
        puppet_free(ctx->ca_dir);
        puppet_free(ctx);
        return NULL;
    }

    if (ensure_directory(signed_dir, 0755) != 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to create signed certificates directory: %s", strerror(errno));
        puppet_free(signed_dir);
        puppet_free(ctx->ca_dir);
        puppet_free(ctx);
        return NULL;
    }
    puppet_free(signed_dir);

    /* Initialize serial number to 1 (will be loaded from file if it exists) */
    ctx->serial_number = 1;

    /* Try to load existing CA certificate and key */
    if (puppet_ca_exists(ctx)) {
        if (puppet_ca_load(ctx) != 0) {
            /* Warning but not fatal - CA might need to be generated */
            snprintf(ctx->last_error, sizeof(ctx->last_error),
                    "CA files exist but failed to load");
        }
    }

    /* Load serial number from file if it exists */
    puppet_ca_load_serial(ctx);

    return ctx;
}

/**
 * Free a Certificate Authority context
 */
void puppet_ca_free(puppet_ca_ctx_t *ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->ca_cert) {
        X509_free(ctx->ca_cert);
    }

    if (ctx->ca_key) {
        EVP_PKEY_free(ctx->ca_key);
    }

    puppet_free(ctx->ca_dir);
    puppet_free(ctx);
}

/*
 * ===========================================================================
 * CA GENERATION AND MANAGEMENT
 * ===========================================================================
 */

/**
 * Generate a new Certificate Authority
 */
int puppet_ca_generate(puppet_ca_ctx_t *ctx, const char *subject, int validity_days) {
    if (!ctx || !subject) {
        return -1;
    }

    EVP_PKEY *pkey = NULL;
    X509 *x509 = NULL;
    X509_NAME *name = NULL;
    int ret = -1;

    /* Generate RSA key pair using EVP API (OpenSSL 3.0+ compatible) */
    EVP_PKEY_CTX *pkey_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!pkey_ctx) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to create EVP_PKEY context");
        goto cleanup;
    }

    if (EVP_PKEY_keygen_init(pkey_ctx) <= 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to initialize key generation");
        EVP_PKEY_CTX_free(pkey_ctx);
        goto cleanup;
    }

    if (EVP_PKEY_CTX_set_rsa_keygen_bits(pkey_ctx, PUPPET_CA_KEY_SIZE) <= 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to set RSA key size");
        EVP_PKEY_CTX_free(pkey_ctx);
        goto cleanup;
    }

    if (EVP_PKEY_keygen(pkey_ctx, &pkey) <= 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to generate RSA key pair");
        EVP_PKEY_CTX_free(pkey_ctx);
        goto cleanup;
    }
    EVP_PKEY_CTX_free(pkey_ctx);

    /* Create X.509 certificate */
    x509 = X509_new();
    if (!x509) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to create X509 certificate");
        goto cleanup;
    }

    /* Set version to X509 v3 */
    X509_set_version(x509, 2);

    /* Set serial number */
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);

    /* Set validity period */
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), (long)60 * 60 * 24 * validity_days);

    /* Set public key */
    X509_set_pubkey(x509, pkey);

    /* Set subject name */
    name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                (unsigned char *)subject, -1, -1, 0);

    /* Self-signed: issuer same as subject */
    X509_set_issuer_name(x509, name);

    /* Add CA extension */
    X509_EXTENSION *ext = NULL;
    X509V3_CTX v3ctx;
    X509V3_set_ctx_nodb(&v3ctx);
    X509V3_set_ctx(&v3ctx, x509, x509, NULL, NULL, 0);

    /* Add basic constraints: CA:TRUE */
    ext = X509V3_EXT_conf_nid(NULL, &v3ctx, NID_basic_constraints, "critical,CA:TRUE");
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    /* Add key usage: certificate signing, CRL signing */
    ext = X509V3_EXT_conf_nid(NULL, &v3ctx, NID_key_usage,
                              "critical,keyCertSign,cRLSign");
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    /* Add subject key identifier */
    ext = X509V3_EXT_conf_nid(NULL, &v3ctx, NID_subject_key_identifier, "hash");
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    /* Sign the certificate using EVP API */
    if (!X509_sign(x509, pkey, EVP_sha256())) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to sign CA certificate");
        goto cleanup;
    }

    /* Store in context */
    if (ctx->ca_cert) {
        X509_free(ctx->ca_cert);
    }
    if (ctx->ca_key) {
        EVP_PKEY_free(ctx->ca_key);
    }

    ctx->ca_cert = x509;
    ctx->ca_key = pkey;

    /* Save to disk */
    if (puppet_ca_save(ctx) != 0) {
        goto cleanup;
    }

    /* Initialize serial number */
    ctx->serial_number = 2; /* CA cert used serial 1 */
    if (puppet_ca_save_serial(ctx) != 0) {
        /* Warning but not fatal */
    }

    ret = 0;
    x509 = NULL;
    pkey = NULL;

cleanup:
    if (x509) {
        X509_free(x509);
    }
    if (pkey) {
        EVP_PKEY_free(pkey);
    }

    return ret;
}

/**
 * Load existing CA certificate and private key
 */
int puppet_ca_load(puppet_ca_ctx_t *ctx) {
    if (!ctx) {
        return -1;
    }

    char *cert_path = build_path(ctx->ca_dir, PUPPET_CA_CERT_FILE);
    char *key_path = build_path(ctx->ca_dir, PUPPET_CA_KEY_FILE);

    if (!cert_path || !key_path) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to build CA file paths");
        puppet_free(cert_path);
        puppet_free(key_path);
        return -1;
    }

    /* Load certificate */
    FILE *cert_file = fopen(cert_path, "r");
    if (!cert_file) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to open CA certificate file: %s", strerror(errno));
        puppet_free(cert_path);
        puppet_free(key_path);
        return -1;
    }

    X509 *cert = PEM_read_X509(cert_file, NULL, NULL, NULL);
    fclose(cert_file);

    if (!cert) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to read CA certificate");
        puppet_free(cert_path);
        puppet_free(key_path);
        return -1;
    }

    /* Load private key */
    FILE *key_file = fopen(key_path, "r");
    if (!key_file) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to open CA private key file: %s", strerror(errno));
        X509_free(cert);
        puppet_free(cert_path);
        puppet_free(key_path);
        return -1;
    }

    EVP_PKEY *pkey = PEM_read_PrivateKey(key_file, NULL, NULL, NULL);
    fclose(key_file);

    if (!pkey) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to read CA private key");
        X509_free(cert);
        puppet_free(cert_path);
        puppet_free(key_path);
        return -1;
    }

    /* Store in context */
    if (ctx->ca_cert) {
        X509_free(ctx->ca_cert);
    }
    if (ctx->ca_key) {
        EVP_PKEY_free(ctx->ca_key);
    }

    ctx->ca_cert = cert;
    ctx->ca_key = pkey;

    puppet_free(cert_path);
    puppet_free(key_path);

    return 0;
}

/**
 * Save CA certificate and private key to disk
 */
int puppet_ca_save(puppet_ca_ctx_t *ctx) {
    if (!ctx || !ctx->ca_cert || !ctx->ca_key) {
        return -1;
    }

    char *cert_path = build_path(ctx->ca_dir, PUPPET_CA_CERT_FILE);
    char *key_path = build_path(ctx->ca_dir, PUPPET_CA_KEY_FILE);

    if (!cert_path || !key_path) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to build CA file paths");
        puppet_free(cert_path);
        puppet_free(key_path);
        return -1;
    }

    /* Save certificate */
    FILE *cert_file = fopen(cert_path, "w");
    if (!cert_file) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to create CA certificate file: %s", strerror(errno));
        puppet_free(cert_path);
        puppet_free(key_path);
        return -1;
    }

    if (!PEM_write_X509(cert_file, ctx->ca_cert)) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to write CA certificate");
        fclose(cert_file);
        puppet_free(cert_path);
        puppet_free(key_path);
        return -1;
    }
    fclose(cert_file);

    /* Set certificate permissions (readable by all) */
    set_file_permissions(cert_path, 0644);

    /* Save private key */
    FILE *key_file = fopen(key_path, "w");
    if (!key_file) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to create CA private key file: %s", strerror(errno));
        puppet_free(cert_path);
        puppet_free(key_path);
        return -1;
    }

    if (!PEM_write_PrivateKey(key_file, ctx->ca_key, NULL, NULL, 0, NULL, NULL)) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to write CA private key");
        fclose(key_file);
        puppet_free(cert_path);
        puppet_free(key_path);
        return -1;
    }
    fclose(key_file);

    /* Set private key permissions (owner read/write only) */
    set_file_permissions(key_path, 0600);

    puppet_free(cert_path);
    puppet_free(key_path);

    return 0;
}

/**
 * Check if CA certificate and key exist
 */
bool puppet_ca_exists(puppet_ca_ctx_t *ctx) {
    if (!ctx) {
        return false;
    }

    char *cert_path = build_path(ctx->ca_dir, PUPPET_CA_CERT_FILE);
    char *key_path = build_path(ctx->ca_dir, PUPPET_CA_KEY_FILE);

    if (!cert_path || !key_path) {
        puppet_free(cert_path);
        puppet_free(key_path);
        return false;
    }

    bool cert_exists = (access(cert_path, R_OK) == 0);
    bool key_exists = (access(key_path, R_OK) == 0);

    puppet_free(cert_path);
    puppet_free(key_path);

    return cert_exists && key_exists;
}

/*
 * ===========================================================================
 * CERTIFICATE SIGNING
 * ===========================================================================
 */

/**
 * Sign a certificate signing request
 */
int puppet_ca_sign_csr(puppet_ca_ctx_t *ctx,
                       const char *csr_pem,
                       const char *certname,
                       int validity_days,
                       char **signed_cert_pem) {
    if (!ctx || !csr_pem || !certname || !signed_cert_pem) {
        return -1;
    }

    if (!ctx->ca_cert || !ctx->ca_key) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "CA not initialized (no certificate or key)");
        return -1;
    }

    /* Parse CSR */
    BIO *bio = BIO_new_mem_buf(csr_pem, -1);
    if (!bio) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to create BIO for CSR");
        return -1;
    }

    X509_REQ *req = PEM_read_bio_X509_REQ(bio, NULL, NULL, NULL);
    BIO_free(bio);

    if (!req) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to parse CSR");
        return -1;
    }

    /* Verify CSR signature */
    EVP_PKEY *req_pkey = X509_REQ_get_pubkey(req);
    if (!req_pkey) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to extract public key from CSR");
        X509_REQ_free(req);
        return -1;
    }

    int verify_result = X509_REQ_verify(req, req_pkey);
    if (verify_result <= 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "CSR signature verification failed");
        EVP_PKEY_free(req_pkey);
        X509_REQ_free(req);
        return -1;
    }

    /* Create new certificate */
    X509 *x509 = X509_new();
    if (!x509) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to create X509 certificate");
        EVP_PKEY_free(req_pkey);
        X509_REQ_free(req);
        return -1;
    }

    /* Set version to X509 v3 */
    X509_set_version(x509, 2);

    /* Set serial number */
    long serial = puppet_ca_get_next_serial(ctx);
    if (serial < 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to get next serial number");
        X509_free(x509);
        EVP_PKEY_free(req_pkey);
        X509_REQ_free(req);
        return -1;
    }
    ASN1_INTEGER_set(X509_get_serialNumber(x509), serial);

    /* Set validity period */
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), (long)60 * 60 * 24 * validity_days);

    /* Set public key from CSR */
    X509_set_pubkey(x509, req_pkey);

    /* Set subject name from CSR */
    X509_NAME *req_name = X509_REQ_get_subject_name(req);
    X509_set_subject_name(x509, req_name);

    /* Set issuer name (CA's subject) */
    X509_set_issuer_name(x509, X509_get_subject_name(ctx->ca_cert));

    /* Add extensions */
    X509V3_CTX v3ctx;
    X509V3_set_ctx(&v3ctx, ctx->ca_cert, x509, req, NULL, 0);

    /* Add subject key identifier */
    X509_EXTENSION *ext = X509V3_EXT_conf_nid(NULL, &v3ctx,
                                               NID_subject_key_identifier, "hash");
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    /* Add authority key identifier */
    ext = X509V3_EXT_conf_nid(NULL, &v3ctx, NID_authority_key_identifier,
                              "keyid:always");
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    /* Add basic constraints: not a CA */
    ext = X509V3_EXT_conf_nid(NULL, &v3ctx, NID_basic_constraints, "CA:FALSE");
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    /* Add key usage */
    ext = X509V3_EXT_conf_nid(NULL, &v3ctx, NID_key_usage,
                              "critical,digitalSignature,keyEncipherment");
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    /* Add extended key usage */
    ext = X509V3_EXT_conf_nid(NULL, &v3ctx, NID_ext_key_usage,
                              "clientAuth,serverAuth");
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    /* Sign the certificate */
    if (!X509_sign(x509, ctx->ca_key, EVP_sha256())) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to sign certificate");
        X509_free(x509);
        EVP_PKEY_free(req_pkey);
        X509_REQ_free(req);
        return -1;
    }

    /* Convert certificate to PEM */
    BIO *cert_bio = BIO_new(BIO_s_mem());
    if (!cert_bio) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to create BIO for certificate");
        X509_free(x509);
        EVP_PKEY_free(req_pkey);
        X509_REQ_free(req);
        return -1;
    }

    if (!PEM_write_bio_X509(cert_bio, x509)) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to write certificate to PEM");
        BIO_free(cert_bio);
        X509_free(x509);
        EVP_PKEY_free(req_pkey);
        X509_REQ_free(req);
        return -1;
    }

    /* Extract PEM data */
    BUF_MEM *cert_buf;
    BIO_get_mem_ptr(cert_bio, &cert_buf);

    *signed_cert_pem = puppet_malloc(cert_buf->length + 1);
    if (!*signed_cert_pem) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to allocate memory for signed certificate");
        BIO_free(cert_bio);
        X509_free(x509);
        EVP_PKEY_free(req_pkey);
        X509_REQ_free(req);
        return -1;
    }

    memcpy(*signed_cert_pem, cert_buf->data, cert_buf->length);
    (*signed_cert_pem)[cert_buf->length] = '\0';

    /* Save signed certificate to disk */
    puppet_ca_save_signed_cert(ctx, certname, *signed_cert_pem);

    /* Cleanup */
    BIO_free(cert_bio);
    X509_free(x509);
    EVP_PKEY_free(req_pkey);
    X509_REQ_free(req);

    return 0;
}

/**
 * Parse CSR and extract information
 */
int puppet_ca_parse_csr(const char *csr_pem, puppet_csr_info_t **info) {
    if (!csr_pem || !info) {
        return -1;
    }

    /* Parse CSR */
    BIO *bio = BIO_new_mem_buf(csr_pem, -1);
    if (!bio) {
        return -1;
    }

    X509_REQ *req = PEM_read_bio_X509_REQ(bio, NULL, NULL, NULL);
    BIO_free(bio);

    if (!req) {
        return -1;
    }

    /* Allocate info structure */
    puppet_csr_info_t *csr_info = puppet_malloc(sizeof(puppet_csr_info_t));
    if (!csr_info) {
        X509_REQ_free(req);
        return -1;
    }
    memset(csr_info, 0, sizeof(puppet_csr_info_t));

    /* Extract subject */
    X509_NAME *subject = X509_REQ_get_subject_name(req);
    if (subject) {
        BIO *subject_bio = BIO_new(BIO_s_mem());
        if (subject_bio) {
            X509_NAME_print_ex(subject_bio, subject, 0, XN_FLAG_ONELINE);
            BUF_MEM *subject_buf;
            BIO_get_mem_ptr(subject_bio, &subject_buf);
            csr_info->subject = puppet_malloc(subject_buf->length + 1);
            if (csr_info->subject) {
                memcpy(csr_info->subject, subject_buf->data, subject_buf->length);
                csr_info->subject[subject_buf->length] = '\0';
            }
            BIO_free(subject_bio);
        }

        /* Extract CN (certname) */
        char cn_buf[PUPPET_CA_MAX_CERTNAME];
        if (X509_NAME_get_text_by_NID(subject, NID_commonName, cn_buf, sizeof(cn_buf)) > 0) {
            csr_info->certname = puppet_strdup(cn_buf);
        }
    }

    /* Extract public key */
    EVP_PKEY *pkey = X509_REQ_get_pubkey(req);
    if (pkey) {
        BIO *pkey_bio = BIO_new(BIO_s_mem());
        if (pkey_bio) {
            PEM_write_bio_PUBKEY(pkey_bio, pkey);
            BUF_MEM *pkey_buf;
            BIO_get_mem_ptr(pkey_bio, &pkey_buf);
            csr_info->public_key_pem = puppet_malloc(pkey_buf->length + 1);
            if (csr_info->public_key_pem) {
                memcpy(csr_info->public_key_pem, pkey_buf->data, pkey_buf->length);
                csr_info->public_key_pem[pkey_buf->length] = '\0';
            }
            BIO_free(pkey_bio);
        }
        EVP_PKEY_free(pkey);
    }

    /* Calculate fingerprint using EVP API */
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int md_len;
    EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
    if (md_ctx) {
        if (EVP_DigestInit_ex(md_ctx, EVP_sha256(), NULL) &&
            EVP_DigestUpdate(md_ctx, csr_pem, strlen(csr_pem)) &&
            EVP_DigestFinal_ex(md_ctx, md, &md_len)) {

            /* Convert to hex string */
            char fp_buf[EVP_MAX_MD_SIZE * 2 + 1];
            for (unsigned int i = 0; i < md_len; i++) {
                sprintf(fp_buf + (i * 2), "%02X", md[i]);
            }
            fp_buf[md_len * 2] = '\0';
            csr_info->fingerprint = puppet_strdup(fp_buf);
        }
        EVP_MD_CTX_free(md_ctx);
    }

    /* Set request time */
    csr_info->request_time = time(NULL);

    X509_REQ_free(req);
    *info = csr_info;

    return 0;
}

/* puppet_csr_info_free is implemented in puppet_autosign.c */

/*
 * ===========================================================================
 * CERTIFICATE STORAGE AND RETRIEVAL
 * ===========================================================================
 */

/**
 * Save a signed certificate to disk
 */
int puppet_ca_save_signed_cert(puppet_ca_ctx_t *ctx,
                                const char *certname,
                                const char *cert_pem) {
    if (!ctx || !certname || !cert_pem) {
        return -1;
    }

    char *signed_dir = build_path(ctx->ca_dir, PUPPET_CA_SIGNED_DIR);
    if (!signed_dir) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to build signed directory path");
        return -1;
    }

    /* Build certificate filename */
    char cert_filename[PUPPET_CA_MAX_CERTNAME + 5]; /* +4 for ".pem", +1 for '\0' */
    snprintf(cert_filename, sizeof(cert_filename), "%s.pem", certname);

    char *cert_path = build_path(signed_dir, cert_filename);
    puppet_free(signed_dir);

    if (!cert_path) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to build certificate path");
        return -1;
    }

    /* Write certificate to file */
    FILE *cert_file = fopen(cert_path, "w");
    if (!cert_file) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to create certificate file: %s", strerror(errno));
        puppet_free(cert_path);
        return -1;
    }

    fprintf(cert_file, "%s", cert_pem);
    fclose(cert_file);

    /* Set permissions (readable by all) */
    set_file_permissions(cert_path, 0644);

    puppet_free(cert_path);
    return 0;
}

/**
 * Load a signed certificate from disk
 */
int puppet_ca_load_signed_cert(puppet_ca_ctx_t *ctx,
                                const char *certname,
                                char **cert_pem) {
    if (!ctx || !certname || !cert_pem) {
        return -1;
    }

    char *signed_dir = build_path(ctx->ca_dir, PUPPET_CA_SIGNED_DIR);
    if (!signed_dir) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to build signed directory path");
        return -1;
    }

    /* Build certificate filename */
    char cert_filename[PUPPET_CA_MAX_CERTNAME + 5];
    snprintf(cert_filename, sizeof(cert_filename), "%s.pem", certname);

    char *cert_path = build_path(signed_dir, cert_filename);
    puppet_free(signed_dir);

    if (!cert_path) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to build certificate path");
        return -1;
    }

    /* Check if file exists */
    if (access(cert_path, R_OK) != 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Certificate file not found: %s", cert_path);
        puppet_free(cert_path);
        return -1;
    }

    /* Read certificate file */
    FILE *cert_file = fopen(cert_path, "r");
    if (!cert_file) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to open certificate file: %s", strerror(errno));
        puppet_free(cert_path);
        return -1;
    }

    /* Get file size */
    fseek(cert_file, 0, SEEK_END);
    long file_size = ftell(cert_file);
    fseek(cert_file, 0, SEEK_SET);

    /* Allocate buffer */
    *cert_pem = puppet_malloc(file_size + 1);
    if (!*cert_pem) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to allocate memory for certificate");
        fclose(cert_file);
        puppet_free(cert_path);
        return -1;
    }

    /* Read file */
    size_t read_size = fread(*cert_pem, 1, file_size, cert_file);
    (*cert_pem)[read_size] = '\0';

    fclose(cert_file);
    puppet_free(cert_path);

    return 0;
}

/**
 * Check if a certificate has been signed
 */
bool puppet_ca_cert_exists(puppet_ca_ctx_t *ctx, const char *certname) {
    if (!ctx || !certname) {
        return false;
    }

    char *signed_dir = build_path(ctx->ca_dir, PUPPET_CA_SIGNED_DIR);
    if (!signed_dir) {
        return false;
    }

    /* Build certificate filename */
    char cert_filename[PUPPET_CA_MAX_CERTNAME + 5];
    snprintf(cert_filename, sizeof(cert_filename), "%s.pem", certname);

    char *cert_path = build_path(signed_dir, cert_filename);
    puppet_free(signed_dir);

    if (!cert_path) {
        return false;
    }

    bool exists = (access(cert_path, R_OK) == 0);
    puppet_free(cert_path);

    return exists;
}

/*
 * ===========================================================================
 * SERIAL NUMBER MANAGEMENT
 * ===========================================================================
 */

/**
 * Get the next serial number for certificate signing
 */
long puppet_ca_get_next_serial(puppet_ca_ctx_t *ctx) {
    if (!ctx) {
        return -1;
    }

    long serial = ctx->serial_number;
    ctx->serial_number++;

    /* Save updated serial number */
    puppet_ca_save_serial(ctx);

    return serial;
}

/**
 * Save serial number to disk
 */
int puppet_ca_save_serial(puppet_ca_ctx_t *ctx) {
    if (!ctx) {
        return -1;
    }

    char *serial_path = build_path(ctx->ca_dir, PUPPET_CA_SERIAL_FILE);
    if (!serial_path) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to build serial file path");
        return -1;
    }

    FILE *serial_file = fopen(serial_path, "w");
    if (!serial_file) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to create serial file: %s", strerror(errno));
        puppet_free(serial_path);
        return -1;
    }

    fprintf(serial_file, "%ld\n", ctx->serial_number);
    fclose(serial_file);

    puppet_free(serial_path);
    return 0;
}

/**
 * Load serial number from disk
 */
int puppet_ca_load_serial(puppet_ca_ctx_t *ctx) {
    if (!ctx) {
        return -1;
    }

    char *serial_path = build_path(ctx->ca_dir, PUPPET_CA_SERIAL_FILE);
    if (!serial_path) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to build serial file path");
        return -1;
    }

    /* Check if file exists */
    if (access(serial_path, R_OK) != 0) {
        /* File doesn't exist, initialize to 1 */
        ctx->serial_number = 1;
        puppet_free(serial_path);
        return 0;
    }

    FILE *serial_file = fopen(serial_path, "r");
    if (!serial_file) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to open serial file: %s", strerror(errno));
        puppet_free(serial_path);
        return -1;
    }

    long serial;
    if (fscanf(serial_file, "%ld", &serial) == 1) {
        ctx->serial_number = serial;
    } else {
        ctx->serial_number = 1;
    }

    fclose(serial_file);
    puppet_free(serial_path);

    return 0;
}

/* Auto-signing functions (puppet_autosign_init, puppet_autosign_free,
 * puppet_autosign_should_sign, puppet_autosign_get_error) are implemented
 * in puppet_autosign.c */

/*
 * ===========================================================================
 * SERVER CERTIFICATE GENERATION
 * ===========================================================================
 */

/**
 * Check if server certificate exists
 */
bool puppet_ca_server_cert_exists(puppet_ca_ctx_t *ctx) {
    if (!ctx || !ctx->ca_dir) return false;

    char cert_path[PUPPET_CA_MAX_PATH];
    char key_path[PUPPET_CA_MAX_PATH];

    snprintf(cert_path, sizeof(cert_path), "%s/server_crt.pem", ctx->ca_dir);
    snprintf(key_path, sizeof(key_path), "%s/server_key.pem", ctx->ca_dir);

    struct stat st;
    return (stat(cert_path, &st) == 0 && stat(key_path, &st) == 0);
}

/**
 * Generate a server certificate for TLS
 */
int puppet_ca_generate_server_cert(puppet_ca_ctx_t *ctx,
                                    const char *hostname,
                                    const char **ip_addresses,
                                    int validity_days) {
    if (!ctx || !ctx->ca_cert || !ctx->ca_key || !hostname) {
        if (ctx) {
            snprintf(ctx->last_error, sizeof(ctx->last_error),
                    "Invalid parameters or CA not loaded");
        }
        return -1;
    }

    /* Generate key pair for server */
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *pkey_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!pkey_ctx) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to create key context");
        return -1;
    }

    if (EVP_PKEY_keygen_init(pkey_ctx) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(pkey_ctx, PUPPET_CA_KEY_SIZE) <= 0 ||
        EVP_PKEY_keygen(pkey_ctx, &pkey) <= 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to generate server key pair");
        EVP_PKEY_CTX_free(pkey_ctx);
        return -1;
    }
    EVP_PKEY_CTX_free(pkey_ctx);

    /* Create X509 certificate */
    X509 *x509 = X509_new();
    if (!x509) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to create X509 certificate");
        EVP_PKEY_free(pkey);
        return -1;
    }

    /* Set version to X509 v3 */
    X509_set_version(x509, 2);

    /* Set serial number */
    long serial = puppet_ca_get_next_serial(ctx);
    if (serial < 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to get next serial number");
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return -1;
    }
    ASN1_INTEGER_set(X509_get_serialNumber(x509), serial);

    /* Set validity period */
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), (long)60 * 60 * 24 * validity_days);

    /* Set public key */
    X509_set_pubkey(x509, pkey);

    /* Set subject name */
    X509_NAME *name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                (unsigned char *)hostname, -1, -1, 0);

    /* Set issuer name (CA's subject) */
    X509_set_issuer_name(x509, X509_get_subject_name(ctx->ca_cert));

    /* Add extensions for server certificate */
    X509V3_CTX v3ctx;
    X509V3_set_ctx(&v3ctx, ctx->ca_cert, x509, NULL, NULL, 0);

    /* Basic constraints: not a CA */
    X509_EXTENSION *ext = X509V3_EXT_conf_nid(NULL, &v3ctx,
                                               NID_basic_constraints, "CA:FALSE");
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    /* Key usage for server */
    ext = X509V3_EXT_conf_nid(NULL, &v3ctx, NID_key_usage,
                              "digitalSignature,keyEncipherment");
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    /* Extended key usage: server authentication */
    ext = X509V3_EXT_conf_nid(NULL, &v3ctx, NID_ext_key_usage,
                              "serverAuth");
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    /* Subject alternative name (for hostname and IP addresses) */
    char san[1024];
    int san_offset = snprintf(san, sizeof(san), "DNS:%s,DNS:localhost", hostname);

    /* Add IP addresses if provided */
    if (ip_addresses) {
        for (int i = 0; ip_addresses[i] != NULL && san_offset < (int)sizeof(san) - 50; i++) {
            san_offset += snprintf(san + san_offset, sizeof(san) - san_offset,
                                   ",IP:%s", ip_addresses[i]);
        }
    }
    /* Always add localhost IP */
    snprintf(san + san_offset, sizeof(san) - san_offset, ",IP:127.0.0.1");

    ext = X509V3_EXT_conf_nid(NULL, &v3ctx, NID_subject_alt_name, san);
    if (ext) {
        X509_add_ext(x509, ext, -1);
        X509_EXTENSION_free(ext);
    }

    /* Sign with CA key */
    if (X509_sign(x509, ctx->ca_key, EVP_sha256()) == 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to sign server certificate");
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return -1;
    }

    /* Save certificate and key */
    char cert_path[PUPPET_CA_MAX_PATH];
    char key_path[PUPPET_CA_MAX_PATH];
    snprintf(cert_path, sizeof(cert_path), "%s/server_crt.pem", ctx->ca_dir);
    snprintf(key_path, sizeof(key_path), "%s/server_key.pem", ctx->ca_dir);

    /* Save certificate */
    FILE *cert_file = fopen(cert_path, "w");
    if (!cert_file) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to create server certificate file");
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return -1;
    }
    PEM_write_X509(cert_file, x509);
    fclose(cert_file);

    /* Save private key with restricted permissions */
    int fd = open(key_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to create server key file");
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return -1;
    }
    FILE *key_file = fdopen(fd, "w");
    if (!key_file) {
        close(fd);
        snprintf(ctx->last_error, sizeof(ctx->last_error),
                "Failed to open server key file for writing");
        X509_free(x509);
        EVP_PKEY_free(pkey);
        return -1;
    }
    PEM_write_PrivateKey(key_file, pkey, NULL, NULL, 0, NULL, NULL);
    fclose(key_file);

    X509_free(x509);
    EVP_PKEY_free(pkey);

    return 0;
}

/*
 * ===========================================================================
 * ERROR HANDLING
 * ===========================================================================
 */

/**
 * Get the last error message from a CA context
 */
const char *puppet_ca_get_error(puppet_ca_ctx_t *ctx) {
    if (!ctx) {
        return "Invalid CA context";
    }
    return ctx->last_error;
}
