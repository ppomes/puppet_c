/**
 * @file facter_main.c
 * @brief Standalone facter command for testing libfacter_c
 */

#include <stdio.h>
#include <stdlib.h>
#include "puppet_memory.h"
#include <string.h>
#include <getopt.h>

#include "facter.h"

typedef enum {
    FORMAT_TEXT,
    FORMAT_JSON,
    FORMAT_YAML
} output_format_t;

/* Print a JSON-escaped string (handles quotes, backslashes, control chars) */
static void print_json_escaped(const char *str) {
    putchar('"');
    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", stdout); break;
            case '\\': fputs("\\\\", stdout); break;
            case '\b': fputs("\\b", stdout); break;
            case '\f': fputs("\\f", stdout); break;
            case '\n': fputs("\\n", stdout); break;
            case '\r': fputs("\\r", stdout); break;
            case '\t': fputs("\\t", stdout); break;
            default:
                if ((unsigned char)*p < 0x20) {
                    printf("\\u%04x", (unsigned char)*p);
                } else {
                    putchar(*p);
                }
                break;
        }
    }
    putchar('"');
}

static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS] [FACT...]\n", prog);
    printf("\nNative fact collection tool (libfacter_c %s)\n", facter_version());
    printf("\nOptions:\n");
    printf("  -j, --json      Output in JSON format\n");
    printf("  -y, --yaml      Output in YAML format\n");
    printf("  -l, --list      List all fact names\n");
    printf("  -h, --help      Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s                    # Show all facts\n", prog);
    printf("  %s hostname ipaddress # Show specific facts\n", prog);
    printf("  %s -j                 # Output as JSON\n", prog);
}

int main(int argc, char *argv[]) {
    output_format_t format = FORMAT_TEXT;
    int list_only = 0;
    int opt;

    static struct option long_options[] = {
        {"json", no_argument, 0, 'j'},
        {"yaml", no_argument, 0, 'y'},
        {"list", no_argument, 0, 'l'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "jylh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'j':
                format = FORMAT_JSON;
                break;
            case 'y':
                format = FORMAT_YAML;
                break;
            case 'l':
                list_only = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    /* Create facter context and collect facts */
    facter_ctx_t *ctx = facter_create();
    if (!ctx) {
        fprintf(stderr, "Error: Failed to create facter context\n");
        return 1;
    }

    if (facter_collect(ctx) != 0) {
        fprintf(stderr, "Error: Failed to collect facts\n");
        facter_destroy(ctx);
        return 1;
    }

    /* List mode - just show fact names */
    if (list_only) {
        size_t count;
        const char **names = facter_list(ctx, &count);
        for (size_t i = 0; i < count; i++) {
            printf("%s\n", names[i]);
        }
        facter_destroy(ctx);
        return 0;
    }

    /* Specific facts requested */
    if (optind < argc) {
        for (int i = optind; i < argc; i++) {
            const char *name = argv[i];
            const char *value = facter_get_string(ctx, name);

            if (value) {
                if (format == FORMAT_JSON) {
                    putchar('{');
                    print_json_escaped(name);
                    fputs(": ", stdout);
                    print_json_escaped(value);
                    puts("}");
                } else if (format == FORMAT_YAML) {
                    printf("%s: \"%s\"\n", name, value);
                } else {
                    printf("%s => %s\n", name, value);
                }
            } else {
                /* Try integer */
                long int_val;
                if (facter_get_integer(ctx, name, &int_val) == 0) {
                    if (format == FORMAT_JSON) {
                        printf("{\"%s\": %ld}\n", name, int_val);
                    } else if (format == FORMAT_YAML) {
                        printf("%s: %ld\n", name, int_val);
                    } else {
                        printf("%s => %ld\n", name, int_val);
                    }
                } else {
                    /* Try boolean */
                    bool bool_val;
                    if (facter_get_boolean(ctx, name, &bool_val) == 0) {
                        const char *str = bool_val ? "true" : "false";
                        if (format == FORMAT_JSON) {
                            printf("{\"%s\": %s}\n", name, str);
                        } else if (format == FORMAT_YAML) {
                            printf("%s: %s\n", name, str);
                        } else {
                            printf("%s => %s\n", name, str);
                        }
                    } else {
                        fprintf(stderr, "Fact not found: %s\n", name);
                    }
                }
            }
        }
        facter_destroy(ctx);
        return 0;
    }

    /* Output all facts */
    char *output = NULL;
    switch (format) {
        case FORMAT_JSON:
            output = facter_to_json(ctx);
            break;
        case FORMAT_YAML:
            output = facter_to_yaml(ctx);
            break;
        case FORMAT_TEXT:
        default:
            output = facter_to_text(ctx);
            break;
    }

    if (output) {
        printf("%s", output);
        puppet_free(output);
    }

    facter_destroy(ctx);
    return 0;
}
