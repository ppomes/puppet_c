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
#include "puppet_ast.h"
#include "puppet_json.h"
#include "puppet_interpreter.h"
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
    printf("Usage: %s [OPTIONS] <puppet_file>\n", program_name);
    printf("Options:\n");
    printf("  -j, --json     Output AST as JSON\n");
    printf("  -e, --eval     Evaluate the manifest\n");
    printf("  -o, --output   Output file (default: stdout)\n");
    printf("  -h, --help     Show this help message\n");
}

int main(int argc, char *argv[]) {
    int json_output = 0;
    int eval_mode = 0;
    char *output_file = NULL;
    int opt;
    
    static struct option long_options[] = {
        {"json", no_argument, 0, 'j'},
        {"eval", no_argument, 0, 'e'},
        {"output", required_argument, 0, 'o'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "jeo:h", long_options, NULL)) != -1) {
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
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    if (optind >= argc) {
        fprintf(stderr, "Error: No input file specified\n");
        print_usage(argv[0]);
        return 1;
    }
    
    char *input_file = argv[optind];
    
    yyin = fopen(input_file, "r");
    if (!yyin) {
        perror("fopen");
        return 1;
    }
    
    if (!json_output && !eval_mode) {
        printf("Parsing %s...\n", input_file);
    }
    
    int result = yyparse();
    
    fclose(yyin);
    
    if (result == 0) {
        if (json_output) {
            FILE *output = stdout;
            if (output_file) {
                output = fopen(output_file, "w");
                if (!output) {
                    perror("fopen output");
                    puppet_program_destroy(parsed_program);
                    return 1;
                }
            }
            
            if (parsed_program) {
                puppet_program_to_json(parsed_program, output);
            } else {
                fprintf(output, "{\"type\":\"program\",\"statements\":[]}\n");
            }
            
            if (output_file) {
                fclose(output);
            }
        } else if (eval_mode) {
            if (parsed_program) {
                printf("Evaluating manifest...\n");
                puppet_env_t *env = puppet_env_create();
                puppet_exec_program(parsed_program, env);
                printf("Evaluation complete.\n");
                puppet_env_destroy(env);
            }
        } else {
            printf("Parse successful!\n");
            if (parsed_program) {
                printf("Program has %zu top-level statements\n", parsed_program->statements.count);
            }
        }
        
        if (parsed_program) {
            puppet_program_destroy(parsed_program);
        }
    } else {
        if (!json_output && !eval_mode) {
            printf("Parse failed!\n");
        }
    }
    
    return result;
}