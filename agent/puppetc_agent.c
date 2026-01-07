/**
 * @file puppetc_agent.c
 * @brief Puppet agent client
 *
 * Collects facts using libfacter_c, requests catalog from puppetc-server,
 * and displays/applies the compiled catalog.
 */

#include <stdio.h>
#include <stdlib.h>
#include "puppet_memory.h"
#include "color_output.h"
#include "config_parser.h"
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <unistd.h>
#include <curl/curl.h>

#include "facter.h"
#include "puppet_provider.h"
#include "puppet_agent_ruby.h"
#include "puppet_pluginsync.h"

#define DEFAULT_SERVER "http://localhost:8140"
#define DEFAULT_LIBDIR "/var/lib/puppetc/lib"
#define DEFAULT_ENVIRONMENT "production"
#define DEFAULT_CONFIG_FILE "/etc/puppetc/puppet.conf"
#define AGENT_VERSION "0.1.0"

/* Response buffer for curl */
typedef struct {
    char *data;
    size_t size;
} response_buffer_t;

/* Agent configuration */
typedef struct {
    const char *server_url;
    char *certname;
    const char *environment;
    const char *catalog_file;  /* Local catalog file to apply (for testing) */
    const char *libdir;        /* Plugin library directory */
    bool verbose;
    bool noop;           /* No-op mode - don't apply changes */
    bool apply_catalog;  /* Actually apply resources */
    bool show_facts;     /* Just show facts, don't request catalog */
    bool show_catalog;   /* Show catalog JSON */
    bool use_ruby;       /* Enable Ruby support */
    bool pluginsync;     /* Enable pluginsync */
} agent_config_t;

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    response_buffer_t *buf = (response_buffer_t *)userp;

    char *ptr = puppet_realloc(buf->data, buf->size + realsize + 1);
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
    char *request = puppet_malloc(buf_size);
    if (!request) {
        puppet_free(facts_json);
        return NULL;
    }

    snprintf(request, buf_size,
             "{\n"
             "  \"certname\": \"%s\",\n"
             "  \"environment\": \"%s\",\n"
             "  \"facts\": %s\n"
             "}",
             certname, environment, facts_json);

    puppet_free(facts_json);
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
    puppet_free(response.data);

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
    puppet_free(response.data);

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
    agent_ruby_context_t *ruby_ctx = NULL;

    /* Initialize color output */
    color_init();

    print_info("puppetc-agent v%s", AGENT_VERSION);
    print_info("Server: %s", config->server_url);

    /* Set server URL in environment for providers (e.g., file provider) */
    setenv("PUPPET_SERVER", config->server_url, 1);

    /* Initialize Ruby if enabled */
    if (config->use_ruby) {
        print_info("Initializing Ruby (%s)...", agent_ruby_version());
        ruby_ctx = agent_ruby_init(config->libdir);
        if (!ruby_ctx) {
            print_warning("Ruby initialization failed, continuing without Ruby support");
        } else {
            print_info("Ruby initialized");
            /* Make Ruby available for providers */
            provider_set_ruby_context(ruby_ctx);
        }
    }

    /* Run pluginsync before fact collection */
    if (config->pluginsync && !config->catalog_file) {
        print_info("Running pluginsync...");
        pluginsync_config_t ps_config = {
            .server_url = config->server_url,
            .libdir = config->libdir,
            .environment = config->environment,
            .verbose = config->verbose
        };
        pluginsync_result_t ps_result = {0};
        if (pluginsync_run(&ps_config, &ps_result) == 0) {
            print_info("Pluginsync complete: %d files synced", ps_result.files_downloaded);
        } else {
            print_warning("Pluginsync failed (continuing without plugins)");
        }

        /* Add libdir to Ruby load path if Ruby is active */
        if (ruby_ctx) {
            agent_ruby_add_loadpath(ruby_ctx, config->libdir);
        }
    }

    /* Collect facts */
    print_info("Collecting facts...");

    facter_ctx_t *facts = facter_create();
    if (!facts) {
        print_error("Failed to create facter context");
        return 1;
    }

    if (facter_collect(facts) != 0) {
        print_error("Failed to collect facts");
        facter_destroy(facts);
        if (ruby_ctx) agent_ruby_cleanup(ruby_ctx);
        return 1;
    }

    /* Run custom Ruby facts if available */
    if (ruby_ctx) {
        int custom_facts = agent_ruby_run_custom_facts(ruby_ctx, facts);
        if (custom_facts > 0) {
            print_info("Custom Ruby facts: %d", custom_facts);
        }
    }

    /* Get certname from facts if not specified */
    if (!config->certname) {
        const char *hostname = facter_get_string(facts, "fqdn");
        if (!hostname) {
            hostname = facter_get_string(facts, "hostname");
        }
        if (hostname) {
            config->certname = puppet_strdup(hostname);
        } else {
            config->certname = puppet_strdup("unknown");
        }
    }

    print_info("Certname: %s", config->certname);

    size_t fact_count;
    facter_list(facts, &fact_count);
    print_info("Facts collected: %zu", fact_count);

    /* Show facts mode */
    if (config->show_facts) {
        printf("\n");
        char *facts_text = facter_to_text(facts);
        if (facts_text) {
            printf("%s", facts_text);
            puppet_free(facts_text);
        }
        facter_destroy(facts);
        return 0;
    }

    char *catalog_json = NULL;

    /* Load catalog from local file or request from server */
    if (config->catalog_file) {
        /* Load from local file */
        print_info("Loading catalog from file: %s", config->catalog_file);

        FILE *f = fopen(config->catalog_file, "r");
        if (!f) {
            print_error("Cannot open catalog file: %s", config->catalog_file);
            facter_destroy(facts);
            return 1;
        }

        /* Get file size */
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        catalog_json = puppet_malloc(size + 1);
        if (!catalog_json) {
            print_error("Out of memory reading catalog file");
            fclose(f);
            facter_destroy(facts);
            return 1;
        }

        size_t read_size = fread(catalog_json, 1, size, f);
        catalog_json[read_size] = '\0';
        fclose(f);

        print_info("Catalog loaded (%ld bytes)", size);
    } else {
        /* Check server status */
        print_info("Connecting to server...");
        if (check_server_status(config->server_url, config->verbose) != 0) {
            print_error("Cannot connect to server at %s", config->server_url);
            print_error("Make sure puppetc-server is running");
            facter_destroy(facts);
            return 1;
        }
        print_info("Server is available");

        /* Build catalog request */
        print_info("Requesting catalog...");
        char *request_json = build_catalog_request(config->certname, config->environment, facts);
        if (!request_json) {
            print_error("Failed to build catalog request");
            facter_destroy(facts);
            return 1;
        }

        if (config->verbose) {
            print_debug("Request:\n%s", request_json);
        }

        /* Request catalog from server */
        catalog_json = request_catalog(config->server_url, request_json, config->verbose);
        puppet_free(request_json);

        if (!catalog_json) {
            print_error("Failed to receive catalog from server");
            facter_destroy(facts);
            return 1;
        }

        print_info("Catalog received");
    }

    /* Display catalog */
    if (config->show_catalog) {
        printf("\n=== Catalog JSON ===\n%s\n", catalog_json);
    }

    if (config->verbose) {
        display_catalog_summary(catalog_json);
        list_catalog_resources(catalog_json);
    }

    /* Apply catalog */
    if (config->apply_catalog || config->noop) {
        /* Get OS family from facts */
        const char *os_family_str = facter_get_string(facts, "osfamily");
        os_family_t os_family = os_family_from_string(os_family_str);

        print_info("OS Family: %s", os_family_to_string(os_family));

        if (config->noop) {
            print_info("Running in no-op mode - no changes will be made");
        }

        /* Initialize providers */
        providers_init(os_family);

        /* Create apply context */
        apply_context_t *apply_ctx = apply_context_create(os_family, config->noop, config->verbose);
        if (apply_ctx) {
            /* Apply the catalog */
            int failures = catalog_apply(catalog_json, apply_ctx);

            if (failures > 0) {
                print_warning("%d resource(s) failed to apply", failures);
                result = 1;
            } else {
                result = 0;
            }

            apply_context_free(apply_ctx);
        }

        providers_shutdown();
    } else {
        print_info("Use --apply to apply resources or --noop to simulate");
        result = 0;
    }

    puppet_free(catalog_json);
    facter_destroy(facts);

    /* Cleanup Ruby */
    if (ruby_ctx) {
        provider_set_ruby_context(NULL);  /* Clear provider fallback */
        agent_ruby_cleanup(ruby_ctx);
    }

    return result;
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\nPuppet agent client (v%s)\n", AGENT_VERSION);
    printf("\nOptions:\n");
    printf("  -c, --config FILE     Config file (default: %s)\n", DEFAULT_CONFIG_FILE);
    printf("  -s, --server URL      Server URL (default: %s)\n", DEFAULT_SERVER);
    printf("      --certname NAME   Node certificate name (default: hostname)\n");
    printf("  -e, --environment ENV Environment (default: %s)\n", DEFAULT_ENVIRONMENT);
    printf("  -F, --catalog-file FILE  Apply a local catalog JSON file\n");
    printf("  -L, --libdir PATH     Plugin library directory (default: %s)\n", DEFAULT_LIBDIR);
    printf("  -a, --apply           Apply catalog resources (requires root for some)\n");
    printf("  -n, --noop            No-op mode - simulate but don't apply\n");
    printf("  -f, --facts           Just show collected facts\n");
    printf("  -C, --catalog         Show full catalog JSON\n");
    printf("      --ruby            Enable Ruby support for custom types/facts\n");
    printf("      --no-ruby         Disable Ruby support (default)\n");
    printf("      --pluginsync      Enable pluginsync to fetch modules' lib/ files\n");
    printf("      --no-pluginsync   Disable pluginsync (default)\n");
    printf("  -v, --verbose         Verbose output\n");
    printf("  -h, --help            Show this help\n");
    printf("\nConfig file sections:\n");
    printf("  [main]                Common settings\n");
    printf("  [agent]               Agent-specific settings\n");
    printf("\nExamples:\n");
    printf("  %s                                    # Get catalog, show summary\n", prog);
    printf("  %s -n                                 # Noop mode, show what would change\n", prog);
    printf("  %s -a                                 # Apply catalog resources\n", prog);
    printf("  %s -s http://puppet:8140 -a           # Apply from remote server\n", prog);
    printf("  %s -F catalog.json -a                 # Apply local catalog file\n", prog);
    printf("  %s -f                                 # Just show facts\n", prog);
}

int main(int argc, char *argv[]) {
    const char *config_file = NULL;
    config_file_t *file_config = NULL;

    /* Default configuration */
    agent_config_t config = {
        .server_url = NULL,
        .certname = NULL,
        .environment = NULL,
        .catalog_file = NULL,
        .libdir = DEFAULT_LIBDIR,
        .verbose = false,
        .noop = false,
        .apply_catalog = false,
        .show_facts = false,
        .show_catalog = false,
        .use_ruby = false,
        .pluginsync = false
    };

    /* Long option IDs for options without short form */
    enum {
        OPT_CERTNAME = 256,
        OPT_RUBY,
        OPT_NO_RUBY,
        OPT_PLUGINSYNC,
        OPT_NO_PLUGINSYNC
    };

    static struct option long_options[] = {
        {"config", required_argument, 0, 'c'},
        {"server", required_argument, 0, 's'},
        {"certname", required_argument, 0, OPT_CERTNAME},
        {"environment", required_argument, 0, 'e'},
        {"catalog-file", required_argument, 0, 'F'},
        {"libdir", required_argument, 0, 'L'},
        {"apply", no_argument, 0, 'a'},
        {"noop", no_argument, 0, 'n'},
        {"facts", no_argument, 0, 'f'},
        {"catalog", no_argument, 0, 'C'},
        {"ruby", no_argument, 0, OPT_RUBY},
        {"no-ruby", no_argument, 0, OPT_NO_RUBY},
        {"pluginsync", no_argument, 0, OPT_PLUGINSYNC},
        {"no-pluginsync", no_argument, 0, OPT_NO_PLUGINSYNC},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    /* First pass: find config file option */
    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "c:s:e:F:L:anfCvh", long_options, NULL)) != -1) {
        if (opt == 'c') {
            config_file = optarg;
            break;
        } else if (opt == 'h') {
            print_usage(argv[0]);
            return 0;
        }
    }

    /* Load config file (try specified, then default) */
    if (config_file) {
        file_config = config_parse(config_file);
        if (!file_config) {
            fprintf(stderr, "Error: Cannot read config file: %s\n", config_file);
            return 1;
        }
    } else {
        /* Try default config file (silently ignore if missing) */
        file_config = config_parse(DEFAULT_CONFIG_FILE);
    }

    /* Apply config file values (lowest priority) */
    if (file_config) {
        /* Try [agent] section first, then [main] */
        const char *val;

        val = config_get_string(file_config, "agent", "server", NULL);
        if (!val) val = config_get_string(file_config, "main", "server", NULL);
        if (val) config.server_url = val;

        val = config_get_string(file_config, "agent", "certname", NULL);
        if (!val) val = config_get_string(file_config, "main", "certname", NULL);
        if (val) config.certname = puppet_strdup(val);

        val = config_get_string(file_config, "agent", "environment", NULL);
        if (!val) val = config_get_string(file_config, "main", "environment", NULL);
        if (val) config.environment = val;

        val = config_get_string(file_config, "agent", "libdir", NULL);
        if (!val) val = config_get_string(file_config, "main", "libdir", NULL);
        if (val) config.libdir = val;

        config.noop = config_get_bool(file_config, "agent", "noop", false);
        config.verbose = config_get_bool(file_config, "agent", "verbose", false);
        config.use_ruby = config_get_bool(file_config, "agent", "ruby", false);
        config.pluginsync = config_get_bool(file_config, "agent", "pluginsync", false);
    }

    /* Apply environment variables (medium priority) */
    const char *env_server = getenv("PUPPET_SERVER");
    const char *env_environment = getenv("PUPPET_ENVIRONMENT");
    if (env_server) config.server_url = env_server;
    if (env_environment) config.environment = env_environment;

    /* Apply final defaults if still unset */
    if (!config.server_url) config.server_url = DEFAULT_SERVER;
    if (!config.environment) config.environment = DEFAULT_ENVIRONMENT;

    /* Second pass: command-line options (highest priority) */
    optind = 1;
    while ((opt = getopt_long(argc, argv, "c:s:e:F:L:anfCvh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                /* Already handled */
                break;
            case 's':
                config.server_url = optarg;
                break;
            case OPT_CERTNAME:
                if (config.certname) puppet_free(config.certname);
                config.certname = puppet_strdup(optarg);
                break;
            case 'e':
                config.environment = optarg;
                break;
            case 'F':
                config.catalog_file = optarg;
                break;
            case 'L':
                config.libdir = optarg;
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
            case OPT_RUBY:
                config.use_ruby = true;
                break;
            case OPT_NO_RUBY:
                config.use_ruby = false;
                break;
            case OPT_PLUGINSYNC:
                config.pluginsync = true;
                break;
            case OPT_NO_PLUGINSYNC:
                config.pluginsync = false;
                break;
            case 'v':
                config.verbose = true;
                break;
            case 'h':
                print_usage(argv[0]);
                config_free(file_config);
                return 0;
            default:
                print_usage(argv[0]);
                config_free(file_config);
                return 1;
        }
    }

    /* Initialize curl */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    int result = run_agent(&config);

    /* Cleanup */
    curl_global_cleanup();
    config_free(file_config);
    if (config.certname) {
        puppet_free(config.certname);
    }

    return result;
}
