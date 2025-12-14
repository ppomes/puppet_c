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
#include "puppet_ast.h"
#include "puppet_json.h"
#include "puppet_interpreter.h"
#include "puppet_loader.h"
#include "puppet_memory.h"
#include "puppet.tab.h"

/* External symbols from generated parser */
extern int yyparse(void);
extern FILE *yyin;
extern puppet_program_t *parsed_program;

/**
 * @brief Print command-line usage information
 * @param program_name Name of the executable for usage display
 */
static void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS] <puppet_file_or_directory>\n", program_name);
    printf("Options:\n");
    printf("  -j, --json        Output AST as JSON\n");
    printf("  -e, --eval        Evaluate the manifest\n");
    printf("  -o, --output      Output file (default: stdout)\n");
    printf("  -m, --modules     Path to modules directory (default: ./modules)\n");
    printf("  -n, --node        Execute only the specified node\n");
    printf("  -a, --all-nodes   Execute all nodes (default: only 'default' node)\n");
    printf("  -f, --facts       Load facts from JSON file (facter or PuppetDB format)\n");
    printf("  -t, --template    Display template output for file resource with specified title\n");
    printf("  -h, --help        Show this help message\n");
    printf("\nWhen a directory is provided, site.pp will be loaded from manifests/\n");
    printf("and modules will be loaded from modules/ subdirectory.\n");
    printf("\nNode execution:\n");
    printf("  By default, only the 'default' node is executed.\n");
    printf("  Use --node <name> to execute a specific node.\n");
    printf("  Use --all-nodes to execute all defined nodes.\n");
}

int main(int argc, char *argv[]) {
    /* Initialize memory tracking */
    puppet_memory_init();
    
    int json_output = 0;
    int eval_mode = 0;
    char *output_file = NULL;
    char *modules_path = NULL;
    char *node_name = NULL;
    int all_nodes = 0;
    char *facts_file = NULL;
    char *template_output = NULL;
    int opt;
    
    static struct option long_options[] = {
        {"json", no_argument, 0, 'j'},
        {"eval", no_argument, 0, 'e'},
        {"output", required_argument, 0, 'o'},
        {"modules", required_argument, 0, 'm'},
        {"node", required_argument, 0, 'n'},
        {"all-nodes", no_argument, 0, 'a'},
        {"facts", required_argument, 0, 'f'},
        {"template", required_argument, 0, 't'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "jeo:m:n:af:t:hd", long_options, NULL)) != -1) {
        switch (opt) {
            case 'j':
                json_output = 1;
                break;
            case 'e':
                eval_mode = 1;
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
                break;
            case 'f':
                facts_file = optarg;
                break;
            case 't':
                template_output = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'd':
                #if YYDEBUG
                extern int yydebug;
                yydebug = 1;
                #endif
                break;
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
    
    /* Validate template output option */
    if (template_output && !node_name) {
        fprintf(stderr, "Error: --template requires --node to be specified\n");
        return 1;
    }
    
    if (template_output && !eval_mode) {
        fprintf(stderr, "Error: --template requires --eval mode\n");
        return 1;
    }
    
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
        if (!json_output && !eval_mode) {
            printf("Loading Puppet directory: %s\n", input_path);
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
        
        if (!json_output && !eval_mode) {
            if (program && program->statements.count > 0) {
                printf("Loaded site.pp with %zu statements\n", program->statements.count);
            } else {
                printf("No site.pp found or empty site.pp\n");
            }
        }
    } else {
        /* File mode - traditional parsing */
        yyin = fopen(input_path, "r");
        if (!yyin) {
            perror("fopen");
            return 1;
        }
        
        if (!json_output && !eval_mode) {
            printf("Parsing %s...\n", input_path);
        }
        
        result = yyparse();
        fclose(yyin);
        
        if (result == 0) {
            program = parsed_program;
            parsed_program = NULL;
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
            printf("Evaluating manifest...\n");
            puppet_env_t *env = puppet_env_create();
            
            /* Set loader if in directory mode */
            if (loader) {
                puppet_env_set_loader(env, loader);
            }
            
            /* Configure node execution */
            if (all_nodes) {
                puppet_env_set_execute_all_nodes(env, true);
                printf("Executing all nodes.\n");
            } else if (node_name) {
                puppet_env_set_node(env, node_name);
                printf("Executing node: %s\n", node_name);
            } else {
                printf("Executing default node only.\n");
            }
            
            /* Set template output target if specified */
            if (template_output) {
                puppet_env_set_template_output(env, template_output);
                printf("Template output mode for resource title: %s\n", template_output);
            }
            
            /* Load facts if specified */
            if (facts_file) {
                printf("Loading facts from: %s\n", facts_file);
                puppet_facts_db_t *facts_db = puppet_facts_db_create();
                if (puppet_facts_db_load_file(facts_db, facts_file) == 0) {
                    puppet_env_set_facts_db(env, facts_db);
                    printf("Facts loaded successfully.\n");
                    
                    /* If node is specified, set it as current in facts */
                    if (node_name) {
                        if (puppet_facts_db_set_current_node(facts_db, node_name) == 0) {
                            printf("Using facts for node: %s\n", node_name);
                        }
                    }
                } else {
                    printf("Warning: Failed to load facts from %s\n", facts_file);
                    puppet_facts_db_destroy(facts_db);
                }
            }
            
            puppet_exec_program(program, env);
            printf("Evaluation complete.\n");
            puppet_env_destroy(env);
        } else {
            printf("Parse successful!\n");
            printf("Program has %zu top-level statements\n", program->statements.count);
        }
        
        puppet_program_destroy(program);
    } else if (result != 0) {
        if (!json_output && !eval_mode) {
            printf("Parse failed!\n");
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