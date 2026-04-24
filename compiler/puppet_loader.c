/**
 * @file puppet_loader.c
 * @brief Implementation of the Puppet module loading system
 */

#include "puppet_loader.h"
#include "puppet_ts_parser.h"
#include "puppet_memory.h"
#include "puppet_interpreter.h"
#include "puppet_deadcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

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

    /* Initialize custom function cache */
    loader->custom_functions.capacity = 32;
    loader->custom_functions.func_names = puppet_calloc(loader->custom_functions.capacity, sizeof(char*));
    loader->custom_functions.func_exists = puppet_calloc(loader->custom_functions.capacity, sizeof(bool));
    loader->custom_functions.count = 0;

    /* Initialize parsed manifest cache */
    loader->parsed_manifests.capacity = 64;
    loader->parsed_manifests.file_paths = puppet_calloc(loader->parsed_manifests.capacity, sizeof(char*));
    loader->parsed_manifests.programs = puppet_calloc(loader->parsed_manifests.capacity, sizeof(puppet_program_t*));
    loader->parsed_manifests.count = 0;

    /* Initialize thread synchronization */
    pthread_mutex_init(&loader->cache_mutex, NULL);

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

    /* Clean up custom function cache */
    for (size_t i = 0; i < loader->custom_functions.count; i++) {
        puppet_free(loader->custom_functions.func_names[i]);
    }
    puppet_free(loader->custom_functions.func_names);
    puppet_free(loader->custom_functions.func_exists);

    /* Clean up parsed manifest cache */
    for (size_t i = 0; i < loader->parsed_manifests.count; i++) {
        puppet_free(loader->parsed_manifests.file_paths[i]);
        /* Note: programs are owned by the cache, but we don't destroy them
         * because class_defs point into them. This is a memory trade-off
         * for performance. */
    }
    puppet_free(loader->parsed_manifests.file_paths);
    puppet_free(loader->parsed_manifests.programs);

    /* Destroy thread synchronization */
    pthread_mutex_destroy(&loader->cache_mutex);

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

    /* Strip leading :: if present (top-level scope indicator) */
    const char *name = class_name;
    if (strncmp(name, "::", 2) == 0) {
        name = class_name + 2;
    }

    /* Handle simple class names (no :: separator) */
    const char *separator = strstr(name, "::");
    if (!separator) {
        /* Simple class: modules/classname/manifests/init.pp */
        snprintf(path_buffer, buffer_size, "%s/%s/manifests/init.pp",
                loader->modules_path, name);
    } else {
        /* Namespaced class: modules/module/manifests/subclass.pp */
        /* Extract module name (part before first ::) */
        size_t module_len = separator - name;
        char *module_name = puppet_malloc(module_len + 1);
        strncpy(module_name, name, module_len);
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

    /* Check if already loaded (thread-safe) */
    pthread_mutex_lock(&loader->cache_mutex);
    for (size_t i = 0; i < loader->loaded_classes.count; i++) {
        if (strcmp(loader->loaded_classes.class_names[i], class_name) == 0) {
            puppet_stmt_t *cached = loader->loaded_classes.class_defs[i];
            pthread_mutex_unlock(&loader->cache_mutex);
            return cached;
        }
    }
    pthread_mutex_unlock(&loader->cache_mutex);
    
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
        /* Note: do NOT destroy program here - it's owned by the
         * parsed_manifests cache in puppet_loader_load_manifest. */
        return NULL;
    }
    
    /* Cache the loaded class (thread-safe) */
    pthread_mutex_lock(&loader->cache_mutex);

    /* Double-check: another thread may have loaded while we were parsing */
    for (size_t i = 0; i < loader->loaded_classes.count; i++) {
        if (strcmp(loader->loaded_classes.class_names[i], class_name) == 0) {
            puppet_stmt_t *cached = loader->loaded_classes.class_defs[i];
            pthread_mutex_unlock(&loader->cache_mutex);
            return cached;
        }
    }

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
    pthread_mutex_unlock(&loader->cache_mutex);

    return class_def;
}

puppet_stmt_t *puppet_loader_load_define(puppet_loader_t *loader,
                                         const char *define_name) {
    if (!loader || !define_name) return NULL;

    /* Resolve the define path (same as class path) */
    char path_buffer[1024];
    if (!puppet_loader_resolve_class_path(loader, define_name, path_buffer, sizeof(path_buffer))) {
        /* Not found - this is normal for built-in types, don't log error */
        return NULL;
    }

    /* Load and parse the manifest */
    puppet_program_t *program = puppet_loader_load_manifest(loader, path_buffer);
    if (!program) {
        return NULL;
    }

    /* Find the define statement in the parsed program */
    puppet_stmt_t *define_stmt = NULL;
    for (size_t i = 0; i < program->statements.count; i++) {
        puppet_stmt_t *stmt = program->statements.stmts[i];
        if (stmt && stmt->type == PUPPET_STMT_DEFINE) {
            define_stmt = stmt;
            break;
        }
    }

    if (!define_stmt) {
        /* No define found - maybe it's a class file, not an error.
         * Note: do NOT destroy program here - it's owned by the
         * parsed_manifests cache in puppet_loader_load_manifest. */
        return NULL;
    }

    return define_stmt;
}

puppet_program_t *puppet_loader_load_manifest(puppet_loader_t *loader,
                                              const char *file_path) {
    if (!file_path) return NULL;

    /* Check manifest cache first (thread-safe) */
    if (loader) {
        pthread_mutex_lock(&loader->cache_mutex);
        for (size_t i = 0; i < loader->parsed_manifests.count; i++) {
            if (strcmp(loader->parsed_manifests.file_paths[i], file_path) == 0) {
                puppet_program_t *cached = loader->parsed_manifests.programs[i];
                pthread_mutex_unlock(&loader->cache_mutex);
                return cached;
            }
        }
        pthread_mutex_unlock(&loader->cache_mutex);
    }

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

    /* Add to cache (thread-safe) */
    if (loader) {
        pthread_mutex_lock(&loader->cache_mutex);

        /* Double-check: another thread may have parsed while we were */
        for (size_t i = 0; i < loader->parsed_manifests.count; i++) {
            if (strcmp(loader->parsed_manifests.file_paths[i], file_path) == 0) {
                /* Another thread already cached it - use theirs and destroy our copy */
                puppet_program_t *cached = loader->parsed_manifests.programs[i];
                pthread_mutex_unlock(&loader->cache_mutex);
                puppet_program_destroy(program);
                return cached;
            }
        }

        if (loader->parsed_manifests.count >= loader->parsed_manifests.capacity) {
            loader->parsed_manifests.capacity *= 2;
            loader->parsed_manifests.file_paths = puppet_realloc(
                loader->parsed_manifests.file_paths,
                loader->parsed_manifests.capacity * sizeof(char*));
            loader->parsed_manifests.programs = puppet_realloc(
                loader->parsed_manifests.programs,
                loader->parsed_manifests.capacity * sizeof(puppet_program_t*));
        }
        loader->parsed_manifests.file_paths[loader->parsed_manifests.count] = puppet_strdup(file_path);
        loader->parsed_manifests.programs[loader->parsed_manifests.count] = program;
        loader->parsed_manifests.count++;
        pthread_mutex_unlock(&loader->cache_mutex);
    }

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

    /* Normalize class name by stripping leading :: (top-level scope indicator) */
    const char *normalized_name = class_name;
    if (strncmp(normalized_name, "::", 2) == 0) {
        normalized_name = class_name + 2;
    }

    puppet_deadcode_mark_class_used(env->deadcode, normalized_name);

    /* Check if class is already included - classes are idempotent */
    if (puppet_hash_get(env->class_scopes, normalized_name, strlen(normalized_name))) {
        puppet_debug("Class %s already included, skipping", normalized_name);
        return true;
    }

    /* First, check if class is already registered in the environment
     * (defined in the same file or previously parsed) */
    puppet_stmt_t *class_def = puppet_find_class_def(env, normalized_name);

    /* If not found in registered definitions, try to load from module files */
    if (!class_def) {
        class_def = puppet_loader_load_class(loader, normalized_name);
    }

    if (!class_def) {
        fprintf(stderr, "Error: Cannot include class '%s'\n", class_name);
        return false;
    }

    /* Execute the class definition */
    if (puppet_verbose) fprintf(stderr, "Including class: %s\n", normalized_name);

    /* Handle class inheritance - include parent class first */
    puppet_scope_t *parent_class_scope = NULL;
    if (class_def->data.class_def.inherits && class_def->data.class_def.inherits->data) {
        const char *parent_name = class_def->data.class_def.inherits->data;

        /* Strip leading :: from parent name for lookups */
        const char *parent_lookup_name = parent_name;
        if (strncmp(parent_lookup_name, "::", 2) == 0) {
            parent_lookup_name = parent_name + 2;
        }

        puppet_stmt_t *parent_def = puppet_find_class_def(env, parent_lookup_name);
        if (!parent_def) {
            parent_def = puppet_loader_load_class(loader, parent_lookup_name);
        }

        if (parent_def) {
            parent_class_scope = (puppet_scope_t *)puppet_hash_get(
                env->class_scopes, parent_lookup_name, strlen(parent_lookup_name));
            if (!parent_class_scope) {
                puppet_loader_include_class(loader, parent_lookup_name, env);
                parent_class_scope = (puppet_scope_t *)puppet_hash_get(
                    env->class_scopes, parent_lookup_name, strlen(parent_lookup_name));
            }
        }
    }

    /* Create a new scope for the class, parented by inherited class if any.
     * IMPORTANT: Use node_scope instead of current_scope to avoid dangling pointers
     * when current_scope is a transient define scope that gets destroyed. */
    puppet_scope_t *scope_parent = parent_class_scope ? parent_class_scope : env->node_scope;
    puppet_scope_t *class_scope = puppet_scope_create(scope_parent, normalized_name);
    puppet_scope_push(env, class_scope);

    /* Set class scope in environment for enhanced variable lookup */
    puppet_scope_t *old_class_scope = env->class_scope;
    env->class_scope = class_scope;

    /* Store class scope BEFORE executing body for $class::var lookups */
    puppet_hash_set(env->class_scopes, normalized_name, strlen(normalized_name), (puppet_value_t *)class_scope);

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

    /* Set caller_module_name for hiera interpolation (%{module_name}).
     * Without this, hiera() calls made inside loader-autoloaded classes
     * can't resolve module-relative hierarchy paths. Matches what
     * puppet_include_class_from_def already does on its path. */
    char *old_caller_module = env->caller_module_name;
    const char *mod_sep = strstr(normalized_name, "::");
    if (mod_sep) {
        size_t mlen = mod_sep - normalized_name;
        env->caller_module_name = puppet_malloc(mlen + 1);
        memcpy(env->caller_module_name, normalized_name, mlen);
        env->caller_module_name[mlen] = '\0';
    } else {
        env->caller_module_name = puppet_strdup(normalized_name);
    }

    /* Execute the class body */
    puppet_exec_stmt_list(&class_def->data.class_def.body, env);

    /* Restore caller_module_name */
    puppet_free(env->caller_module_name);
    env->caller_module_name = old_caller_module;

    /* Register in catalog so Class['x'] references and agent-side class
     * queries see it. Without this the class runs but is invisible to
     * the catalog validator, producing false "Could not find resource
     * Class[x]" errors for classes loaded via the module loader. */
    if (env->build_catalog && env->catalog) {
        puppet_catalog_add_class(env->catalog, normalized_name);
    }

    /* Restore old class scope */
    env->class_scope = old_class_scope;

    /* Pop the class scope but don't destroy (it's stored in class_scopes) */
    (void)puppet_scope_pop(env);

    return true;
}

bool puppet_loader_is_class_loaded(puppet_loader_t *loader,
                                   const char *class_name) {
    if (!loader || !class_name) return false;

    pthread_mutex_lock(&loader->cache_mutex);
    for (size_t i = 0; i < loader->loaded_classes.count; i++) {
        if (strcmp(loader->loaded_classes.class_names[i], class_name) == 0) {
            pthread_mutex_unlock(&loader->cache_mutex);
            return true;
        }
    }
    pthread_mutex_unlock(&loader->cache_mutex);

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

bool puppet_loader_has_custom_function(puppet_loader_t *loader,
                                       const char *func_name) {
    if (!loader || !func_name || !loader->modules_path) return false;

    /* Check cache first (thread-safe) */
    pthread_mutex_lock(&loader->cache_mutex);
    for (size_t i = 0; i < loader->custom_functions.count; i++) {
        if (strcmp(loader->custom_functions.func_names[i], func_name) == 0) {
            bool cached = loader->custom_functions.func_exists[i];
            pthread_mutex_unlock(&loader->cache_mutex);
            return cached;
        }
    }
    pthread_mutex_unlock(&loader->cache_mutex);

    /* Open modules directory */
    DIR *modules_dir = opendir(loader->modules_path);
    if (!modules_dir) return false;

    struct dirent *module_entry;
    bool found = false;

    /* Iterate over all modules */
    while ((module_entry = readdir(modules_dir)) != NULL && !found) {
        /* Skip . and .. */
        if (module_entry->d_name[0] == '.') continue;

        /* Check both Puppet 3 and Puppet 4+ function paths */
        char path[1024];
        struct stat st;

        /* Puppet 3 style: lib/puppet/parser/functions/<name>.rb */
        snprintf(path, sizeof(path), "%s/%s/lib/puppet/parser/functions/%s.rb",
                 loader->modules_path, module_entry->d_name, func_name);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            found = true;
            puppet_debug("Found custom function: %s in %s", func_name, path);
            break;
        }

        /* Puppet 4+ style: lib/puppet/functions/<name>.rb */
        snprintf(path, sizeof(path), "%s/%s/lib/puppet/functions/%s.rb",
                 loader->modules_path, module_entry->d_name, func_name);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            found = true;
            puppet_debug("Found custom function: %s in %s", func_name, path);
            break;
        }

        /* Puppet 4+ namespaced style: lib/puppet/functions/<module>/<name>.rb */
        snprintf(path, sizeof(path), "%s/%s/lib/puppet/functions/%s/%s.rb",
                 loader->modules_path, module_entry->d_name, module_entry->d_name, func_name);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            found = true;
            puppet_debug("Found custom function: %s in %s", func_name, path);
            break;
        }
    }

    closedir(modules_dir);

    /* Add to cache (thread-safe) */
    pthread_mutex_lock(&loader->cache_mutex);

    /* Double-check: another thread may have searched while we were */
    for (size_t i = 0; i < loader->custom_functions.count; i++) {
        if (strcmp(loader->custom_functions.func_names[i], func_name) == 0) {
            bool cached = loader->custom_functions.func_exists[i];
            pthread_mutex_unlock(&loader->cache_mutex);
            return cached;
        }
    }

    if (loader->custom_functions.count >= loader->custom_functions.capacity) {
        loader->custom_functions.capacity *= 2;
        loader->custom_functions.func_names = puppet_realloc(
            loader->custom_functions.func_names,
            loader->custom_functions.capacity * sizeof(char*));
        loader->custom_functions.func_exists = puppet_realloc(
            loader->custom_functions.func_exists,
            loader->custom_functions.capacity * sizeof(bool));
    }
    loader->custom_functions.func_names[loader->custom_functions.count] = puppet_strdup(func_name);
    loader->custom_functions.func_exists[loader->custom_functions.count] = found;
    loader->custom_functions.count++;
    pthread_mutex_unlock(&loader->cache_mutex);

    return found;
}