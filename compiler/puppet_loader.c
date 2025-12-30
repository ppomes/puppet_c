/**
 * @file puppet_loader.c
 * @brief Implementation of the Puppet module loading system
 */

#include "puppet_loader.h"
#include "puppet_ts_parser.h"
#include "puppet_memory.h"
#include "puppet_interpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

puppet_loader_t *puppet_loader_create(const char *base_path) {
    puppet_loader_t *loader = puppet_calloc(1, sizeof(puppet_loader_t));
    if (!loader) return NULL;
    
    /* Set up base paths */
    loader->base_path = puppet_strdup(base_path ? base_path : ".");
    
    /* Construct default paths */
    size_t base_len = strlen(loader->base_path);
    
    loader->modules_path = puppet_malloc(base_len + 10);
    sprintf(loader->modules_path, "%s/modules", loader->base_path);
    
    loader->manifests_path = puppet_malloc(base_len + 12);
    sprintf(loader->manifests_path, "%s/manifests", loader->base_path);
    
    /* Initialize class cache */
    loader->loaded_classes.capacity = 16;
    loader->loaded_classes.class_names = puppet_calloc(loader->loaded_classes.capacity, sizeof(char*));
    loader->loaded_classes.class_defs = puppet_calloc(loader->loaded_classes.capacity, sizeof(puppet_stmt_t*));
    loader->loaded_classes.count = 0;
    
    return loader;
}

void puppet_loader_destroy(puppet_loader_t *loader) {
    if (!loader) return;
    
    /* Clean up loaded classes cache */
    for (size_t i = 0; i < loader->loaded_classes.count; i++) {
        puppet_free(loader->loaded_classes.class_names[i]);
        /* Note: class_defs are owned by their programs, not freed here */
    }
    puppet_free(loader->loaded_classes.class_names);
    puppet_free(loader->loaded_classes.class_defs);
    
    puppet_free(loader->base_path);
    puppet_free(loader->modules_path);
    puppet_free(loader->manifests_path);
    puppet_free(loader);
}

bool puppet_loader_resolve_class_path(puppet_loader_t *loader,
                                      const char *class_name,
                                      char *path_buffer,
                                      size_t buffer_size) {
    if (!loader || !class_name || !path_buffer) return false;
    
    /* Handle simple class names (no :: separator) */
    const char *separator = strstr(class_name, "::");
    if (!separator) {
        /* Simple class: modules/classname/manifests/init.pp */
        snprintf(path_buffer, buffer_size, "%s/%s/manifests/init.pp",
                loader->modules_path, class_name);
    } else {
        /* Namespaced class: modules/module/manifests/subclass.pp */
        /* Extract module name (part before first ::) */
        size_t module_len = separator - class_name;
        char *module_name = puppet_malloc(module_len + 1);
        strncpy(module_name, class_name, module_len);
        module_name[module_len] = '\0';
        
        /* Convert remaining :: to / for path */
        const char *rest = separator + 2;
        char *manifest_path = puppet_strdup(rest);
        
        /* Replace :: with / in the remaining path */
        char *pos = manifest_path;
        while ((pos = strstr(pos, "::")) != NULL) {
            pos[0] = '/';
            memmove(pos + 1, pos + 2, strlen(pos + 2) + 1);
        }
        
        snprintf(path_buffer, buffer_size, "%s/%s/manifests/%s.pp",
                loader->modules_path, module_name, manifest_path);
        
        puppet_free(module_name);
        puppet_free(manifest_path);
    }
    
    /* Check if the file exists */
    struct stat st;
    if (stat(path_buffer, &st) != 0) {
        return false;
    }
    
    return true;
}

puppet_stmt_t *puppet_loader_load_class(puppet_loader_t *loader,
                                        const char *class_name) {
    if (!loader || !class_name) return NULL;
    
    /* Check if already loaded */
    for (size_t i = 0; i < loader->loaded_classes.count; i++) {
        if (strcmp(loader->loaded_classes.class_names[i], class_name) == 0) {
            return loader->loaded_classes.class_defs[i];
        }
    }
    
    /* Resolve the class path */
    char path_buffer[1024];
    if (!puppet_loader_resolve_class_path(loader, class_name, path_buffer, sizeof(path_buffer))) {
        fprintf(stderr, "Error: Cannot resolve class '%s' to a file path\n", class_name);
        return NULL;
    }
    
    /* Load and parse the manifest */
    puppet_program_t *program = puppet_loader_load_manifest(loader, path_buffer);
    if (!program) {
        fprintf(stderr, "Error: Failed to load class '%s' from %s\n", class_name, path_buffer);
        return NULL;
    }
    
    /* Find the class definition in the parsed program */
    puppet_stmt_t *class_def = NULL;
    for (size_t i = 0; i < program->statements.count; i++) {
        puppet_stmt_t *stmt = program->statements.stmts[i];
        if (stmt && stmt->type == PUPPET_STMT_CLASS_DEF) {
            /* Check if this is the class we're looking for */
            /* Note: We might need to handle namespaced classes differently */
            /* const char *def_name = stmt->data.class_def.name.data; */ /* Currently unused */
            
            /* For namespaced classes, we might have a mismatch between
             * the requested name (apache::vhost) and the defined name (vhost)
             * in the file. For now, we'll accept any class definition. */
            class_def = stmt;
            break;
        }
    }
    
    if (!class_def) {
        fprintf(stderr, "Error: No class definition found in %s\n", path_buffer);
        puppet_program_destroy(program);
        return NULL;
    }
    
    /* Cache the loaded class */
    if (loader->loaded_classes.count >= loader->loaded_classes.capacity) {
        loader->loaded_classes.capacity *= 2;
        loader->loaded_classes.class_names = puppet_realloc(loader->loaded_classes.class_names,
            loader->loaded_classes.capacity * sizeof(char*));
        loader->loaded_classes.class_defs = puppet_realloc(loader->loaded_classes.class_defs,
            loader->loaded_classes.capacity * sizeof(puppet_stmt_t*));
    }
    
    loader->loaded_classes.class_names[loader->loaded_classes.count] = puppet_strdup(class_name);
    loader->loaded_classes.class_defs[loader->loaded_classes.count] = class_def;
    loader->loaded_classes.count++;
    
    return class_def;
}

puppet_program_t *puppet_loader_load_manifest(puppet_loader_t *loader,
                                              const char *file_path) {
    (void)loader; /* Currently unused */
    if (!file_path) return NULL;

    /* Parse the file with tree-sitter */
    puppet_stmt_list_t *stmts = puppet_ts_parse_file(file_path);
    if (!stmts) {
        fprintf(stderr, "Error: Failed to parse '%s'\n", file_path);
        return NULL;
    }

    /* Wrap in program structure */
    puppet_program_t *program = puppet_calloc(1, sizeof(puppet_program_t));
    program->statements = *stmts;
    puppet_free(stmts);

    return program;
}

puppet_program_t *puppet_loader_load_site(puppet_loader_t *loader) {
    if (!loader) return NULL;
    
    char site_path[1024];
    snprintf(site_path, sizeof(site_path), "%s/site.pp", loader->manifests_path);
    
    /* Check if site.pp exists */
    struct stat st;
    if (stat(site_path, &st) != 0) {
        /* site.pp is optional, not an error if missing */
        return NULL;
    }
    
    return puppet_loader_load_manifest(loader, site_path);
}

bool puppet_loader_include_class(puppet_loader_t *loader,
                                 const char *class_name,
                                 puppet_env_t *env) {
    if (!loader || !class_name || !env) return false;
    
    /* Load the class */
    puppet_stmt_t *class_def = puppet_loader_load_class(loader, class_name);
    if (!class_def) {
        fprintf(stderr, "Error: Cannot include class '%s'\n", class_name);
        return false;
    }
    
    /* Execute the class definition */
    printf("Including class: %s\n", class_name);

    /* Create a new scope for the class */
    puppet_scope_t *class_scope = puppet_scope_create(env->current_scope, class_name);
    puppet_scope_push(env, class_scope);

    /* Set class scope in environment for enhanced variable lookup */
    puppet_scope_t *old_class_scope = env->class_scope;
    env->class_scope = class_scope;

    /* Process class parameters and set default values */
    for (size_t i = 0; i < class_def->data.class_def.params.count; i++) {
        puppet_param_t *param = &class_def->data.class_def.params.params[i];
        const char *param_name = param->name.data;

        if (param->default_value) {
            /* Evaluate default value and set in class scope */
            puppet_value_t *default_val = puppet_eval_expr(param->default_value, env);
            puppet_scope_set_var(class_scope, param_name, default_val);
        } else {
            /* Set parameter to undef if no default provided */
            puppet_value_t *undef_val = puppet_value_create_undef();
            puppet_scope_set_var(class_scope, param_name, undef_val);
        }
    }

    /* Execute the class body */
    puppet_exec_stmt_list(&class_def->data.class_def.body, env);

    /* Restore old class scope */
    env->class_scope = old_class_scope;

    /* Pop the class scope */
    puppet_scope_t *old_scope = puppet_scope_pop(env);
    puppet_scope_destroy(old_scope);
    
    return true;
}

bool puppet_loader_is_class_loaded(puppet_loader_t *loader,
                                   const char *class_name) {
    if (!loader || !class_name) return false;
    
    for (size_t i = 0; i < loader->loaded_classes.count; i++) {
        if (strcmp(loader->loaded_classes.class_names[i], class_name) == 0) {
            return true;
        }
    }
    
    return false;
}

void puppet_loader_set_modules_path(puppet_loader_t *loader,
                                    const char *modules_path) {
    if (!loader || !modules_path) return;
    
    puppet_free(loader->modules_path);
    loader->modules_path = puppet_strdup(modules_path);
}

void puppet_loader_set_manifests_path(puppet_loader_t *loader,
                                      const char *manifests_path) {
    if (!loader || !manifests_path) return;
    
    puppet_free(loader->manifests_path);
    loader->manifests_path = puppet_strdup(manifests_path);
}