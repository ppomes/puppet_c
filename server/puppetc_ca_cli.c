/**
 * @file puppetc_ca_cli.c
 * @brief Certificate Authority management CLI for puppetc
 *
 * Provides command-line tools for managing certificates:
 * - List pending CSRs and signed certificates
 * - Sign pending CSRs
 * - Revoke/clean certificates
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <sys/stat.h>

#include "puppet_ca.h"
#include "puppet_memory.h"

#define DEFAULT_CA_DIR "/etc/puppetc/ssl/ca"

static const char *ca_dir = DEFAULT_CA_DIR;
static int verbose = 0;

static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS] <command> [args]\n", prog);
    printf("\n");
    printf("Certificate Authority management for puppetc\n");
    printf("\n");
    printf("Options:\n");
    printf("  -C, --ca-dir <path>   CA directory (default: %s)\n", DEFAULT_CA_DIR);
    printf("  -v, --verbose         Verbose output\n");
    printf("  -h, --help            Show this help message\n");
    printf("\n");
    printf("Commands:\n");
    printf("  list                  List all certificates (pending and signed)\n");
    printf("  list --pending        List only pending CSRs\n");
    printf("  list --signed         List only signed certificates\n");
    printf("  sign <certname>       Sign a pending CSR\n");
    printf("  sign --all            Sign all pending CSRs\n");
    printf("  revoke <certname>     Revoke a signed certificate (delete it)\n");
    printf("  clean <certname>      Remove a pending CSR\n");
    printf("  print <certname>      Print certificate or CSR details\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s list                    # List all certificates\n", prog);
    printf("  %s sign agent1.example.com # Sign a specific CSR\n", prog);
    printf("  %s sign --all              # Sign all pending CSRs\n", prog);
    printf("  %s revoke old-agent        # Revoke a certificate\n", prog);
    printf("\n");
}

static int cmd_list(int argc, char **argv, puppet_ca_ctx_t *ctx) {
    int show_pending = 1;
    int show_signed = 1;

    /* Parse subcommand options */
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--pending") == 0) {
            show_pending = 1;
            show_signed = 0;
        } else if (strcmp(argv[i], "--signed") == 0) {
            show_pending = 0;
            show_signed = 1;
        }
    }

    int pending_count = 0;
    int signed_count = 0;

    if (show_pending) {
        char **pending = puppet_ca_list_pending_csrs(ctx, &pending_count);
        if (pending) {
            if (pending_count > 0) {
                printf("Pending Certificate Requests (%d):\n", pending_count);
                for (int i = 0; i < pending_count; i++) {
                    printf("  + %s\n", pending[i]);
                    puppet_free(pending[i]);
                }
            } else if (show_pending && !show_signed) {
                printf("No pending certificate requests.\n");
            }
            puppet_free(pending);
        }
    }

    if (show_signed) {
        char **signed_certs = puppet_ca_list_signed_certs(ctx, &signed_count);
        if (signed_certs) {
            if (signed_count > 0) {
                if (show_pending && pending_count > 0) printf("\n");
                printf("Signed Certificates (%d):\n", signed_count);
                for (int i = 0; i < signed_count; i++) {
                    printf("  * %s\n", signed_certs[i]);
                    puppet_free(signed_certs[i]);
                }
            } else if (!show_pending && show_signed) {
                printf("No signed certificates.\n");
            }
            puppet_free(signed_certs);
        }
    }

    if (show_pending && show_signed && pending_count == 0 && signed_count == 0) {
        printf("No certificates found.\n");
    }

    return 0;
}

static int cmd_sign(int argc, char **argv, puppet_ca_ctx_t *ctx) {
    if (argc < 1) {
        fprintf(stderr, "Error: Missing certname. Use 'sign <certname>' or 'sign --all'\n");
        return 1;
    }

    int sign_all = (strcmp(argv[0], "--all") == 0);
    int signed_count = 0;
    int error_count = 0;

    if (sign_all) {
        int count = 0;
        char **pending = puppet_ca_list_pending_csrs(ctx, &count);
        if (!pending || count == 0) {
            printf("No pending certificate requests to sign.\n");
            if (pending) puppet_free(pending);
            return 0;
        }

        for (int i = 0; i < count; i++) {
            char *csr_pem = NULL;
            if (puppet_ca_load_csr(ctx, pending[i], &csr_pem) != 0) {
                fprintf(stderr, "Error: Failed to load CSR for %s: %s\n",
                        pending[i], puppet_ca_get_error(ctx));
                error_count++;
                puppet_free(pending[i]);
                continue;
            }

            char *signed_cert = NULL;
            if (puppet_ca_sign_csr(ctx, csr_pem, pending[i],
                                    PUPPET_CA_CERT_VALIDITY_DAYS, &signed_cert) != 0) {
                fprintf(stderr, "Error: Failed to sign CSR for %s: %s\n",
                        pending[i], puppet_ca_get_error(ctx));
                error_count++;
            } else {
                /* Delete the pending CSR */
                puppet_ca_delete_csr(ctx, pending[i]);
                printf("Signed certificate for: %s\n", pending[i]);
                signed_count++;
                puppet_free(signed_cert);
            }

            puppet_free(csr_pem);
            puppet_free(pending[i]);
        }
        puppet_free(pending);

        printf("\nSigned %d certificate(s)", signed_count);
        if (error_count > 0) {
            printf(", %d error(s)", error_count);
        }
        printf(".\n");

        return error_count > 0 ? 1 : 0;
    }

    /* Sign a specific certificate */
    const char *certname = argv[0];

    if (!puppet_ca_csr_exists(ctx, certname)) {
        fprintf(stderr, "Error: No pending CSR found for '%s'\n", certname);
        return 1;
    }

    char *csr_pem = NULL;
    if (puppet_ca_load_csr(ctx, certname, &csr_pem) != 0) {
        fprintf(stderr, "Error: Failed to load CSR: %s\n", puppet_ca_get_error(ctx));
        return 1;
    }

    char *signed_cert = NULL;
    if (puppet_ca_sign_csr(ctx, csr_pem, certname,
                            PUPPET_CA_CERT_VALIDITY_DAYS, &signed_cert) != 0) {
        fprintf(stderr, "Error: Failed to sign CSR: %s\n", puppet_ca_get_error(ctx));
        puppet_free(csr_pem);
        return 1;
    }

    /* Delete the pending CSR */
    puppet_ca_delete_csr(ctx, certname);

    printf("Successfully signed certificate for: %s\n", certname);

    if (verbose) {
        printf("\nCertificate:\n%s\n", signed_cert);
    }

    puppet_free(csr_pem);
    puppet_free(signed_cert);

    return 0;
}

static int cmd_revoke(int argc, char **argv, puppet_ca_ctx_t *ctx) {
    if (argc < 1) {
        fprintf(stderr, "Error: Missing certname. Use 'revoke <certname>'\n");
        return 1;
    }

    const char *certname = argv[0];

    if (!puppet_ca_cert_exists(ctx, certname)) {
        fprintf(stderr, "Error: No signed certificate found for '%s'\n", certname);
        return 1;
    }

    if (puppet_ca_delete_signed_cert(ctx, certname) != 0) {
        fprintf(stderr, "Error: Failed to revoke certificate: %s\n",
                puppet_ca_get_error(ctx));
        return 1;
    }

    printf("Revoked certificate for: %s\n", certname);
    return 0;
}

static int cmd_clean(int argc, char **argv, puppet_ca_ctx_t *ctx) {
    if (argc < 1) {
        fprintf(stderr, "Error: Missing certname. Use 'clean <certname>'\n");
        return 1;
    }

    const char *certname = argv[0];

    if (!puppet_ca_csr_exists(ctx, certname)) {
        fprintf(stderr, "Error: No pending CSR found for '%s'\n", certname);
        return 1;
    }

    if (puppet_ca_delete_csr(ctx, certname) != 0) {
        fprintf(stderr, "Error: Failed to delete CSR: %s\n",
                puppet_ca_get_error(ctx));
        return 1;
    }

    printf("Removed pending CSR for: %s\n", certname);
    return 0;
}

static int cmd_print(int argc, char **argv, puppet_ca_ctx_t *ctx) {
    if (argc < 1) {
        fprintf(stderr, "Error: Missing certname. Use 'print <certname>'\n");
        return 1;
    }

    const char *certname = argv[0];

    /* Try signed certificate first */
    if (puppet_ca_cert_exists(ctx, certname)) {
        char *cert_pem = NULL;
        if (puppet_ca_load_signed_cert(ctx, certname, &cert_pem) == 0) {
            printf("Signed Certificate for: %s\n", certname);
            printf("========================\n");
            printf("%s", cert_pem);
            puppet_free(cert_pem);
            return 0;
        }
    }

    /* Try pending CSR */
    if (puppet_ca_csr_exists(ctx, certname)) {
        char *csr_pem = NULL;
        if (puppet_ca_load_csr(ctx, certname, &csr_pem) == 0) {
            printf("Pending CSR for: %s\n", certname);
            printf("=================\n");
            printf("%s", csr_pem);
            puppet_free(csr_pem);
            return 0;
        }
    }

    fprintf(stderr, "Error: No certificate or CSR found for '%s'\n", certname);
    return 1;
}

int main(int argc, char **argv) {
    static struct option long_options[] = {
        {"ca-dir",  required_argument, 0, 'C'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "C:vh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'C':
                ca_dir = optarg;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: No command specified.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    const char *command = argv[optind];
    int cmd_argc = argc - optind - 1;
    char **cmd_argv = &argv[optind + 1];

    /* Initialize CA context */
    puppet_ca_ctx_t *ctx = puppet_ca_init(ca_dir);
    if (!ctx) {
        fprintf(stderr, "Error: Failed to initialize CA context\n");
        return 1;
    }

    /* Load CA if it exists */
    if (puppet_ca_exists(ctx)) {
        if (puppet_ca_load(ctx) != 0) {
            fprintf(stderr, "Error: Failed to load CA: %s\n", puppet_ca_get_error(ctx));
            puppet_ca_free(ctx);
            return 1;
        }
    } else {
        fprintf(stderr, "Warning: CA not found at %s\n", ca_dir);
        fprintf(stderr, "Run puppetc-server first to initialize the CA.\n");
        puppet_ca_free(ctx);
        return 1;
    }

    int result = 0;

    if (strcmp(command, "list") == 0) {
        result = cmd_list(cmd_argc, cmd_argv, ctx);
    } else if (strcmp(command, "sign") == 0) {
        result = cmd_sign(cmd_argc, cmd_argv, ctx);
    } else if (strcmp(command, "revoke") == 0) {
        result = cmd_revoke(cmd_argc, cmd_argv, ctx);
    } else if (strcmp(command, "clean") == 0) {
        result = cmd_clean(cmd_argc, cmd_argv, ctx);
    } else if (strcmp(command, "print") == 0) {
        result = cmd_print(cmd_argc, cmd_argv, ctx);
    } else {
        fprintf(stderr, "Error: Unknown command '%s'\n\n", command);
        print_usage(argv[0]);
        result = 1;
    }

    puppet_ca_free(ctx);
    return result;
}
