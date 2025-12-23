/**
 * @file puppetc_agent.c
 * @brief Puppet agent client
 *
 * Collects facts using libfacter_c, requests catalog from puppetc-server,
 * and displays/applies the compiled catalog.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <unistd.h>
#include <curl/curl.h>

#include "facter.h"
#include "puppet_provider.h"

#define DEFAULT_SERVER "http://localhost:8140"
#define DEFAULT_ENVIRONMENT "production"
#define AGENT_VERSION "0.2.0"

/* Response buffer for curl */
typedef struct {
    char *data;
    size_t size;
} response_buffer_t;

/* Agent configuration */
typedef struct {
    char *server_url;
    char *certname;
    char *environment;
    bool verbose;
    bool noop;           /* No-op mode - don't apply changes */
    bool apply_catalog;  /* Actually apply resources */
    bool show_facts;     /* Just show facts, don't request catalog */
    bool show_catalog;   /* Show catalog JSON */
} agent_config_t;

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    response_buffer_t *buf = (response_buffer_t *)userp;

    char *ptr = realloc(buf->data, buf->size + realsize + 1);
    if (!ptr) {
        fprintf(stderr, "Error: Out of memory\n");
        return 0;
    }

    buf->data = ptr;
    memcpy(&(buf->data[buf->size]), contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = '\0';

    return realsize;
}

static char *build_catalog_request(const char *certname, const char *environment,
                                   facter_ctx_t *facts) {
    /* Get facts as JSON */
    char *facts_json = facter_to_json(facts);
    if (!facts_json) {
        return NULL;
    }

    /* Build request JSON
     * Format: {"certname": "...", "environment": "...", "facts": {...}}
     */
    size_t buf_size = strlen(facts_json) + strlen(certname) + strlen(environment) + 256;
    char *request = malloc(buf_size);
    if (!request) {
        free(facts_json);
        return NULL;
    }

    snprintf(request, buf_size,
             "{\n"
             "  \"certname\": \"%s\",\n"
             "  \"environment\": \"%s\",\n"
             "  \"facts\": %s\n"
             "}",
             certname, environment, facts_json);

    free(facts_json);
    return request;
}

static char *request_catalog(const char *server_url, const char *request_json,
                            bool verbose) {
    CURL *curl;
    CURLcode res;
    response_buffer_t response = {0};
    char *result = NULL;

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Error: Failed to initialize curl\n");
        return NULL;
    }

    /* Build full URL */
    char url[512];
    snprintf(url, sizeof(url), "%s/puppet/v4/catalog", server_url);

    /* Set up request */
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_json);

    /* Headers */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    /* Response handling */
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    /* Timeout */
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    if (verbose) {
        fprintf(stderr, "[INFO] Requesting catalog from %s\n", url);
    }

    /* Perform request */
    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "Error: Failed to request catalog: %s\n",
                curl_easy_strerror(res));
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (verbose) {
            fprintf(stderr, "[INFO] Server response: HTTP %ld\n", http_code);
        }

        if (http_code == 200) {
            result = response.data;
            response.data = NULL;  /* Transfer ownership */
        } else {
            fprintf(stderr, "Error: Server returned HTTP %ld\n", http_code);
            if (response.data) {
                fprintf(stderr, "Response: %s\n", response.data);
            }
        }
    }

    /* Cleanup */
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(response.data);

    return result;
}

static int check_server_status(const char *server_url, bool verbose) {
    CURL *curl;
    CURLcode res;
    response_buffer_t response = {0};
    int result = -1;

    curl = curl_easy_init();
    if (!curl) {
        return -1;
    }

    char url[512];
    snprintf(url, sizeof(url), "%s/status", server_url);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);

    if (verbose) {
        fprintf(stderr, "[INFO] Checking server status at %s\n", url);
    }

    res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code == 200) {
            result = 0;
            if (verbose) {
                fprintf(stderr, "[INFO] Server status: %s\n", response.data);
            }
        }
    }

    curl_easy_cleanup(curl);
    free(response.data);

    return result;
}

/* ============================================================================
 * Catalog Parsing and Display
 * ============================================================================ */

static void display_catalog_summary(const char *catalog_json) {
    /* Simple parsing to extract key info */
    printf("\n=== Catalog Summary ===\n");

    /* Count resources (look for "type": patterns) */
    int resource_count = 0;
    const char *p = catalog_json;
    while ((p = strstr(p, "\"type\":")) != NULL) {
        resource_count++;
        p++;
    }

    /* Extract certname */
    const char *certname_start = strstr(catalog_json, "\"certname\":");
    if (certname_start) {
        certname_start = strchr(certname_start, ':') + 1;
        while (*certname_start == ' ' || *certname_start == '"') certname_start++;
        const char *certname_end = strchr(certname_start, '"');
        if (certname_end) {
            printf("Node: %.*s\n", (int)(certname_end - certname_start), certname_start);
        }
    }

    /* Extract environment */
    const char *env_start = strstr(catalog_json, "\"environment\":");
    if (env_start) {
        env_start = strchr(env_start, ':') + 1;
        while (*env_start == ' ' || *env_start == '"') env_start++;
        const char *env_end = strchr(env_start, '"');
        if (env_end) {
            printf("Environment: %.*s\n", (int)(env_end - env_start), env_start);
        }
    }

    printf("Resources: %d\n", resource_count > 0 ? resource_count : 0);

    /* Check for errors */
    if (strstr(catalog_json, "\"error\":")) {
        const char *error_start = strstr(catalog_json, "\"error\":");
        if (error_start) {
            error_start = strchr(error_start, ':') + 1;
            while (*error_start == ' ' || *error_start == '"') error_start++;
            const char *error_end = strchr(error_start, '"');
            if (error_end) {
                printf("\n*** ERROR: %.*s ***\n", (int)(error_end - error_start), error_start);
            }
        }
    }

    printf("=======================\n\n");
}

static void list_catalog_resources(const char *catalog_json) {
    printf("\n=== Resources ===\n");

    /* Very simple resource listing - finds "type" and "title" pairs */
    const char *p = catalog_json;
    int count = 0;

    while ((p = strstr(p, "\"type\":")) != NULL) {
        /* Extract type */
        const char *type_start = p + 8;  /* Skip "type": " */
        while (*type_start == ' ' || *type_start == '"') type_start++;
        const char *type_end = strchr(type_start, '"');

        /* Find title */
        const char *title_marker = strstr(p, "\"title\":");
        if (title_marker && title_marker < p + 200) {
            const char *title_start = title_marker + 9;
            while (*title_start == ' ' || *title_start == '"') title_start++;
            const char *title_end = strchr(title_start, '"');

            if (type_end && title_end) {
                printf("  %.*s[%.*s]\n",
                       (int)(type_end - type_start), type_start,
                       (int)(title_end - title_start), title_start);
                count++;
            }
        }
        p++;
    }

    if (count == 0) {
        printf("  (no resources)\n");
    }

    printf("=================\n\n");
}

/* ============================================================================
 * Main Agent Logic
 * ============================================================================ */

static int run_agent(agent_config_t *config) {
    int result = 1;

    printf("puppetc-agent v%s\n", AGENT_VERSION);
    printf("Server: %s\n", config->server_url);

    /* Collect facts */
    printf("\nCollecting facts...\n");

    facter_ctx_t *facts = facter_create();
    if (!facts) {
        fprintf(stderr, "Error: Failed to create facter context\n");
        return 1;
    }

    if (facter_collect(facts) != 0) {
        fprintf(stderr, "Error: Failed to collect facts\n");
        facter_destroy(facts);
        return 1;
    }

    /* Get certname from facts if not specified */
    if (!config->certname) {
        const char *hostname = facter_get_string(facts, "fqdn");
        if (!hostname) {
            hostname = facter_get_string(facts, "hostname");
        }
        if (hostname) {
            config->certname = strdup(hostname);
        } else {
            config->certname = strdup("unknown");
        }
    }

    printf("Certname: %s\n", config->certname);

    size_t fact_count;
    facter_list(facts, &fact_count);
    printf("Facts collected: %zu\n", fact_count);

    /* Show facts mode */
    if (config->show_facts) {
        printf("\n");
        char *facts_text = facter_to_text(facts);
        if (facts_text) {
            printf("%s", facts_text);
            free(facts_text);
        }
        facter_destroy(facts);
        return 0;
    }

    /* Check server status */
    printf("\nConnecting to server...\n");
    if (check_server_status(config->server_url, config->verbose) != 0) {
        fprintf(stderr, "Error: Cannot connect to server at %s\n", config->server_url);
        fprintf(stderr, "Make sure puppetc-server is running.\n");
        facter_destroy(facts);
        return 1;
    }
    printf("Server is available.\n");

    /* Build catalog request */
    printf("\nRequesting catalog...\n");
    char *request_json = build_catalog_request(config->certname, config->environment, facts);
    if (!request_json) {
        fprintf(stderr, "Error: Failed to build catalog request\n");
        facter_destroy(facts);
        return 1;
    }

    if (config->verbose) {
        fprintf(stderr, "[DEBUG] Request:\n%s\n", request_json);
    }

    /* Request catalog from server */
    char *catalog_json = request_catalog(config->server_url, request_json, config->verbose);
    free(request_json);

    if (!catalog_json) {
        fprintf(stderr, "Error: Failed to receive catalog from server\n");
        facter_destroy(facts);
        return 1;
    }

    printf("Catalog received.\n");

    /* Display catalog */
    if (config->show_catalog) {
        printf("\n=== Catalog JSON ===\n%s\n", catalog_json);
    }

    display_catalog_summary(catalog_json);
    list_catalog_resources(catalog_json);

    /* Apply catalog */
    if (config->apply_catalog || config->noop) {
        /* Get OS family from facts */
        const char *os_family_str = facter_get_string(facts, "osfamily");
        os_family_t os_family = os_family_from_string(os_family_str);

        printf("OS Family: %s\n", os_family_to_string(os_family));

        if (config->noop) {
            printf("Running in no-op mode - no changes will be made.\n");
        }

        /* Initialize providers */
        providers_init(os_family);

        /* Create apply context */
        apply_context_t *apply_ctx = apply_context_create(os_family, config->noop, config->verbose);
        if (apply_ctx) {
            /* Apply the catalog */
            int failures = catalog_apply(catalog_json, apply_ctx);

            if (failures > 0) {
                fprintf(stderr, "Warning: %d resource(s) failed to apply\n", failures);
                result = 1;
            } else {
                result = 0;
            }

            apply_context_free(apply_ctx);
        }

        providers_shutdown();
    } else {
        printf("\nNote: Use --apply to apply resources or --noop to simulate.\n");
        result = 0;
    }

    free(catalog_json);
    facter_destroy(facts);

    return result;
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\nPuppet agent client (v%s)\n", AGENT_VERSION);
    printf("\nOptions:\n");
    printf("  -s, --server URL      Server URL (default: %s)\n", DEFAULT_SERVER);
    printf("  -c, --certname NAME   Node certificate name (default: hostname)\n");
    printf("  -e, --environment ENV Environment (default: %s)\n", DEFAULT_ENVIRONMENT);
    printf("  -a, --apply           Apply catalog resources (requires root for some)\n");
    printf("  -n, --noop            No-op mode - simulate but don't apply\n");
    printf("  -f, --facts           Just show collected facts\n");
    printf("  -C, --catalog         Show full catalog JSON\n");
    printf("  -v, --verbose         Verbose output\n");
    printf("  -h, --help            Show this help\n");
    printf("\nExamples:\n");
    printf("  %s                                    # Get catalog, show summary\n", prog);
    printf("  %s -n                                 # Noop mode, show what would change\n", prog);
    printf("  %s -a                                 # Apply catalog resources\n", prog);
    printf("  %s -s http://puppet:8140 -a           # Apply from remote server\n", prog);
    printf("  %s -f                                 # Just show facts\n", prog);
}

int main(int argc, char *argv[]) {
    agent_config_t config = {
        .server_url = DEFAULT_SERVER,
        .certname = NULL,
        .environment = DEFAULT_ENVIRONMENT,
        .verbose = false,
        .noop = false,
        .apply_catalog = false,
        .show_facts = false,
        .show_catalog = false
    };

    static struct option long_options[] = {
        {"server", required_argument, 0, 's'},
        {"certname", required_argument, 0, 'c'},
        {"environment", required_argument, 0, 'e'},
        {"apply", no_argument, 0, 'a'},
        {"noop", no_argument, 0, 'n'},
        {"facts", no_argument, 0, 'f'},
        {"catalog", no_argument, 0, 'C'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "s:c:e:anfCvh", long_options, NULL)) != -1) {
        switch (opt) {
            case 's':
                config.server_url = optarg;
                break;
            case 'c':
                config.certname = strdup(optarg);
                break;
            case 'e':
                config.environment = optarg;
                break;
            case 'a':
                config.apply_catalog = true;
                break;
            case 'n':
                config.noop = true;
                break;
            case 'f':
                config.show_facts = true;
                break;
            case 'C':
                config.show_catalog = true;
                break;
            case 'v':
                config.verbose = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    /* Initialize curl */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    int result = run_agent(&config);

    /* Cleanup */
    curl_global_cleanup();
    if (config.certname) {
        free(config.certname);
    }

    return result;
}
