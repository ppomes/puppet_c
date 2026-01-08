/**
 * @file main.c
 * @brief Command-line interface for the Puppet C parser
 *
 * This file provides the main entry point for the Puppet language parser.
 * It handles command-line argument parsing, file I/O, and coordinates
 * between the lexer, parser, and various output modes.
 *
 * Supported modes:
 * - Parse-only: Validates syntax and reports errors
 * - JSON output: Serializes AST to JSON for analysis
 * - Evaluation: Interprets the manifest and executes statements
 *
 * The CLI uses getopt for POSIX-compliant argument parsing and supports
 * both short and long option formats for user convenience.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>
#include "puppet_ast.h"
#include "puppet_catalog.h"
#include "puppet_json.h"
#include "puppet_interpreter.h"
#include "puppet_loader.h"
#include "puppet_memory.h"
#include "puppet_ts_parser.h"
#include "puppet_hiera.h"

/**
 * @brief Print command-line usage information
 * @param program_name Name of the executable for usage display
 */
static void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS] <puppet_file_or_directory>\n", program_name);
    printf("Options:\n");
    printf("  -j, --json        Output AST as JSON\n");
    printf("  -e, --eval        Evaluate the manifest\n");
    printf("  -c, --catalog     Output compiled catalog as JSON (implies -e)\n");
    printf("  -p, --pretty      Output catalog in human-readable format with colors (implies -e)\n");
    printf("  -o, --output      Output file (default: stdout)\n");
    printf("  -m, --modules     Path to modules directory (default: ./modules)\n");
    printf("  -n, --node        Execute only the specified node\n");
    printf("  -a, --all-nodes   Execute all nodes (skips ERB for faster CI/CD)\n");
    printf("  -f, --facts       Load facts from JSON file (facter or PuppetDB format)\n");
    printf("  -t, --template    Display template output for file resource with specified title\n");
    printf("  -D, --hiera-data  Path to Hiera data directory (default: ./data)\n");
    printf("  -s, --summary     Print validation summary (for CI, implies -e)\n");
    printf("  -P, --parallel    Process nodes in parallel (faster, requires --all-nodes)\n");
    printf("  -v, --verbose     Enable verbose/debug output\n");
    printf("  -h, --help        Show this help message\n");
    printf("\nWhen a directory is provided, site.pp will be loaded from manifests/\n");
    printf("and modules will be loaded from modules/ subdirectory.\n");
    printf("\nNode execution:\n");
    printf("  By default, only the 'default' node is executed.\n");
    printf("  Use --node <name> to execute a specific node (matches regex nodes).\n");
    printf("  Use --all-nodes to execute all literal nodes (regex nodes are skipped).\n");
    printf("\nCI/Validation mode:\n");
    printf("  Use --all-nodes --summary to validate all nodes and get a summary.\n");
    printf("  Exit code: 0 = all nodes OK, 1 = errors found\n");
}

int main(int argc, char *argv[]) {
    /* Initialize memory tracking */
    puppet_memory_init();
    
    int json_output = 0;
    int eval_mode = 0;
    int catalog_mode = 0;
    int pretty_mode = 0;
    int summary_mode = 0;
    int parallel_mode = 0;
    char *output_file = NULL;
    char *modules_path = NULL;
    char *node_name = NULL;
    int all_nodes = 0;
    char *facts_file = NULL;
    char *template_output = NULL;
    char *hiera_datadir = NULL;
    int verbose = 0;
    int opt;

    static struct option long_options[] = {
        {"json", no_argument, 0, 'j'},
        {"eval", no_argument, 0, 'e'},
        {"catalog", no_argument, 0, 'c'},
        {"pretty", no_argument, 0, 'p'},
        {"summary", no_argument, 0, 's'},
        {"parallel", no_argument, 0, 'P'},
        {"output", required_argument, 0, 'o'},
        {"modules", required_argument, 0, 'm'},
        {"node", required_argument, 0, 'n'},
        {"all-nodes", no_argument, 0, 'a'},
        {"facts", required_argument, 0, 'f'},
        {"template", required_argument, 0, 't'},
        {"hiera-data", required_argument, 0, 'D'},
        {"verbose", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "jecpsPo:m:n:af:t:D:vh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'j':
                json_output = 1;
                break;
            case 'e':
                eval_mode = 1;
                break;
            case 'c':
                catalog_mode = 1;
                eval_mode = 1;  /* Catalog implies eval */
                break;
            case 'p':
                pretty_mode = 1;
                eval_mode = 1;  /* Pretty implies eval */
                break;
            case 's':
                summary_mode = 1;
                eval_mode = 1;  /* Summary implies eval */
                break;
            case 'P':
                parallel_mode = 1;
                eval_mode = 1;  /* Parallel implies eval */
                break;
            case 'o':
                output_file = optarg;
                break;
            case 'm':
                modules_path = optarg;
                break;
            case 'n':
                node_name = optarg;
                break;
            case 'a':
                all_nodes = 1;
                eval_mode = 1;  /* All-nodes implies eval */
                break;
            case 'f':
                facts_file = optarg;
                break;
            case 't':
                template_output = optarg;
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
        fprintf(stderr, "Error: No input file or directory specified\n");
        print_usage(argv[0]);
        return 1;
    }
    
    /* Validate node options */
    if (node_name && all_nodes) {
        fprintf(stderr, "Error: Cannot specify both --node and --all-nodes\n");
        return 1;
    }

    /* Catalog/pretty output requires single node */
    if (all_nodes && (catalog_mode || pretty_mode)) {
        fprintf(stderr, "Error: --all-nodes cannot be used with --catalog or --pretty\n");
        fprintf(stderr, "       Use --node to compile a catalog for a specific node\n");
        return 1;
    }

    /* Validate parallel mode - requires all-nodes and facts file */
    if (parallel_mode && !all_nodes) {
        fprintf(stderr, "Error: --parallel requires --all-nodes\n");
        return 1;
    }
    if (parallel_mode && !facts_file) {
        fprintf(stderr, "Error: --parallel requires --facts file with multiple nodes\n");
        return 1;
    }

    /* Validate template output option */
    if (template_output && !node_name) {
        fprintf(stderr, "Error: --template requires --node to be specified\n");
        return 1;
    }

    if (template_output && !eval_mode) {
        fprintf(stderr, "Error: --template requires --eval mode\n");
        return 1;
    }
    
    /* Set global verbose flag early for pre-environment debug output */
    puppet_verbose = verbose;

    char *input_path = argv[optind];

    /* Check if input is a directory */
    struct stat path_stat;
    if (stat(input_path, &path_stat) != 0) {
        perror("stat");
        return 1;
    }
    
    int result = 0;
    puppet_loader_t *loader = NULL;
    puppet_program_t *program = NULL;
    
    if (S_ISDIR(path_stat.st_mode)) {
        /* Directory mode - use module loader */
        if (!json_output && !eval_mode && verbose) {
            fprintf(stderr, "Loading Puppet directory: %s\n", input_path);
        }
        
        /* Create module loader */
        loader = puppet_loader_create(input_path);
        if (!loader) {
            fprintf(stderr, "Error: Failed to create module loader\n");
            return 1;
        }
        
        /* Override modules path if specified */
        if (modules_path) {
            puppet_loader_set_modules_path(loader, modules_path);
        }
        
        /* Try to load site.pp */
        program = puppet_loader_load_site(loader);
        if (!program) {
            /* If no site.pp, create empty program */
            program = puppet_calloc(1, sizeof(puppet_program_t));
            program->statements.stmts = NULL;
            program->statements.count = 0;
        }
        
        if (!json_output && !eval_mode && verbose) {
            if (program && program->statements.count > 0) {
                fprintf(stderr, "Loaded site.pp with %zu statements\n", program->statements.count);
            } else {
                fprintf(stderr, "No site.pp found or empty site.pp\n");
            }
        }
    } else {
        /* File mode - tree-sitter parsing */
        if (!json_output && !eval_mode && verbose) {
            fprintf(stderr, "Parsing %s...\n", input_path);
        }

        /* Create loader if modules path specified (for template() function) */
        if (modules_path) {
            loader = puppet_loader_create(".");
            if (loader) {
                puppet_loader_set_modules_path(loader, modules_path);
                if (verbose) fprintf(stderr, "Using modules path: %s\n", modules_path);
            }
        }

        puppet_stmt_list_t *stmts = puppet_ts_parse_file(input_path);
        if (stmts) {
            program = puppet_calloc(1, sizeof(puppet_program_t));
            program->statements = *stmts;
            puppet_free(stmts);
            result = 0;
        } else {
            fprintf(stderr, "Failed to parse: %s\n", input_path);
            result = 1;
        }
    }
    
    if (result == 0 && program) {
        if (json_output) {
            FILE *output = stdout;
            if (output_file) {
                output = fopen(output_file, "w");
                if (!output) {
                    perror("fopen output");
                    puppet_program_destroy(program);
                    if (loader) puppet_loader_destroy(loader);
                    return 1;
                }
            }
            
            puppet_program_to_json(program, output);
            
            if (output_file) {
                fclose(output);
            }
        } else if (eval_mode) {
            if (verbose) fprintf(stderr, "Evaluating manifest...\n");
            puppet_env_t *env = puppet_env_create();

            /* Set verbose mode */
            puppet_env_set_verbose(env, verbose);

            /* Set loader if in directory mode */
            if (loader) {
                puppet_env_set_loader(env, loader);
            }

            /* Configure Hiera data provider */
            if (hiera_datadir) {
                puppet_hiera_register_provider(env, hiera_datadir);
                if (verbose) fprintf(stderr, "Using Hiera data directory: %s\n", hiera_datadir);
            } else {
                /* Look for hiera.yaml in the manifest directory or parent */
                char *manifest_dir = puppet_strdup(argv[optind]);
                char *last_slash = strrchr(manifest_dir, '/');
                if (last_slash) {
                    *last_slash = '\0';
                    char hiera_yaml[1024];
                    struct stat st;

                    /* Try manifest directory first */
                    snprintf(hiera_yaml, sizeof(hiera_yaml), "%s/hiera.yaml", manifest_dir);
                    if (stat(hiera_yaml, &st) == 0 && S_ISREG(st.st_mode)) {
                        puppet_hiera_register_provider(env, hiera_yaml);
                        if (verbose) fprintf(stderr, "Using Hiera config: %s\n", hiera_yaml);
                    } else {
                        /* Try parent directory (e.g., preprod/ when manifest is preprod/manifests/site.pp) */
                        char *parent_slash = strrchr(manifest_dir, '/');
                        if (parent_slash) {
                            *parent_slash = '\0';
                            snprintf(hiera_yaml, sizeof(hiera_yaml), "%s/hiera.yaml", manifest_dir);
                            if (stat(hiera_yaml, &st) == 0 && S_ISREG(st.st_mode)) {
                                puppet_hiera_register_provider(env, hiera_yaml);
                                if (verbose) fprintf(stderr, "Using Hiera config: %s\n", hiera_yaml);
                            }
                        }
                    }
                }
                puppet_free(manifest_dir);
            }

            /* Configure node execution */
            if (all_nodes) {
                puppet_env_set_execute_all_nodes(env, true);
                if (parallel_mode) {
                    puppet_env_set_parallel_nodes(env, true);
                    if (verbose) fprintf(stderr, "Executing all nodes in parallel.\n");
                } else {
                    if (verbose) fprintf(stderr, "Executing all nodes.\n");
                }
            } else if (node_name) {
                puppet_env_set_node(env, node_name);
                if (verbose) fprintf(stderr, "Executing node: %s\n", node_name);
            } else {
                if (verbose) fprintf(stderr, "Executing default node only.\n");
            }

            /* Set template output target if specified */
            if (template_output) {
                puppet_env_set_template_output(env, template_output);
                if (verbose) fprintf(stderr, "Template output mode for resource title: %s\n", template_output);
            }

            /* Enable catalog building if requested */
            if (catalog_mode || pretty_mode) {
                const char *certname = node_name ? node_name : "localhost";
                puppet_env_enable_catalog(env, certname, "production");
                if (verbose) fprintf(stderr, "Building catalog for: %s\n", certname);
            }

            /* Load facts if specified */
            if (facts_file) {
                if (verbose) fprintf(stderr, "Loading facts from: %s\n", facts_file);
                puppet_facts_db_t *facts_db = puppet_facts_db_create();
                if (puppet_facts_db_load_file(facts_db, facts_file) == 0) {
                    puppet_env_set_facts_db(env, facts_db);
                    if (verbose) fprintf(stderr, "Facts loaded successfully.\n");

                    /* If node is specified, set it as current in facts */
                    if (node_name) {
                        if (puppet_facts_db_set_current_node(facts_db, node_name) == 0) {
                            if (verbose) fprintf(stderr, "Using facts for node: %s\n", node_name);
                        }
                    }
                } else {
                    fprintf(stderr, "Warning: Failed to load facts from %s\n", facts_file);
                    puppet_facts_db_destroy(facts_db);
                }
            } else if (node_name && !all_nodes) {
                /* No facts file provided for single node - use local host facts as fallback */
                fprintf(stderr, "Warning: No facts file provided, using local host facts for '%s'\n", node_name);
                puppet_facts_db_t *facts_db = puppet_facts_db_create();
                if (puppet_facts_db_load_from_facter(facts_db, node_name) == 0) {
                    puppet_env_set_facts_db(env, facts_db);
                    if (verbose) fprintf(stderr, "Local host facts collected successfully.\n");
                } else {
                    fprintf(stderr, "Warning: Failed to collect local host facts\n");
                    puppet_facts_db_destroy(facts_db);
                }
            }

            puppet_exec_program(program, env);
            if (verbose) fprintf(stderr, "Evaluation complete.\n");

            // Check if template target was found
            if (template_output && !env->template_output_found) {
                fprintf(stderr, "Warning: No file resource with title '%s' found\n", template_output);
            }

            // Check if compilation failed due to fail() function
            if (env->compilation_failed) {
                if (env->failure_message) {
                    fprintf(stderr, "Catalog compilation failed: %s\n", env->failure_message);
                } else {
                    fprintf(stderr, "Catalog compilation failed\n");
                }
                puppet_env_destroy(env);
                puppet_program_destroy(program);
                if (loader) puppet_loader_destroy(loader);
                return 1;  // Exit with failure
            }

            /* Output catalog if requested */
            if (catalog_mode) {
                puppet_catalog_t *catalog = puppet_env_get_catalog(env);
                if (catalog) {
                    char *json = puppet_catalog_to_json(catalog);
                    if (json) {
                        FILE *out = stdout;
                        if (output_file) {
                            out = fopen(output_file, "w");
                            if (!out) {
                                perror("fopen output");
                                puppet_free(json);
                                puppet_catalog_destroy(catalog);
                                puppet_env_destroy(env);
                                puppet_program_destroy(program);
                                if (loader) puppet_loader_destroy(loader);
                                return 1;
                            }
                        }
                        fputs(json, out);
                        if (output_file) fclose(out);
                        puppet_free(json);
                    }
                    puppet_catalog_destroy(catalog);
                }
            } else if (pretty_mode) {
                /* Output catalog in human-readable format */
                puppet_catalog_t *catalog = puppet_env_get_catalog(env);
                if (catalog) {
                    FILE *out = stdout;
                    if (output_file) {
                        out = fopen(output_file, "w");
                        if (!out) {
                            perror("fopen output");
                            puppet_catalog_destroy(catalog);
                            puppet_env_destroy(env);
                            puppet_program_destroy(program);
                            if (loader) puppet_loader_destroy(loader);
                            return 1;
                        }
                    }
                    /* Use colors if output is a terminal */
                    bool use_color = (out == stdout && isatty(fileno(stdout)));
                    puppet_catalog_pretty_print(catalog, out, use_color);
                    if (output_file) fclose(out);
                    puppet_catalog_destroy(catalog);
                }
            }

            /* Print validation summary if requested */
            if (summary_mode) {
                size_t nodes_processed, nodes_failed, nodes_skipped, errors, warnings;
                puppet_env_get_stats(env, &nodes_processed, &nodes_failed, &nodes_skipped, &errors, &warnings);

                fprintf(stderr, "\n=== Validation Summary ===\n");
                fprintf(stderr, "Nodes processed: %zu\n", nodes_processed);
                fprintf(stderr, "Nodes failed:    %zu\n", nodes_failed);
                if (nodes_skipped > 0) {
                    fprintf(stderr, "Regex skipped:   %zu\n", nodes_skipped);
                }
                fprintf(stderr, "Total errors:    %zu\n", errors);
                fprintf(stderr, "Total warnings:  %zu\n", warnings);

                if (nodes_failed > 0 || errors > 0) {
                    fprintf(stderr, "Status: FAILED\n");
                    result = 1;
                } else {
                    fprintf(stderr, "Status: OK\n");
                }
            }

            puppet_env_destroy(env);
        } else {
            if (verbose) {
                fprintf(stderr, "Parse successful!\n");
                fprintf(stderr, "Program has %zu top-level statements\n", program->statements.count);
            }
        }

        puppet_program_destroy(program);
    } else if (result != 0) {
        if (!json_output && !eval_mode) {
            fprintf(stderr, "Parse failed!\n");
        }
    }
    
    if (loader) {
        puppet_loader_destroy(loader);
    }
    
    /* Shutdown memory tracking and report leaks */
    int memory_leaks = puppet_memory_shutdown();
    if (memory_leaks > 0) {
        fprintf(stderr, "Warning: %d memory leaks detected\n", memory_leaks);
    }
    
    return result;
}