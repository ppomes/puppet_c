/**
 * @file puppet_autosign.c
 * @brief Certificate auto-signing policy implementation
 */

#include "puppet_autosign.h"
#include "puppet_memory.h"
#include "config_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

/**
 * @brief Write a JSON-escaped string to a file stream
 */
static void fprint_json_escaped(FILE *fp, const char *str) {
    fputc('"', fp);
    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", fp); break;
            case '\\': fputs("\\\\", fp); break;
            case '\n': fputs("\\n", fp); break;
            case '\r': fputs("\\r", fp); break;
            case '\t': fputs("\\t", fp); break;
            default:
                if ((unsigned char)*p < 0x20) {
                    fprintf(fp, "\\u%04x", (unsigned char)*p);
                } else {
                    fputc(*p, fp);
                }
                break;
        }
    }
    fputc('"', fp);
}

/**
 * @brief Set error message in configuration
 */
static void set_error(puppet_autosign_config_t *config, const char *format, ...) {
    if (!config) return;

    va_list args;
    va_start(args, format);
    vsnprintf(config->last_error, PUPPET_AUTOSIGN_MAX_ERROR, format, args);
    va_end(args);
}

/**
 * @brief Trim leading and trailing whitespace in place
 */
static char *trim(char *str) {
    if (!str) return NULL;

    /* Trim leading whitespace */
    while (isspace((unsigned char)*str)) str++;

    if (*str == '\0') return str;

    /* Trim trailing whitespace */
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';

    return str;
}

puppet_autosign_mode_t puppet_autosign_parse_mode(const char *mode_str) {
    if (!mode_str) return PUPPET_AUTOSIGN_NONE;

    if (strcasecmp(mode_str, "policy") == 0) {
        return PUPPET_AUTOSIGN_POLICY;
    } else if (strcasecmp(mode_str, "whitelist") == 0) {
        return PUPPET_AUTOSIGN_WHITELIST;
    } else if (strcasecmp(mode_str, "naive") == 0) {
        return PUPPET_AUTOSIGN_NAIVE;
    } else if (strcasecmp(mode_str, "none") == 0 || strcasecmp(mode_str, "false") == 0) {
        return PUPPET_AUTOSIGN_NONE;
    }

    return PUPPET_AUTOSIGN_NONE;
}

const char *puppet_autosign_mode_to_string(puppet_autosign_mode_t mode) {
    switch (mode) {
        case PUPPET_AUTOSIGN_POLICY:
            return "policy";
        case PUPPET_AUTOSIGN_WHITELIST:
            return "whitelist";
        case PUPPET_AUTOSIGN_NAIVE:
            return "naive";
        case PUPPET_AUTOSIGN_NONE:
        default:
            return "none";
    }
}

puppet_autosign_config_t *puppet_autosign_init(const char *config_path) {
    if (!config_path) {
        fprintf(stderr, "Error: autosign config path is NULL\n");
        return NULL;
    }

    puppet_autosign_config_t *config = puppet_calloc(1, sizeof(puppet_autosign_config_t));
    if (!config) {
        fprintf(stderr, "Error: failed to allocate autosign config\n");
        return NULL;
    }

    /* Default to no auto-signing */
    config->mode = PUPPET_AUTOSIGN_NONE;
    config->policy_path = NULL;
    config->whitelist_path = NULL;
    config->last_error[0] = '\0';

    /* Check if config file exists */
    struct stat st;
    if (stat(config_path, &st) != 0) {
        /* Config file doesn't exist - use default (no auto-signing) */
        snprintf(config->last_error, PUPPET_AUTOSIGN_MAX_ERROR,
                 "Config file %s does not exist, using default (no auto-signing)", config_path);
        return config;
    }

    /* Parse configuration file */
    config_file_t *cfg = config_parse(config_path);
    if (!cfg) {
        set_error(config, "Failed to parse config file: %s", config_path);
        return config;
    }

    /* Get auto-signing mode */
    const char *mode_str = config_get_string(cfg, "autosign", "mode", "none");
    config->mode = puppet_autosign_parse_mode(mode_str);

    /* Get mode-specific configuration */
    switch (config->mode) {
        case PUPPET_AUTOSIGN_POLICY: {
            const char *policy_path = config_get_string(cfg, "autosign", "policy_path", NULL);
            if (!policy_path) {
                set_error(config, "Policy mode requires policy_path setting");
                config->mode = PUPPET_AUTOSIGN_NONE;
            } else {
                config->policy_path = puppet_strdup(policy_path);

                /* Verify policy executable exists and is executable */
                if (stat(config->policy_path, &st) != 0) {
                    set_error(config, "Policy executable does not exist: %s", config->policy_path);
                    config->mode = PUPPET_AUTOSIGN_NONE;
                } else if (!(st.st_mode & S_IXUSR)) {
                    set_error(config, "Policy file is not executable: %s", config->policy_path);
                    config->mode = PUPPET_AUTOSIGN_NONE;
                }
            }
            break;
        }

        case PUPPET_AUTOSIGN_WHITELIST: {
            const char *whitelist_path = config_get_string(cfg, "autosign", "whitelist_path", NULL);
            if (!whitelist_path) {
                set_error(config, "Whitelist mode requires whitelist_path setting");
                config->mode = PUPPET_AUTOSIGN_NONE;
            } else {
                config->whitelist_path = puppet_strdup(whitelist_path);

                /* Verify whitelist file exists and is readable */
                if (stat(config->whitelist_path, &st) != 0) {
                    set_error(config, "Whitelist file does not exist: %s", config->whitelist_path);
                    config->mode = PUPPET_AUTOSIGN_NONE;
                } else if (!(st.st_mode & S_IRUSR)) {
                    set_error(config, "Whitelist file is not readable: %s", config->whitelist_path);
                    config->mode = PUPPET_AUTOSIGN_NONE;
                }
            }
            break;
        }

        case PUPPET_AUTOSIGN_NAIVE:
            /* No additional configuration needed, but log warning */
            fprintf(stderr, "WARNING: Naive auto-signing enabled - this is INSECURE and should only be used for testing!\n");
            break;

        case PUPPET_AUTOSIGN_NONE:
        default:
            /* No auto-signing - no additional configuration needed */
            break;
    }

    config_free(cfg);
    return config;
}

void puppet_autosign_free(puppet_autosign_config_t *config) {
    if (!config) return;

    if (config->policy_path) {
        puppet_free(config->policy_path);
    }
    if (config->whitelist_path) {
        puppet_free(config->whitelist_path);
    }

    puppet_free(config);
}

bool puppet_autosign_execute_policy(puppet_autosign_config_t *config,
                                     puppet_csr_info_t *csr_info) {
    if (!config || !csr_info) {
        if (config) set_error(config, "Invalid arguments to execute_policy");
        return false;
    }

    if (config->mode != PUPPET_AUTOSIGN_POLICY) {
        set_error(config, "Not in policy mode");
        return false;
    }

    if (!config->policy_path) {
        set_error(config, "Policy path not configured");
        return false;
    }

    /* Create pipes for stdin */
    int stdin_pipe[2];
    if (pipe(stdin_pipe) != 0) {
        set_error(config, "Failed to create pipe: %s", strerror(errno));
        return false;
    }

    /* Fork and execute policy */
    pid_t pid = fork();
    if (pid < 0) {
        set_error(config, "Failed to fork: %s", strerror(errno));
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        return false;
    }

    if (pid == 0) {
        /* Child process */
        close(stdin_pipe[1]); /* Close write end */
        dup2(stdin_pipe[0], STDIN_FILENO); /* Redirect stdin */
        close(stdin_pipe[0]);

        /* Execute policy */
        execl(config->policy_path, config->policy_path, (char *)NULL);

        /* If execl returns, there was an error */
        fprintf(stderr, "Failed to execute policy: %s\n", strerror(errno));
        exit(1);
    }

    /* Parent process */
    close(stdin_pipe[0]); /* Close read end */

    /* Write CSR information to policy's stdin in JSON format */
    FILE *fp = fdopen(stdin_pipe[1], "w");
    if (fp) {
        fprintf(fp, "{\n");
        fprintf(fp, "  \"certname\": ");
        fprint_json_escaped(fp, csr_info->certname ? csr_info->certname : "");
        fprintf(fp, ",\n  \"subject\": ");
        fprint_json_escaped(fp, csr_info->subject ? csr_info->subject : "");
        fprintf(fp, ",\n  \"fingerprint\": ");
        fprint_json_escaped(fp, csr_info->fingerprint ? csr_info->fingerprint : "");
        fprintf(fp, "\n}\n");
        fclose(fp); /* This also closes stdin_pipe[1] */
    } else {
        close(stdin_pipe[1]);
    }

    /* Wait for policy to complete */
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        set_error(config, "Failed to wait for policy: %s", strerror(errno));
        return false;
    }

    /* Check exit status */
    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code == 0) {
            return true; /* Policy approved */
        } else {
            set_error(config, "Policy rejected CSR (exit code %d)", exit_code);
            return false;
        }
    } else if (WIFSIGNALED(status)) {
        set_error(config, "Policy terminated by signal %d", WTERMSIG(status));
        return false;
    }

    set_error(config, "Policy exited abnormally");
    return false;
}

bool puppet_autosign_check_whitelist(puppet_autosign_config_t *config,
                                      const char *certname) {
    if (!config || !certname) {
        if (config) set_error(config, "Invalid arguments to check_whitelist");
        return false;
    }

    if (config->mode != PUPPET_AUTOSIGN_WHITELIST) {
        set_error(config, "Not in whitelist mode");
        return false;
    }

    if (!config->whitelist_path) {
        set_error(config, "Whitelist path not configured");
        return false;
    }

    FILE *fp = fopen(config->whitelist_path, "r");
    if (!fp) {
        set_error(config, "Failed to open whitelist file: %s", strerror(errno));
        return false;
    }

    char line[PUPPET_AUTOSIGN_MAX_LINE];
    bool found = false;

    while (fgets(line, sizeof(line), fp)) {
        /* Remove newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (len > 1 && line[len - 2] == '\r') line[len - 2] = '\0';

        char *trimmed = trim(line);

        /* Skip empty lines and comments */
        if (*trimmed == '\0' || *trimmed == '#') {
            continue;
        }

        /* Check for exact match */
        if (strcmp(trimmed, certname) == 0) {
            found = true;
            break;
        }

        /* Check for simple wildcard pattern (e.g., *.example.com) */
        if (trimmed[0] == '*' && trimmed[1] == '.') {
            const char *domain = trimmed + 1; /* Skip '*', keep the leading '.' */
            size_t domain_len = strlen(domain);
            size_t certname_len = strlen(certname);
            if (certname_len > domain_len) {
                /* Check if certname ends with the domain pattern AND
                 * the character before the match is the start of the certname
                 * (i.e., the matched portion is a complete subdomain label).
                 * This prevents *.example.com from matching evil.notexample.com */
                const char *suffix = certname + certname_len - domain_len;
                if (strcmp(suffix, domain) == 0) {
                    /* Verify the match is at a domain boundary:
                     * suffix starts with '.', so certname_len > domain_len
                     * means there's at least one char before the '.', which is valid */
                    found = true;
                    break;
                }
            }
        }
    }

    fclose(fp);

    if (!found) {
        set_error(config, "Certname not found in whitelist: %s", certname);
    }

    return found;
}

bool puppet_autosign_should_sign(puppet_autosign_config_t *config,
                                  puppet_csr_info_t *csr_info) {
    if (!config) {
        return false;
    }

    if (!csr_info || !csr_info->certname) {
        set_error(config, "Invalid CSR information");
        return false;
    }

    switch (config->mode) {
        case PUPPET_AUTOSIGN_NAIVE:
            /* Always sign - insecure, testing only */
            return true;

        case PUPPET_AUTOSIGN_WHITELIST:
            return puppet_autosign_check_whitelist(config, csr_info->certname);

        case PUPPET_AUTOSIGN_POLICY:
            return puppet_autosign_execute_policy(config, csr_info);

        case PUPPET_AUTOSIGN_NONE:
        default:
            /* No auto-signing */
            set_error(config, "Auto-signing disabled");
            return false;
    }
}

puppet_csr_info_t *puppet_csr_info_create(const char *certname,
                                           const char *subject,
                                           const char *public_key_pem,
                                           const char *fingerprint) {
    puppet_csr_info_t *info = puppet_calloc(1, sizeof(puppet_csr_info_t));
    if (!info) {
        return NULL;
    }

    info->certname = certname ? puppet_strdup(certname) : NULL;
    info->subject = subject ? puppet_strdup(subject) : NULL;
    info->public_key_pem = public_key_pem ? puppet_strdup(public_key_pem) : NULL;
    info->fingerprint = fingerprint ? puppet_strdup(fingerprint) : NULL;
    info->request_time = time(NULL);

    return info;
}

void puppet_csr_info_free(puppet_csr_info_t *info) {
    if (!info) return;

    if (info->certname) {
        puppet_free(info->certname);
    }
    if (info->subject) {
        puppet_free(info->subject);
    }
    if (info->public_key_pem) {
        puppet_free(info->public_key_pem);
    }
    if (info->fingerprint) {
        puppet_free(info->fingerprint);
    }

    puppet_free(info);
}

const char *puppet_autosign_get_error(puppet_autosign_config_t *config) {
    if (!config) {
        return "Invalid config";
    }
    return config->last_error;
}
