/**
 * @file puppetc_server.c
 * @brief HTTP server for Puppet catalog compilation
 *
 * Provides a REST API for compiling Puppet catalogs:
 * - GET  /status           - Health check
 * - POST /puppet/v4/catalog - Compile catalog for a node
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <microhttpd.h>

#include "puppet_ast.h"
#include "puppet_catalog.h"
#include "puppet_interpreter.h"
#include "puppet_loader.h"
#include "puppet_memory.h"
#include "puppet_json_parser.h"
#include "puppet_hiera.h"
#include "puppet.tab.h"

/* Default configuration */
#define DEFAULT_PORT 8140
#define DEFAULT_ENVIRONMENT "production"

/* Global state */
static volatile int running = 1;
static char *manifest_path = NULL;
static char *modules_path = NULL;
static char *hiera_datadir = NULL;
static int verbose = 0;

/* External parser symbols */
extern int yyparse(void);
extern FILE *yyin;
extern puppet_program_t *parsed_program;

/**
 * @brief Signal handler for graceful shutdown
 */
static void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

/**
 * @brief Send a JSON response
 */
static enum MHD_Result send_json_response(struct MHD_Connection *connection,
                                          unsigned int status_code,
                                          const char *json) {
    struct MHD_Response *response;
    enum MHD_Result ret;

    response = MHD_create_response_from_buffer(strlen(json),
                                                (void *)json,
                                                MHD_RESPMEM_MUST_COPY);
    if (!response) return MHD_NO;

    MHD_add_response_header(response, "Content-Type", "application/json");
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");

    ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);

    return ret;
}

/**
 * @brief Send an error response
 */
static enum MHD_Result send_error(struct MHD_Connection *connection,
                                  unsigned int status_code,
                                  const char *message) {
    char json[512];
    snprintf(json, sizeof(json),
             "{\"error\": \"%s\", \"status\": %u}", message, status_code);
    return send_json_response(connection, status_code, json);
}

/**
 * @brief Handle GET /status
 */
static enum MHD_Result handle_status(struct MHD_Connection *connection) {
    const char *json = "{\"status\": \"ok\", \"version\": \"0.0.1\"}";
    return send_json_response(connection, MHD_HTTP_OK, json);
}

/**
 * @brief Connection info for POST data accumulation
 */
struct connection_info {
    char *post_data;
    size_t post_data_size;
};

/**
 * @brief Compile a catalog for the given request
 */
static char *compile_catalog(const char *certname, const char *environment,
                             json_value_t *facts_json) {
    puppet_program_t *program = NULL;
    puppet_loader_t *loader = NULL;
    char *catalog_json = NULL;

    /* Create loader for manifest directory */
    if (manifest_path) {
        loader = puppet_loader_create(manifest_path);
        if (!loader) {
            return puppet_strdup("{\"error\": \"Failed to create module loader\"}");
        }

        if (modules_path) {
            puppet_loader_set_modules_path(loader, modules_path);
        }

        /* Load site.pp */
        program = puppet_loader_load_site(loader);
    }

    if (!program) {
        if (loader) puppet_loader_destroy(loader);
        return puppet_strdup("{\"error\": \"No manifest found\"}");
    }

    /* Create environment */
    puppet_env_t *env = puppet_env_create();
    puppet_env_set_verbose(env, verbose);

    if (loader) {
        puppet_env_set_loader(env, loader);
    }

    /* Configure Hiera */
    if (hiera_datadir) {
        puppet_hiera_register_provider(env, hiera_datadir);
    }

    /* Set node name */
    puppet_env_set_node(env, certname);

    /* Enable catalog building */
    puppet_env_enable_catalog(env, certname, environment ? environment : DEFAULT_ENVIRONMENT);

    /* Set facts if provided */
    if (facts_json && facts_json->type == JSON_VALUE_OBJECT) {
        puppet_facts_db_t *facts_db = puppet_facts_db_create();
        if (puppet_facts_db_load_json(facts_db, certname, facts_json) == 0) {
            puppet_env_set_facts_db(env, facts_db);
            if (verbose) {
                fprintf(stderr, "[INFO] Loaded facts for node: %s\n", certname);
            }
        } else {
            puppet_facts_db_destroy(facts_db);
            if (verbose) {
                fprintf(stderr, "[WARN] Failed to load facts for node: %s\n", certname);
            }
        }
    }

    /* Execute program */
    puppet_exec_program(program, env);

    /* Check for compilation failure */
    if (env->compilation_failed) {
        char error_json[1024];
        snprintf(error_json, sizeof(error_json),
                 "{\"error\": \"Catalog compilation failed: %s\"}",
                 env->failure_message ? env->failure_message : "unknown error");
        catalog_json = puppet_strdup(error_json);
    } else {
        /* Get compiled catalog */
        puppet_catalog_t *catalog = puppet_env_get_catalog(env);
        if (catalog) {
            catalog_json = puppet_catalog_to_json(catalog);
            puppet_catalog_destroy(catalog);
        } else {
            catalog_json = puppet_strdup("{\"error\": \"No catalog generated\"}");
        }
    }

    /* Cleanup */
    puppet_env_destroy(env);
    puppet_program_destroy(program);
    if (loader) puppet_loader_destroy(loader);

    return catalog_json;
}

/**
 * @brief Handle POST /puppet/v4/catalog
 */
static enum MHD_Result handle_catalog(struct MHD_Connection *connection,
                                      const char *post_data) {
    /* Parse request JSON */
    if (!post_data || strlen(post_data) == 0) {
        return send_error(connection, MHD_HTTP_BAD_REQUEST,
                         "Missing request body");
    }

    /* Parse JSON to extract certname, environment, facts */
    json_parser_t *parser = json_parser_create(post_data);
    if (!parser) {
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "Failed to create JSON parser");
    }

    json_value_t *request = json_parse_value(parser);
    json_parser_destroy(parser);

    if (!request || request->type != JSON_VALUE_OBJECT) {
        if (request) json_value_destroy(request);
        return send_error(connection, MHD_HTTP_BAD_REQUEST,
                         "Invalid JSON in request body");
    }

    /* Extract certname (required) */
    json_value_t *certname_json = json_object_get(request, "certname");
    if (!certname_json || certname_json->type != JSON_VALUE_STRING) {
        json_value_destroy(request);
        return send_error(connection, MHD_HTTP_BAD_REQUEST,
                         "Missing or invalid 'certname' field");
    }
    const char *certname = certname_json->data.string_value;

    /* Extract environment (optional) */
    json_value_t *env_json = json_object_get(request, "environment");
    const char *environment = NULL;
    if (env_json && env_json->type == JSON_VALUE_STRING) {
        environment = env_json->data.string_value;
    }

    /* Extract facts (optional) */
    json_value_t *facts_json = json_object_get(request, "facts");

    if (verbose) {
        fprintf(stderr, "[INFO] Compiling catalog for node: %s (env: %s, facts: %s)\n",
                certname, environment ? environment : DEFAULT_ENVIRONMENT,
                facts_json ? "yes" : "no");
    }

    /* Compile catalog */
    char *catalog_json = compile_catalog(certname, environment, facts_json);

    json_value_destroy(request);

    if (!catalog_json) {
        return send_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                         "Failed to compile catalog");
    }

    enum MHD_Result ret = send_json_response(connection, MHD_HTTP_OK, catalog_json);
    puppet_free(catalog_json);

    return ret;
}

/**
 * @brief Request handler callback
 */
static enum MHD_Result request_handler(void *cls,
                                        struct MHD_Connection *connection,
                                        const char *url,
                                        const char *method,
                                        const char *version,
                                        const char *upload_data,
                                        size_t *upload_data_size,
                                        void **con_cls) {
    (void)cls;
    (void)version;

    /* Handle connection setup */
    if (*con_cls == NULL) {
        struct connection_info *con_info = puppet_calloc(1, sizeof(struct connection_info));
        if (!con_info) return MHD_NO;
        *con_cls = con_info;
        return MHD_YES;
    }

    struct connection_info *con_info = *con_cls;

    /* Accumulate POST data */
    if (*upload_data_size > 0) {
        char *new_data = puppet_realloc(con_info->post_data,
                                  con_info->post_data_size + *upload_data_size + 1);
        if (!new_data) return MHD_NO;

        memcpy(new_data + con_info->post_data_size, upload_data, *upload_data_size);
        con_info->post_data_size += *upload_data_size;
        new_data[con_info->post_data_size] = '\0';
        con_info->post_data = new_data;

        *upload_data_size = 0;
        return MHD_YES;
    }

    /* Route request */
    enum MHD_Result ret;

    if (strcmp(method, "GET") == 0 && strcmp(url, "/status") == 0) {
        ret = handle_status(connection);
    } else if (strcmp(method, "POST") == 0 &&
               strcmp(url, "/puppet/v4/catalog") == 0) {
        ret = handle_catalog(connection, con_info->post_data);
    } else if (strcmp(method, "OPTIONS") == 0) {
        /* CORS preflight */
        struct MHD_Response *response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
    } else {
        ret = send_error(connection, MHD_HTTP_NOT_FOUND, "Not found");
    }

    return ret;
}

/**
 * @brief Cleanup callback for connection
 */
static void request_completed(void *cls,
                               struct MHD_Connection *connection,
                               void **con_cls,
                               enum MHD_RequestTerminationCode toe) {
    (void)cls;
    (void)connection;
    (void)toe;

    struct connection_info *con_info = *con_cls;
    if (con_info) {
        puppet_free(con_info->post_data);
        puppet_free(con_info);
    }
    *con_cls = NULL;
}

/**
 * @brief Print usage information
 */
static void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS] <manifest_directory>\n", program_name);
    printf("\nPuppet catalog compilation server\n\n");
    printf("Options:\n");
    printf("  -p, --port PORT       Listen port (default: %d)\n", DEFAULT_PORT);
    printf("  -m, --modules PATH    Path to modules directory\n");
    printf("  -D, --hiera-data PATH Path to Hiera data directory\n");
    printf("  -v, --verbose         Enable verbose output\n");
    printf("  -h, --help            Show this help message\n");
    printf("\nAPI Endpoints:\n");
    printf("  GET  /status              Health check\n");
    printf("  POST /puppet/v4/catalog   Compile catalog\n");
    printf("\nExample:\n");
    printf("  %s -p 8140 /etc/puppet\n", program_name);
    printf("\n  curl -X POST http://localhost:8140/puppet/v4/catalog \\\n");
    printf("       -H 'Content-Type: application/json' \\\n");
    printf("       -d '{\"certname\": \"node1.example.com\"}'\n");
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    int opt;

    static struct option long_options[] = {
        {"port", required_argument, 0, 'p'},
        {"modules", required_argument, 0, 'm'},
        {"hiera-data", required_argument, 0, 'D'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "p:m:D:vh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                if (port <= 0 || port > 65535) {
                    fprintf(stderr, "Error: Invalid port number\n");
                    return 1;
                }
                break;
            case 'm':
                modules_path = optarg;
                break;
            case 'D':
                hiera_datadir = optarg;
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
        fprintf(stderr, "Error: No manifest directory specified\n");
        print_usage(argv[0]);
        return 1;
    }

    manifest_path = argv[optind];

    /* Initialize memory tracking */
    puppet_memory_init();

    /* Set up signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Start HTTP server */
    struct MHD_Daemon *daemon;
    daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY,
                               port,
                               NULL, NULL,
                               &request_handler, NULL,
                               MHD_OPTION_NOTIFY_COMPLETED, &request_completed, NULL,
                               MHD_OPTION_END);

    if (!daemon) {
        fprintf(stderr, "Error: Failed to start HTTP server on port %d\n", port);
        return 1;
    }

    printf("puppetc-server started on port %d\n", port);
    printf("Manifest directory: %s\n", manifest_path);
    if (modules_path) printf("Modules directory: %s\n", modules_path);
    if (hiera_datadir) printf("Hiera data directory: %s\n", hiera_datadir);
    printf("\nPress Ctrl+C to stop\n");

    /* Main loop */
    while (running) {
        sleep(1);
    }

    printf("\nShutting down...\n");

    MHD_stop_daemon(daemon);

    /* Cleanup */
    puppet_memory_shutdown();

    return 0;
}
