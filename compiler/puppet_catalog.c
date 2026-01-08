/**
 * @file puppet_catalog.c
 * @brief Puppet resource catalog implementation
 */

#include "puppet_catalog.h"
#include "puppet_memory.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * ===========================================================================
 * CATALOG MANAGEMENT
 * ===========================================================================
 */

puppet_catalog_t *puppet_catalog_create(const char *certname, const char *environment) {
    puppet_catalog_t *catalog = puppet_calloc(1, sizeof(puppet_catalog_t));
    if (!catalog) return NULL;

    catalog->certname = puppet_strdup(certname ? certname : "localhost");
    catalog->environment = puppet_strdup(environment ? environment : "production");

    /* Generate version from timestamp */
    catalog->timestamp = time(NULL);
    char version_buf[32];
    snprintf(version_buf, sizeof(version_buf), "%ld", (long)catalog->timestamp);
    catalog->version = puppet_strdup(version_buf);

    /* Initial capacities */
    catalog->resource_capacity = 64;
    catalog->resources = puppet_calloc(catalog->resource_capacity, sizeof(puppet_catalog_resource_t));

    catalog->edge_capacity = 32;
    catalog->edges = puppet_calloc(catalog->edge_capacity, sizeof(puppet_catalog_edge_t));

    catalog->class_capacity = 16;
    catalog->classes = puppet_calloc(catalog->class_capacity, sizeof(char *));

    return catalog;
}

void puppet_catalog_destroy(puppet_catalog_t *catalog) {
    if (!catalog) return;

    puppet_free(catalog->certname);
    puppet_free(catalog->environment);
    puppet_free(catalog->version);

    /* Free resources */
    for (size_t i = 0; i < catalog->resource_count; i++) {
        puppet_catalog_resource_t *res = &catalog->resources[i];
        puppet_free(res->type);
        puppet_free(res->title);
        puppet_free(res->file);

        for (size_t j = 0; j < res->param_count; j++) {
            puppet_free(res->parameters[j].name);
            if (res->parameters[j].value) {
                puppet_value_destroy(res->parameters[j].value);
            }
        }
        puppet_free(res->parameters);

        for (size_t j = 0; j < res->tag_count; j++) {
            puppet_free(res->tags[j]);
        }
        puppet_free(res->tags);
    }
    puppet_free(catalog->resources);

    /* Free edges */
    for (size_t i = 0; i < catalog->edge_count; i++) {
        puppet_free(catalog->edges[i].source.type);
        puppet_free(catalog->edges[i].source.title);
        puppet_free(catalog->edges[i].target.type);
        puppet_free(catalog->edges[i].target.title);
    }
    puppet_free(catalog->edges);

    /* Free classes */
    for (size_t i = 0; i < catalog->class_count; i++) {
        puppet_free(catalog->classes[i]);
    }
    puppet_free(catalog->classes);

    /* Free tags */
    for (size_t i = 0; i < catalog->tag_count; i++) {
        puppet_free(catalog->tags[i]);
    }
    puppet_free(catalog->tags);

    puppet_free(catalog);
}

int puppet_catalog_add_resource(puppet_catalog_t *catalog,
                                const char *type,
                                const char *title,
                                puppet_catalog_param_t *params,
                                size_t param_count,
                                const char *file,
                                int line) {
    if (!catalog || !type || !title) return -2;

    /* Check for duplicate */
    if (puppet_catalog_find_resource(catalog, type, title)) {
        return -1;  /* Duplicate */
    }

    /* Grow array if needed */
    if (catalog->resource_count >= catalog->resource_capacity) {
        size_t new_capacity = catalog->resource_capacity * 2;
        puppet_catalog_resource_t *new_resources = puppet_realloc(
            catalog->resources,
            new_capacity * sizeof(puppet_catalog_resource_t)
        );
        if (!new_resources) return -2;
        catalog->resources = new_resources;
        catalog->resource_capacity = new_capacity;
    }

    puppet_catalog_resource_t *res = &catalog->resources[catalog->resource_count];
    memset(res, 0, sizeof(puppet_catalog_resource_t));

    res->type = puppet_strdup(type);
    res->title = puppet_strdup(title);
    res->parameters = params;
    res->param_count = param_count;
    res->exported = false;
    res->file = file ? puppet_strdup(file) : NULL;
    res->line = line;

    catalog->resource_count++;
    return 0;
}

int puppet_catalog_update_resource(puppet_catalog_t *catalog,
                                   const char *type,
                                   const char *title,
                                   puppet_catalog_param_t *params,
                                   size_t param_count) {
    if (!catalog || !type || !title) return -2;

    puppet_catalog_resource_t *res = puppet_catalog_find_resource(catalog, type, title);
    if (!res) return -1;  /* Not found */

    /* Free old parameters */
    for (size_t i = 0; i < res->param_count; i++) {
        puppet_free(res->parameters[i].name);
        puppet_value_destroy(res->parameters[i].value);
    }
    puppet_free(res->parameters);

    /* Set new parameters */
    res->parameters = params;
    res->param_count = param_count;

    return 0;
}

int puppet_catalog_add_edge(puppet_catalog_t *catalog,
                            const char *source_type,
                            const char *source_title,
                            const char *target_type,
                            const char *target_title,
                            puppet_relationship_t relationship) {
    if (!catalog) return -1;

    /* Grow array if needed */
    if (catalog->edge_count >= catalog->edge_capacity) {
        size_t new_capacity = catalog->edge_capacity * 2;
        puppet_catalog_edge_t *new_edges = puppet_realloc(
            catalog->edges,
            new_capacity * sizeof(puppet_catalog_edge_t)
        );
        if (!new_edges) return -1;
        catalog->edges = new_edges;
        catalog->edge_capacity = new_capacity;
    }

    puppet_catalog_edge_t *edge = &catalog->edges[catalog->edge_count];
    edge->source.type = puppet_strdup(source_type);
    edge->source.title = puppet_strdup(source_title);
    edge->target.type = puppet_strdup(target_type);
    edge->target.title = puppet_strdup(target_title);
    edge->relationship = relationship;

    catalog->edge_count++;
    return 0;
}

int puppet_catalog_add_class(puppet_catalog_t *catalog, const char *class_name) {
    if (!catalog || !class_name) return -1;

    /* Check for duplicate */
    for (size_t i = 0; i < catalog->class_count; i++) {
        if (strcmp(catalog->classes[i], class_name) == 0) {
            return 0;  /* Already added, not an error */
        }
    }

    /* Grow array if needed */
    if (catalog->class_count >= catalog->class_capacity) {
        size_t new_capacity = catalog->class_capacity * 2;
        char **new_classes = puppet_realloc(
            catalog->classes,
            new_capacity * sizeof(char *)
        );
        if (!new_classes) return -1;
        catalog->classes = new_classes;
        catalog->class_capacity = new_capacity;
    }

    catalog->classes[catalog->class_count] = puppet_strdup(class_name);
    catalog->class_count++;
    return 0;
}

puppet_catalog_resource_t *puppet_catalog_find_resource(puppet_catalog_t *catalog,
                                                        const char *type,
                                                        const char *title) {
    if (!catalog || !type || !title) return NULL;

    for (size_t i = 0; i < catalog->resource_count; i++) {
        if (strcasecmp(catalog->resources[i].type, type) == 0 &&
            strcmp(catalog->resources[i].title, title) == 0) {
            return &catalog->resources[i];
        }
    }
    return NULL;
}

/*
 * ===========================================================================
 * JSON SERIALIZATION
 * ===========================================================================
 */

const char *puppet_relationship_to_string(puppet_relationship_t rel) {
    switch (rel) {
        case PUPPET_REL_BEFORE:    return "before";
        case PUPPET_REL_REQUIRE:   return "required-by";
        case PUPPET_REL_NOTIFY:    return "notifies";
        case PUPPET_REL_SUBSCRIBE: return "subscription-of";
        default:                   return "unknown";
    }
}

/* Helper to escape JSON strings */
static void json_escape_string(FILE *out, const char *str) {
    fputc('"', out);
    if (str) {
        for (const char *p = str; *p; p++) {
            switch (*p) {
                case '"':  fputs("\\\"", out); break;
                case '\\': fputs("\\\\", out); break;
                case '\b': fputs("\\b", out); break;
                case '\f': fputs("\\f", out); break;
                case '\n': fputs("\\n", out); break;
                case '\r': fputs("\\r", out); break;
                case '\t': fputs("\\t", out); break;
                default:
                    if ((unsigned char)*p < 0x20) {
                        fprintf(out, "\\u%04x", (unsigned char)*p);
                    } else {
                        fputc(*p, out);
                    }
            }
        }
    }
    fputc('"', out);
}

/* Helper to write a puppet_value as JSON */
static void json_write_value(FILE *out, puppet_value_t *value) {
    if (!value) {
        fputs("null", out);
        return;
    }

    switch (value->type) {
        case PUPPET_VALUE_STRING:
            json_escape_string(out, value->data.string.data);
            break;
        case PUPPET_VALUE_NUMBER:
            fprintf(out, "%g", value->data.number);
            break;
        case PUPPET_VALUE_BOOL:
            fputs(value->data.boolean ? "true" : "false", out);
            break;
        case PUPPET_VALUE_UNDEF:
            fputs("null", out);
            break;
        case PUPPET_VALUE_ARRAY:
            fputc('[', out);
            for (size_t i = 0; i < value->data.array->count; i++) {
                if (i > 0) fputs(", ", out);
                json_write_value(out, value->data.array->items[i]);
            }
            fputc(']', out);
            break;
        case PUPPET_VALUE_HASH:
            fputc('{', out);
            {
                bool first = true;
                puppet_hash_t *hash = value->data.hash;
                for (size_t i = 0; i < hash->bucket_count; i++) {
                    puppet_hash_entry_t *entry = hash->buckets[i];
                    while (entry) {
                        if (!first) fputs(", ", out);
                        first = false;
                        json_escape_string(out, entry->key.data);
                        fputs(": ", out);
                        json_write_value(out, entry->value);
                        entry = entry->next;
                    }
                }
            }
            fputc('}', out);
            break;
        case PUPPET_VALUE_DEFERRED:
            if (value->data.deferred) {
                fputs("{\"__ptype\": \"Deferred\", \"name\": ", out);
                json_escape_string(out, value->data.deferred->function_name);
                fputs(", \"arguments\": [", out);
                if (value->data.deferred->arguments) {
                    for (size_t i = 0; i < value->data.deferred->arguments->count; i++) {
                        if (i > 0) fputs(", ", out);
                        json_write_value(out, value->data.deferred->arguments->items[i]);
                    }
                }
                fputs("]}", out);
            } else {
                fputs("null", out);
            }
            break;
        default:
            fputs("null", out);
    }
}

char *puppet_catalog_to_json(puppet_catalog_t *catalog) {
    if (!catalog) return NULL;

    /* Write to memory stream */
    char *buffer = NULL;
    size_t size = 0;
    FILE *out = open_memstream(&buffer, &size);
    if (!out) return NULL;

    fputs("{\n", out);

    /* Metadata */
    fputs("  \"certname\": ", out);
    json_escape_string(out, catalog->certname);
    fputs(",\n", out);

    fputs("  \"version\": ", out);
    json_escape_string(out, catalog->version);
    fputs(",\n", out);

    fputs("  \"environment\": ", out);
    json_escape_string(out, catalog->environment);
    fputs(",\n", out);

    fputs("  \"catalog_format\": 1,\n", out);

    /* Tags (catalog level) */
    fputs("  \"tags\": [", out);
    for (size_t i = 0; i < catalog->tag_count; i++) {
        if (i > 0) fputs(", ", out);
        json_escape_string(out, catalog->tags[i]);
    }
    fputs("],\n", out);

    /* Classes */
    fputs("  \"classes\": [", out);
    for (size_t i = 0; i < catalog->class_count; i++) {
        if (i > 0) fputs(", ", out);
        json_escape_string(out, catalog->classes[i]);
    }
    fputs("],\n", out);

    /* Resources */
    fputs("  \"resources\": [\n", out);
    for (size_t i = 0; i < catalog->resource_count; i++) {
        puppet_catalog_resource_t *res = &catalog->resources[i];

        if (i > 0) fputs(",\n", out);
        fputs("    {\n", out);

        fputs("      \"type\": ", out);
        json_escape_string(out, res->type);
        fputs(",\n", out);

        fputs("      \"title\": ", out);
        json_escape_string(out, res->title);
        fputs(",\n", out);

        fprintf(out, "      \"exported\": %s,\n", res->exported ? "true" : "false");

        /* Tags */
        fputs("      \"tags\": [", out);
        for (size_t j = 0; j < res->tag_count; j++) {
            if (j > 0) fputs(", ", out);
            json_escape_string(out, res->tags[j]);
        }
        fputs("],\n", out);

        /* Source location */
        if (res->file) {
            fputs("      \"file\": ", out);
            json_escape_string(out, res->file);
            fputs(",\n", out);
            fprintf(out, "      \"line\": %d,\n", res->line);
        }

        /* Parameters */
        fputs("      \"parameters\": {", out);
        for (size_t j = 0; j < res->param_count; j++) {
            if (j > 0) fputs(", ", out);
            json_escape_string(out, res->parameters[j].name);
            fputs(": ", out);
            json_write_value(out, res->parameters[j].value);
        }
        fputs("}\n", out);

        fputs("    }", out);
    }
    fputs("\n  ],\n", out);

    /* Edges */
    fputs("  \"edges\": [\n", out);
    for (size_t i = 0; i < catalog->edge_count; i++) {
        puppet_catalog_edge_t *edge = &catalog->edges[i];

        if (i > 0) fputs(",\n", out);
        fputs("    {\n", out);

        fprintf(out, "      \"source\": {\"type\": ");
        json_escape_string(out, edge->source.type);
        fputs(", \"title\": ", out);
        json_escape_string(out, edge->source.title);
        fputs("},\n", out);

        fprintf(out, "      \"target\": {\"type\": ");
        json_escape_string(out, edge->target.type);
        fputs(", \"title\": ", out);
        json_escape_string(out, edge->target.title);
        fputs("},\n", out);

        fputs("      \"relationship\": ", out);
        json_escape_string(out, puppet_relationship_to_string(edge->relationship));
        fputc('\n', out);

        fputs("    }", out);
    }
    fputs("\n  ]\n", out);

    fputs("}\n", out);

    fclose(out);
    return buffer;
}

int puppet_catalog_write_json(puppet_catalog_t *catalog, const char *filepath) {
    char *json = puppet_catalog_to_json(catalog);
    if (!json) return -1;

    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        puppet_free(json);
        return -1;
    }

    fputs(json, fp);
    fclose(fp);
    puppet_free(json);
    return 0;
}

/*
 * ===========================================================================
 * PRETTY PRINT OUTPUT (language-puppet style)
 * ===========================================================================
 */

/* ANSI color codes */
#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_DIM     "\033[2m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"
#define COLOR_BRIGHT_RED     "\033[91m"
#define COLOR_BRIGHT_GREEN   "\033[92m"
#define COLOR_BRIGHT_YELLOW  "\033[93m"
#define COLOR_BRIGHT_BLUE    "\033[94m"
#define COLOR_BRIGHT_MAGENTA "\033[95m"
#define COLOR_BRIGHT_CYAN    "\033[96m"

/* Helper to print a value in pretty format */
static void pretty_print_value(FILE *out, puppet_value_t *value, bool use_color) {
    if (!value) {
        if (use_color) fputs(COLOR_DIM, out);
        fputs("undef", out);
        if (use_color) fputs(COLOR_RESET, out);
        return;
    }

    switch (value->type) {
        case PUPPET_VALUE_STRING:
            if (use_color) fputs(COLOR_CYAN, out);
            /* Check if it looks like a resource reference */
            if (value->data.string.data &&
                (strstr(value->data.string.data, "[") != NULL ||
                 (value->data.string.data[0] >= 'A' && value->data.string.data[0] <= 'Z'))) {
                /* Could be a reference like Class[foo] or Package[bar] */
                if (use_color) fputs(COLOR_BRIGHT_GREEN, out);
            }
            fprintf(out, "%s", value->data.string.data ? value->data.string.data : "");
            if (use_color) fputs(COLOR_RESET, out);
            break;

        case PUPPET_VALUE_NUMBER:
            if (use_color) fputs(COLOR_BRIGHT_YELLOW, out);
            fprintf(out, "%g", value->data.number);
            if (use_color) fputs(COLOR_RESET, out);
            break;

        case PUPPET_VALUE_BOOL:
            if (use_color) fputs(COLOR_BRIGHT_MAGENTA, out);
            fputs(value->data.boolean ? "true" : "false", out);
            if (use_color) fputs(COLOR_RESET, out);
            break;

        case PUPPET_VALUE_UNDEF:
            if (use_color) fputs(COLOR_DIM, out);
            fputs("undef", out);
            if (use_color) fputs(COLOR_RESET, out);
            break;

        case PUPPET_VALUE_ARRAY:
            fputc('[', out);
            for (size_t i = 0; i < value->data.array->count; i++) {
                if (i > 0) fputs(", ", out);
                pretty_print_value(out, value->data.array->items[i], use_color);
            }
            fputc(']', out);
            break;

        case PUPPET_VALUE_HASH:
            fputc('{', out);
            {
                bool first = true;
                puppet_hash_t *hash = value->data.hash;
                for (size_t i = 0; i < hash->bucket_count; i++) {
                    puppet_hash_entry_t *entry = hash->buckets[i];
                    while (entry) {
                        if (!first) fputs(", ", out);
                        first = false;
                        if (use_color) fputs(COLOR_YELLOW, out);
                        fprintf(out, "%s", entry->key.data);
                        if (use_color) fputs(COLOR_RESET, out);
                        fputs(" => ", out);
                        pretty_print_value(out, entry->value, use_color);
                        entry = entry->next;
                    }
                }
            }
            fputc('}', out);
            break;

        default:
            if (use_color) fputs(COLOR_DIM, out);
            fputs("???", out);
            if (use_color) fputs(COLOR_RESET, out);
    }
}

void puppet_catalog_pretty_print(puppet_catalog_t *catalog, FILE *out, bool use_color) {
    if (!catalog || !out) return;

    for (size_t i = 0; i < catalog->resource_count; i++) {
        puppet_catalog_resource_t *res = &catalog->resources[i];

        /* Resource header: type/title: file:line node [Scope [...]] */
        if (use_color) fputs(COLOR_BRIGHT_MAGENTA, out);
        fprintf(out, "%s", res->type);
        if (use_color) fputs(COLOR_RESET, out);
        fputc('/', out);
        if (use_color) fputs(COLOR_BRIGHT_CYAN, out);
        fprintf(out, "%s", res->title);
        if (use_color) fputs(COLOR_RESET, out);
        fputs(": ", out);

        /* Source location */
        if (res->file) {
            if (use_color) fputs(COLOR_DIM, out);
            fprintf(out, "%s:%d", res->file, res->line);
            if (use_color) fputs(COLOR_RESET, out);
        }

        /* Node name */
        fprintf(out, " %s", catalog->certname);

        /* Scope info from tags if available */
        if (res->tag_count > 0) {
            fputs(" [Scope [", out);
            if (use_color) fputs(COLOR_GREEN, out);
            bool first_tag = true;
            for (size_t j = 0; j < res->tag_count; j++) {
                /* Only show class-like tags */
                if (strstr(res->tags[j], "::") ||
                    (res->tags[j][0] >= 'a' && res->tags[j][0] <= 'z')) {
                    if (!first_tag) fputs(", ", out);
                    fprintf(out, "class %s", res->tags[j]);
                    first_tag = false;
                }
            }
            if (use_color) fputs(COLOR_RESET, out);
            fputs("]]", out);
        }
        fputc('\n', out);

        /* Parameters */
        for (size_t j = 0; j < res->param_count; j++) {
            fputs("  ", out);
            if (use_color) fputs(COLOR_YELLOW, out);
            fprintf(out, "%s", res->parameters[j].name);
            if (use_color) fputs(COLOR_RESET, out);
            fputs(" => ", out);
            pretty_print_value(out, res->parameters[j].value, use_color);
            fputs(",\n", out);
        }

        fputc('\n', out);
    }

    /* Summary */
    if (use_color) fputs(COLOR_BRIGHT_GREEN, out);
    fprintf(out, "Total: %zu resources\n", catalog->resource_count);
    if (use_color) fputs(COLOR_RESET, out);
}
