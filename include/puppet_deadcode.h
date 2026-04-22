/**
 * @file puppet_deadcode.h
 * @brief Dead-code detection tracker
 *
 * Inventories declared classes/defines/functions/templates in a modules tree,
 * then is fed "used" events during compilation, and finally reports the
 * declared items that were never used.
 */
#ifndef PUPPET_DEADCODE_H
#define PUPPET_DEADCODE_H

#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
#include <stddef.h>

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} puppet_deadcode_set_t;

typedef struct puppet_deadcode {
    puppet_deadcode_set_t declared_classes;
    puppet_deadcode_set_t declared_defines;
    puppet_deadcode_set_t declared_pp_funcs;
    puppet_deadcode_set_t declared_rb_funcs;
    puppet_deadcode_set_t declared_templates;
    puppet_deadcode_set_t used_classes;
    puppet_deadcode_set_t used_defines;
    puppet_deadcode_set_t used_functions;
    puppet_deadcode_set_t used_templates;
    /* Map declaration name -> file path, for reporting. Parallel arrays to declared_* sets. */
    char **class_files;
    char **define_files;
    char **pp_func_files;
    char **rb_func_files;
    char **template_files;
    pthread_mutex_t mutex;
} puppet_deadcode_t;

/** Walk modules_path and build the declared_* inventories. */
puppet_deadcode_t *puppet_deadcode_create(const char *modules_path);
void puppet_deadcode_destroy(puppet_deadcode_t *dc);

/* Thread-safe "mark used" calls. Accept a class/define/etc. name (with leading :: stripped). */
void puppet_deadcode_mark_class_used(puppet_deadcode_t *dc, const char *name);
void puppet_deadcode_mark_define_used(puppet_deadcode_t *dc, const char *name);
void puppet_deadcode_mark_function_used(puppet_deadcode_t *dc, const char *name);
void puppet_deadcode_mark_template_used(puppet_deadcode_t *dc, const char *name);

/** Print a report of declared items that were never marked used. */
void puppet_deadcode_report(puppet_deadcode_t *dc, FILE *out);

#endif /* PUPPET_DEADCODE_H */
