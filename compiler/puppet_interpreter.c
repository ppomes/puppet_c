#include "puppet_interpreter.h"
#include "puppet_erb.h"
#include "puppet_stdlib.h"
#include "puppet_loader.h"
#include "puppet_memory.h"
#include "puppet_hiera.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <regex.h>

/* Global verbose flag */
bool puppet_verbose = false;

/* Forward declarations for node definition management */
static int puppet_register_node_def(puppet_env_t *env, puppet_stmt_t *node_def);
static puppet_stmt_t *puppet_find_matching_node(puppet_env_t *env, const char *certname);
static void puppet_exec_node_for_certname(puppet_stmt_t *node_stmt, const char *certname, puppet_env_t *env);

/**
 * Automatic Parameter Lookup (APL) for class parameters.
 * Looks up class_name::param_name in Hiera data providers and module-specific data.
 * Returns the found value or NULL if not found.
 */
static puppet_value_t *puppet_apl_lookup(const char *class_name, const char *param_name, puppet_env_t *env) {
    if (!class_name || !param_name || !env) return NULL;

    /* Build the lookup key: classname::paramname */
    size_t key_len = strlen(class_name) + 2 + strlen(param_name) + 1;
    char *key = puppet_malloc(key_len);
    snprintf(key, key_len, "%s::%s", class_name, param_name);

    /* Look up in data providers (Hiera) */
    puppet_value_t *result = NULL;
    for (size_t i = 0; i < env->data_provider_count; i++) {
        puppet_data_provider_t *provider = env->data_providers[i];
        if (provider && provider->lookup) {
            result = provider->lookup(key, env, provider->data);
            if (result) {
                puppet_debug("APL: Found %s via provider", key);
                break;
            }
        }
    }

    /* If not found, try module-specific hieradata files */
    if (!result) {
        /* Extract module name (first component of class_name) */
        const char *sep = strchr(class_name, ':');
        char *module_name;
        if (sep) {
            size_t len = sep - class_name;
            module_name = puppet_malloc(len + 1);
            strncpy(module_name, class_name, len);
            module_name[len] = '\0';
        } else {
            module_name = puppet_strdup(class_name);
        }

        /* Get environment name from scope (try jbossenv, then env) */
        const char *env_name = NULL;
        puppet_value_t *env_val = puppet_scope_get_var(env->current_scope, "jbossenv", true);
        if (env_val && env_val->type == PUPPET_VALUE_STRING) {
            env_name = env_val->data.string.data;
        }
        if (!env_name) {
            env_val = puppet_scope_get_var(env->current_scope, "environment", true);
            if (env_val && env_val->type == PUPPET_VALUE_STRING) {
                env_name = env_val->data.string.data;
            }
        }

        /* Try various hieradata paths - including preprod-relative paths */
        char path[1024];
        const char *hieradata_dirs[] = {
            "hieradata", "data", "hieralocal", "hieradata/local",
            "preprod/hieradata", "preprod/hieralocal", "preprod/hieradata/local"
        };

        for (size_t i = 0; i < sizeof(hieradata_dirs)/sizeof(hieradata_dirs[0]) && !result; i++) {
            /* Try module/env.yaml */
            if (env_name) {
                snprintf(path, sizeof(path), "%s/%s/%s.yaml", hieradata_dirs[i], module_name, env_name);
                puppet_value_t *data = puppet_hiera_load_yaml(path);
                if (data && data->type == PUPPET_VALUE_HASH) {
                    result = puppet_hash_get(data->data.hash, key, strlen(key));
                    if (result) {
                        result = puppet_value_copy(result);
                        puppet_debug("APL: Found %s in %s", key, path);
                    }
                    puppet_value_destroy(data);
                }
            }

            /* Try module/global.yaml */
            if (!result) {
                snprintf(path, sizeof(path), "%s/%s/global.yaml", hieradata_dirs[i], module_name);
                puppet_value_t *data = puppet_hiera_load_yaml(path);
                if (data && data->type == PUPPET_VALUE_HASH) {
                    result = puppet_hash_get(data->data.hash, key, strlen(key));
                    if (result) {
                        result = puppet_value_copy(result);
                        puppet_debug("APL: Found %s in %s", key, path);
                    }
                    puppet_value_destroy(data);
                }
            }
        }

        puppet_free(module_name);
    }

    puppet_free(key);
    return result;
}

// Helper function to convert value to string
/* Forward declaration for recursive use */
static void puppet_value_to_string_buffer(puppet_value_t *value, char *buf, size_t *pos, size_t max_len);

static const char *puppet_value_to_string(puppet_value_t *value) {
    if (!value) return "";

    static char buffer[4096];  // Increased buffer size for complex values

    switch (value->type) {
        case PUPPET_VALUE_STRING:
            return value->data.string.data;
        case PUPPET_VALUE_NUMBER:
            snprintf(buffer, sizeof(buffer), "%g", value->data.number);
            return buffer;
        case PUPPET_VALUE_BOOL:
            return value->data.boolean ? "true" : "false";
        case PUPPET_VALUE_UNDEF:
            return "";
        case PUPPET_VALUE_ARRAY:
        case PUPPET_VALUE_HASH: {
            size_t pos = 0;
            puppet_value_to_string_buffer(value, buffer, &pos, sizeof(buffer) - 1);
            buffer[pos] = '\0';
            return buffer;
        }
        default:
            return "";
    }
}

/* Helper function to build string representation of complex values */
static void puppet_value_to_string_buffer(puppet_value_t *value, char *buf, size_t *pos, size_t max_len) {
    if (!value || *pos >= max_len) return;

    switch (value->type) {
        case PUPPET_VALUE_STRING: {
            size_t len = value->data.string.len;
            if (*pos + len > max_len) len = max_len - *pos;
            memcpy(buf + *pos, value->data.string.data, len);
            *pos += len;
            break;
        }
        case PUPPET_VALUE_NUMBER: {
            int written = snprintf(buf + *pos, max_len - *pos, "%g", value->data.number);
            if (written > 0) *pos += (size_t)written;
            break;
        }
        case PUPPET_VALUE_BOOL:
            if (value->data.boolean) {
                if (*pos + 4 <= max_len) { memcpy(buf + *pos, "true", 4); *pos += 4; }
            } else {
                if (*pos + 5 <= max_len) { memcpy(buf + *pos, "false", 5); *pos += 5; }
            }
            break;
        case PUPPET_VALUE_UNDEF:
            break;
        case PUPPET_VALUE_ARRAY: {
            if (*pos < max_len) buf[(*pos)++] = '[';
            if (value->data.array) {
                for (size_t i = 0; i < value->data.array->count && *pos < max_len; i++) {
                    if (i > 0) {
                        if (*pos + 2 <= max_len) { memcpy(buf + *pos, ", ", 2); *pos += 2; }
                    }
                    puppet_value_to_string_buffer(value->data.array->items[i], buf, pos, max_len);
                }
            }
            if (*pos < max_len) buf[(*pos)++] = ']';
            break;
        }
        case PUPPET_VALUE_HASH: {
            if (*pos < max_len) buf[(*pos)++] = '{';
            if (value->data.hash) {
                bool first = true;
                for (size_t i = 0; i < value->data.hash->bucket_count && *pos < max_len; i++) {
                    puppet_hash_entry_t *entry = value->data.hash->buckets[i];
                    while (entry && *pos < max_len) {
                        if (!first) {
                            if (*pos + 2 <= max_len) { memcpy(buf + *pos, ", ", 2); *pos += 2; }
                        }
                        first = false;
                        /* Key */
                        size_t key_len = strlen(entry->key.data);
                        if (*pos + key_len > max_len) key_len = max_len - *pos;
                        memcpy(buf + *pos, entry->key.data, key_len);
                        *pos += key_len;
                        /* Arrow */
                        if (*pos + 4 <= max_len) { memcpy(buf + *pos, " => ", 4); *pos += 4; }
                        /* Value */
                        puppet_value_to_string_buffer(entry->value, buf, pos, max_len);
                        entry = entry->next;
                    }
                }
            }
            if (*pos < max_len) buf[(*pos)++] = '}';
            break;
        }
        default:
            break;
    }
}

puppet_env_t *puppet_env_create(void) {
    puppet_env_t *env = puppet_calloc(1, sizeof(puppet_env_t));
    env->global_scope = puppet_scope_create(NULL, "global");
    env->current_scope = env->global_scope;
    env->stack_capacity = 16;
    env->scope_stack = puppet_calloc(env->stack_capacity, sizeof(puppet_scope_t*));
    env->stack_depth = 0;
    env->loader = NULL;  /* Loader is optional, set separately */
    env->node_name = NULL;  /* No node filtering by default */
    env->execute_all_nodes = false;
    env->node_matched = false;
    env->default_node = NULL;
    
    /* Initialize enhanced variable system */
    env->data_provider_capacity = 4;
    env->data_providers = puppet_calloc(env->data_provider_capacity, sizeof(puppet_data_provider_t*));
    env->data_provider_count = 0;
    env->node_scope = puppet_scope_create(env->global_scope, "node");
    env->class_scope = NULL;  /* Set when entering class context */
    
    /* Initialize class definition registry */
    env->class_def_capacity = 4;
    env->class_definitions = puppet_calloc(env->class_def_capacity, sizeof(puppet_stmt_t*));
    env->class_def_count = 0;

    /* Initialize node definition registry (for facts_db iteration mode) */
    env->node_def_capacity = 4;
    env->node_definitions = puppet_calloc(env->node_def_capacity, sizeof(puppet_stmt_t*));
    env->node_def_count = 0;
    env->defer_node_execution = false;

    /* Initialize class scope registry for $class::var lookups */
    env->class_scopes = puppet_calloc(1, sizeof(puppet_hash_t));
    env->class_scopes->bucket_count = 32;
    env->class_scopes->buckets = puppet_calloc(env->class_scopes->bucket_count, sizeof(puppet_hash_entry_t*));

    /* Initialize facts database */
    env->facts_db = NULL;
    
    /* Initialize resource catalog for duplicate detection */
    env->resource_catalog = puppet_calloc(1, sizeof(puppet_hash_t));
    env->resource_catalog->bucket_count = 64;  /* Start with reasonable size */
    env->resource_catalog->buckets = puppet_calloc(env->resource_catalog->bucket_count, sizeof(puppet_hash_entry_t*));

    /* Initialize virtual resources storage */
    env->virtual_resources = puppet_calloc(1, sizeof(puppet_hash_t));
    env->virtual_resources->bucket_count = 64;
    env->virtual_resources->buckets = puppet_calloc(env->virtual_resources->bucket_count, sizeof(puppet_hash_entry_t*));

    /* Initialize template output */
    env->template_output_target = NULL;
    env->template_output_found = false;
    
    /* Initialize core function support */
    env->defined_resources = puppet_calloc(1, sizeof(puppet_hash_t));
    env->defined_resources->bucket_count = 64;
    env->defined_resources->buckets = puppet_calloc(env->defined_resources->bucket_count, sizeof(puppet_hash_entry_t*));
    env->current_tags = NULL;  /* Initialized when first tag is added */
    env->compilation_failed = false;
    env->failure_message = NULL;

    /* Initialize output control */
    env->verbose = puppet_verbose;  /* Inherit from global flag */

    /* Initialize catalog building (disabled by default) */
    env->catalog = NULL;
    env->build_catalog = false;

    /* Register Hiera data provider */
    puppet_hiera_register_provider(env, "data");

    return env;
}

void puppet_env_set_verbose(puppet_env_t *env, bool verbose) {
    if (env) {
        env->verbose = verbose;
    }
    puppet_verbose = verbose;  /* Also set global flag */
}

void puppet_env_destroy(puppet_env_t *env) {
    if (!env) return;
    
    // Clean up scope stack
    while (env->stack_depth > 0) {
        puppet_scope_pop(env);
    }
    
    // Clean up scopes
    puppet_scope_destroy(env->global_scope);
    if (env->node_scope) puppet_scope_destroy(env->node_scope);
    if (env->class_scope) puppet_scope_destroy(env->class_scope);
    
    // Clean up data providers
    for (size_t i = 0; i < env->data_provider_count; i++) {
        puppet_data_provider_t *provider = env->data_providers[i];
        if (provider && provider->cleanup) {
            provider->cleanup(provider->data);
        }
        puppet_free(provider->name);
        puppet_free(provider);
    }
    puppet_free(env->data_providers);
    
    // Clean up class definition registry (don't destroy statements, they're owned by AST)
    puppet_free(env->class_definitions);

    // Clean up node definition registry (don't destroy statements, they're owned by AST)
    puppet_free(env->node_definitions);

    // Clean up class scopes registry
    if (env->class_scopes) {
        for (size_t i = 0; i < env->class_scopes->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->class_scopes->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                // Destroy the stored scope
                puppet_scope_destroy((puppet_scope_t *)entry->value);
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->class_scopes->buckets);
        puppet_free(env->class_scopes);
    }

    // Clean up facts database
    if (env->facts_db) {
        puppet_facts_db_destroy(env->facts_db);
    }
    
    // Clean up resource catalog
    if (env->resource_catalog) {
        /* Clean up resource catalog */
        for (size_t i = 0; i < env->resource_catalog->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->resource_catalog->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->resource_catalog->buckets);
        puppet_free(env->resource_catalog);
    }

    // Clean up virtual resources
    if (env->virtual_resources) {
        for (size_t i = 0; i < env->virtual_resources->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->virtual_resources->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                /* Note: value points to AST stmt, don't free it here */
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->virtual_resources->buckets);
        puppet_free(env->virtual_resources);
    }

    puppet_free(env->scope_stack);
    puppet_free(env->node_name);
    puppet_free(env->template_output_target);
    
    /* Clean up defined resources hash */
    if (env->defined_resources) {
        for (size_t i = 0; i < env->defined_resources->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->defined_resources->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
        }
        puppet_free(env->defined_resources->buckets);
        puppet_free(env->defined_resources);
    }
    
    /* Clean up tags */
    if (env->current_tags) {
        puppet_value_destroy(env->current_tags);
    }
    
    /* Clean up failure message */
    puppet_free(env->failure_message);

    /* Note: catalog is NOT destroyed here - caller owns it after puppet_env_get_catalog() */
    /* If catalog was never retrieved, it will be leaked - caller should always get it */

    puppet_free(env);
}

puppet_scope_t *puppet_scope_create(puppet_scope_t *parent, const char *name) {
    puppet_scope_t *scope = puppet_calloc(1, sizeof(puppet_scope_t));
    scope->parent = parent;
    scope->name = puppet_string_create(name ? name : "");
    scope->variables = puppet_calloc(1, sizeof(puppet_hash_t));
    scope->variables->bucket_count = 16;
    scope->variables->buckets = puppet_calloc(scope->variables->bucket_count, sizeof(puppet_hash_entry_t*));
    return scope;
}

void puppet_scope_destroy(puppet_scope_t *scope) {
    if (!scope) return;
    
    // Clean up variables hash table
    for (size_t i = 0; i < scope->variables->bucket_count; i++) {
        puppet_hash_entry_t *entry = scope->variables->buckets[i];
        while (entry) {
            puppet_hash_entry_t *next = entry->next;
            puppet_string_free(entry->key);
            puppet_value_destroy(entry->value);
            puppet_free(entry);
            entry = next;
        }
    }
    puppet_free(scope->variables->buckets);
    puppet_free(scope->variables);
    puppet_string_free(scope->name);
    puppet_free(scope);
}

void puppet_scope_push(puppet_env_t *env, puppet_scope_t *scope) {
    if (env->stack_depth >= env->stack_capacity) {
        env->stack_capacity *= 2;
        env->scope_stack = puppet_realloc(env->scope_stack, env->stack_capacity * sizeof(puppet_scope_t*));
    }
    
    env->scope_stack[env->stack_depth++] = env->current_scope;
    env->current_scope = scope;
}

puppet_scope_t *puppet_scope_pop(puppet_env_t *env) {
    if (env->stack_depth == 0) {
        return env->current_scope;
    }
    
    puppet_scope_t *old_scope = env->current_scope;
    env->current_scope = env->scope_stack[--env->stack_depth];
    return old_scope;
}

void puppet_env_set_var(puppet_env_t *env, const char *name, puppet_value_t *value) {
    puppet_scope_set_var(env->current_scope, name, value);
}

puppet_value_t *puppet_env_get_var(puppet_env_t *env, const char *name) {
    return puppet_scope_get_var(env->current_scope, name, true);
}

void puppet_scope_set_var(puppet_scope_t *scope, const char *name, puppet_value_t *value) {
    puppet_hash_set(scope->variables, name, strlen(name), value);
}

puppet_value_t *puppet_scope_get_var(puppet_scope_t *scope, const char *name, bool recursive) {
    puppet_value_t *value = puppet_hash_get(scope->variables, name, strlen(name));
    
    if (!value && recursive && scope->parent) {
        return puppet_scope_get_var(scope->parent, name, true);
    }
    
    return value;
}

puppet_value_t *puppet_eval_expr(puppet_expr_t *expr, puppet_env_t *env) {
    if (!expr) {
        return puppet_value_create_undef();
    }
    
    switch (expr->type) {
        case PUPPET_EXPR_VALUE:
            // Return a copy of the value to avoid double-free issues
            switch (expr->data.value->type) {
                case PUPPET_VALUE_UNDEF:
                    return puppet_value_create_undef();
                case PUPPET_VALUE_BOOL:
                    return puppet_value_create_bool(expr->data.value->data.boolean);
                case PUPPET_VALUE_NUMBER:
                    return puppet_value_create_number(expr->data.value->data.number);
                case PUPPET_VALUE_STRING:
                    return puppet_value_create_string(expr->data.value->data.string.data,
                                                    expr->data.value->data.string.len);
                case PUPPET_VALUE_ARRAY:
                case PUPPET_VALUE_HASH:
                    return puppet_value_copy(expr->data.value);
                default:
                    return puppet_value_create_undef();
            }
            
        case PUPPET_EXPR_VARIABLE:
            return puppet_eval_variable(expr->data.variable.data, env);
            
        case PUPPET_EXPR_BINOP: {
            puppet_value_t *left = puppet_eval_expr(expr->data.binop.left, env);
            puppet_value_t *right = puppet_eval_expr(expr->data.binop.right, env);
            puppet_value_t *result = puppet_eval_binop(expr->data.binop.op, left, right);
            puppet_value_destroy(left);
            puppet_value_destroy(right);
            return result;
        }
            
        case PUPPET_EXPR_UNOP: {
            puppet_value_t *operand = puppet_eval_expr(expr->data.unop.expr, env);
            puppet_value_t *result = puppet_eval_unop(expr->data.unop.op, operand);
            puppet_value_destroy(operand);
            return result;
        }
            
        case PUPPET_EXPR_FUNCALL: {
            // Handle built-in functions
            const char *func_name = expr->data.funcall.name.data;
            
            // Template function
            if (strcmp(func_name, "template") == 0) {
                return puppet_func_template(&expr->data.funcall.args, env);
            }
            // Core logging functions
            else if (strcmp(func_name, "fail") == 0) {
                return puppet_func_fail(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "notice") == 0) {
                return puppet_func_notice(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "info") == 0) {
                return puppet_func_info(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "warning") == 0) {
                return puppet_func_warning(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "err") == 0) {
                return puppet_func_err(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "debug") == 0) {
                return puppet_func_debug(&expr->data.funcall.args, env);
            }
            // Resource functions
            else if (strcmp(func_name, "defined") == 0) {
                return puppet_func_defined(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "realize") == 0) {
                return puppet_func_realize(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "tag") == 0) {
                return puppet_func_tag(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "tagged") == 0) {
                return puppet_func_tagged(&expr->data.funcall.args, env);
            }
            // Data lookup functions
            else if (strcmp(func_name, "lookup") == 0) {
                return puppet_func_lookup(&expr->data.funcall.args, env);
            }
            // String manipulation functions
            else if (strcmp(func_name, "split") == 0) {
                return puppet_func_split(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "join") == 0) {
                return puppet_func_join(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "downcase") == 0) {
                return puppet_func_downcase(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "upcase") == 0) {
                return puppet_func_upcase(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "strip") == 0) {
                return puppet_func_strip(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "lstrip") == 0) {
                return puppet_func_lstrip(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "rstrip") == 0) {
                return puppet_func_rstrip(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "chomp") == 0) {
                return puppet_func_chomp(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "chop") == 0) {
                return puppet_func_chop(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "capitalize") == 0) {
                return puppet_func_capitalize(&expr->data.funcall.args, env);
            }
            // Inspection functions
            else if (strcmp(func_name, "size") == 0 || strcmp(func_name, "length") == 0) {
                return puppet_func_size(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "empty") == 0) {
                return puppet_func_empty(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "keys") == 0) {
                return puppet_func_keys(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "values") == 0) {
                return puppet_func_values(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "has_key") == 0) {
                return puppet_func_has_key(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "member") == 0) {
                return puppet_func_member(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "reverse") == 0) {
                return puppet_func_reverse(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "unique") == 0) {
                return puppet_func_unique(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "sort") == 0) {
                return puppet_func_sort(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "flatten") == 0) {
                return puppet_func_flatten(&expr->data.funcall.args, env);
            }
            // Array functions
            else if (strcmp(func_name, "concat") == 0) {
                return puppet_func_concat(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "delete") == 0) {
                return puppet_func_delete(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "delete_at") == 0) {
                return puppet_func_delete_at(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "first") == 0) {
                return puppet_func_first(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "last") == 0) {
                return puppet_func_last(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "range") == 0) {
                return puppet_func_range(&expr->data.funcall.args, env);
            }
            // Hash functions
            else if (strcmp(func_name, "merge") == 0) {
                return puppet_func_merge(&expr->data.funcall.args, env);
            }
            // Type checking functions
            else if (strcmp(func_name, "is_string") == 0) {
                return puppet_func_is_string(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "is_array") == 0) {
                return puppet_func_is_array(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "is_hash") == 0) {
                return puppet_func_is_hash(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "is_numeric") == 0 || strcmp(func_name, "is_integer") == 0 || strcmp(func_name, "is_float") == 0) {
                return puppet_func_is_numeric(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "is_bool") == 0 || strcmp(func_name, "is_boolean") == 0) {
                return puppet_func_is_bool(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "abs") == 0) {
                return puppet_func_abs(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "min") == 0) {
                return puppet_func_min(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "max") == 0) {
                return puppet_func_max(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "floor") == 0) {
                return puppet_func_floor(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "ceil") == 0 || strcmp(func_name, "ceiling") == 0) {
                return puppet_func_ceil(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "round") == 0) {
                return puppet_func_round(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "sqrt") == 0) {
                return puppet_func_sqrt(&expr->data.funcall.args, env);
            }
            // Path functions
            else if (strcmp(func_name, "basename") == 0) {
                return puppet_func_basename(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "dirname") == 0) {
                return puppet_func_dirname(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "extname") == 0) {
                return puppet_func_extname(&expr->data.funcall.args, env);
            }
            // Regex functions
            else if (strcmp(func_name, "regsubst") == 0) {
                return puppet_func_regsubst(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "match") == 0) {
                return puppet_func_match(&expr->data.funcall.args, env);
            }
            // Crypto functions
            else if (strcmp(func_name, "sha1") == 0) {
                return puppet_func_sha1(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "md5") == 0) {
                return puppet_func_md5(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "base64") == 0) {
                return puppet_func_base64(&expr->data.funcall.args, env);
            }
            // Iterator functions
            else if (strcmp(func_name, "each") == 0) {
                return puppet_func_each(expr, env);
            }
            else if (strcmp(func_name, "map") == 0) {
                return puppet_func_map(expr, env);
            }
            else if (strcmp(func_name, "filter") == 0 || strcmp(func_name, "select") == 0) {
                return puppet_func_filter(expr, env);
            }
            else if (strcmp(func_name, "reduce") == 0) {
                return puppet_func_reduce(expr, env);
            }

            // Validation functions (legacy stdlib)
            else if (strcmp(func_name, "validate_re") == 0) {
                return puppet_func_validate_re(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "validate_hash") == 0) {
                return puppet_func_validate_hash(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "validate_string") == 0) {
                return puppet_func_validate_string(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "validate_array") == 0) {
                return puppet_func_validate_array(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "validate_bool") == 0) {
                return puppet_func_validate_bool(&expr->data.funcall.args, env);
            }

            // Version comparison
            else if (strcmp(func_name, "versioncmp") == 0) {
                return puppet_func_versioncmp(&expr->data.funcall.args, env);
            }

            // Domain/IP validation
            else if (strcmp(func_name, "is_domain_name") == 0) {
                return puppet_func_is_domain_name(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "is_ip_address") == 0) {
                return puppet_func_is_ip_address(&expr->data.funcall.args, env);
            }

            // Resource creation
            else if (strcmp(func_name, "create_resources") == 0) {
                return puppet_func_create_resources(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "ensure_packages") == 0) {
                return puppet_func_ensure_packages(&expr->data.funcall.args, env);
            }

            // Conversion functions
            else if (strcmp(func_name, "any2array") == 0) {
                return puppet_func_any2array(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "str2bool") == 0) {
                return puppet_func_str2bool(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "bool2str") == 0) {
                return puppet_func_bool2str(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "type") == 0 || strcmp(func_name, "type_of") == 0) {
                return puppet_func_type(&expr->data.funcall.args, env);
            }

            // Random functions
            else if (strcmp(func_name, "fqdn_rand") == 0) {
                return puppet_func_fqdn_rand(&expr->data.funcall.args, env);
            }

            // Type assertion
            else if (strcmp(func_name, "assert_type") == 0) {
                return puppet_func_assert_type(&expr->data.funcall.args, env);
            }

            // Data access
            else if (strcmp(func_name, "dig") == 0) {
                return puppet_func_dig(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "pick") == 0) {
                return puppet_func_pick(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "pick_default") == 0) {
                return puppet_func_pick_default(&expr->data.funcall.args, env);
            }

            // Hiera lookup functions
            else if (strcmp(func_name, "hiera") == 0) {
                return puppet_func_hiera(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "lookup") == 0) {
                return puppet_func_lookup(&expr->data.funcall.args, env);
            }

            // Variable/file access functions
            else if (strcmp(func_name, "getvar") == 0) {
                return puppet_func_getvar(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "file") == 0) {
                return puppet_func_file(&expr->data.funcall.args, env);
            }
            else if (strcmp(func_name, "inline_template") == 0) {
                return puppet_func_inline_template(&expr->data.funcall.args, env);
            }

            else {
                puppet_error("Unknown function: %s", func_name);
                return puppet_value_create_undef();
            }
        }
            
        case PUPPET_EXPR_INTERPOLATED_STRING: {
            // Build interpolated string
            size_t total_len = 0;
            char **parts = puppet_calloc(expr->data.interpolated.count * 2 + 1, sizeof(char*));
            size_t part_count = 0;
            
            // Evaluate all parts
            for (size_t i = 0; i < expr->data.interpolated.count; i++) {
                // Add literal part if present
                if (expr->data.interpolated.parts && expr->data.interpolated.parts[i].data) {
                    parts[part_count] = puppet_strdup(expr->data.interpolated.parts[i].data);
                    total_len += expr->data.interpolated.parts[i].len;
                    part_count++;
                }
                
                // Evaluate expression if present
                if (expr->data.interpolated.exprs && expr->data.interpolated.exprs[i]) {
                    puppet_value_t *val = puppet_eval_expr(expr->data.interpolated.exprs[i], env);
                    const char *str = puppet_value_to_string(val);
                    if (str && *str) {
                        // Make a copy immediately since puppet_value_to_string may use static buffer
                        size_t str_len = strlen(str);
                        parts[part_count] = puppet_malloc(str_len + 1);
                        memcpy(parts[part_count], str, str_len + 1);
                        total_len += str_len;
                        part_count++;
                    }
                    puppet_value_destroy(val);
                }
            }
            
            // Build final string
            char *result = puppet_malloc(total_len + 1);
            size_t pos = 0;
            for (size_t i = 0; i < part_count; i++) {
                if (parts[i]) {
                    size_t len = strlen(parts[i]);
                    memcpy(result + pos, parts[i], len);
                    pos += len;
                    puppet_free(parts[i]);
                }
            }
            result[total_len] = '\0';
            puppet_free(parts);
            
            puppet_value_t *ret = puppet_value_create_string(result, total_len);
            puppet_free(result);
            return ret;
        }

        case PUPPET_EXPR_CONDITIONAL: {
            // Ternary conditional: condition ? then_expr : else_expr
            puppet_value_t *cond = puppet_eval_expr(expr->data.conditional.condition, env);
            bool is_true = false;

            if (cond) {
                if (cond->type == PUPPET_VALUE_BOOL) {
                    is_true = cond->data.boolean;
                } else if (cond->type == PUPPET_VALUE_UNDEF) {
                    is_true = false;
                } else {
                    is_true = true;  // Non-undef, non-false values are truthy
                }
                puppet_value_destroy(cond);
            }

            if (is_true) {
                return puppet_eval_expr(expr->data.conditional.then_expr, env);
            } else {
                return puppet_eval_expr(expr->data.conditional.else_expr, env);
            }
        }

        case PUPPET_EXPR_SELECTOR: {
            // Selector expression: control ? { match1 => val1, match2 => val2, default => valN }
            puppet_value_t *control = puppet_eval_expr(expr->data.selector.control, env);
            puppet_value_t *result = NULL;

            // Check each case for a match
            for (size_t i = 0; i < expr->data.selector.case_count && !result; i++) {
                puppet_value_t *match = puppet_eval_expr(expr->data.selector.cases[i].match, env);

                // Compare control value with match value
                bool is_match = false;
                if (control && match) {
                    if (control->type == match->type) {
                        switch (control->type) {
                            case PUPPET_VALUE_STRING:
                                is_match = (strcmp(control->data.string.data,
                                                  match->data.string.data) == 0);
                                break;
                            case PUPPET_VALUE_NUMBER:
                                is_match = (control->data.number == match->data.number);
                                break;
                            case PUPPET_VALUE_BOOL:
                                is_match = (control->data.boolean == match->data.boolean);
                                break;
                            default:
                                break;
                        }
                    }
                }
                puppet_value_destroy(match);

                if (is_match) {
                    result = puppet_eval_expr(expr->data.selector.cases[i].value, env);
                }
            }

            // If no match found, try default case
            if (!result && expr->data.selector.default_value) {
                result = puppet_eval_expr(expr->data.selector.default_value, env);
            }

            puppet_value_destroy(control);
            return result ? result : puppet_value_create_undef();
        }

        case PUPPET_EXPR_INDEX: {
            /* Array/hash indexing: obj[key] */
            puppet_value_t *obj = puppet_eval_expr(expr->data.index.object, env);
            puppet_value_t *key = puppet_eval_expr(expr->data.index.index, env);
            puppet_value_t *result = NULL;

            if (obj && key) {
                if (obj->type == PUPPET_VALUE_HASH && key->type == PUPPET_VALUE_STRING) {
                    /* Hash access */
                    puppet_value_t *val = puppet_hash_get(obj->data.hash,
                        key->data.string.data, key->data.string.len);
                    result = val ? puppet_value_copy(val) : puppet_value_create_undef();
                } else if (obj->type == PUPPET_VALUE_ARRAY && key->type == PUPPET_VALUE_NUMBER) {
                    /* Array access */
                    size_t idx = (size_t)key->data.number;
                    if (idx < obj->data.array->count) {
                        result = puppet_value_copy(obj->data.array->items[idx]);
                    } else {
                        result = puppet_value_create_undef();
                    }
                } else {
                    result = puppet_value_create_undef();
                }
            } else {
                result = puppet_value_create_undef();
            }

            puppet_value_destroy(obj);
            puppet_value_destroy(key);
            return result;
        }

        case PUPPET_EXPR_RESOURCE_REF: {
            /* Resource reference: Type['title'] -> "Type[title]" string */
            puppet_value_t *title_val = puppet_eval_expr(expr->data.resource_ref.title, env);
            /* Note: puppet_value_to_string returns internal pointer, don't free it */
            const char *title_str = puppet_value_to_string(title_val);

            /* Build reference string: Type[title] */
            size_t type_len = expr->data.resource_ref.type.len;
            size_t title_len = title_str ? strlen(title_str) : 0;
            size_t ref_len = type_len + 1 + title_len + 1; /* Type[title] */

            char *ref_str = puppet_malloc(ref_len + 1);
            snprintf(ref_str, ref_len + 1, "%.*s[%s]",
                     (int)type_len, expr->data.resource_ref.type.data,
                     title_str ? title_str : "");

            puppet_value_t *result = puppet_value_create_string(ref_str, strlen(ref_str));

            puppet_free(ref_str);
            puppet_value_destroy(title_val);
            return result;
        }

        default:
            puppet_warn("Unimplemented expression type: %d", expr->type);
            return puppet_value_create_undef();
    }
}

puppet_value_t *puppet_eval_variable(const char *name, puppet_env_t *env) {
    // Use enhanced lookup chain instead of simple scope lookup
    puppet_value_t *value = puppet_variable_lookup_chain(env, name);

    if (!value) {
        puppet_warn("Undefined variable: %s", name);
        return puppet_value_create_undef();
    }

    // Return a copy to avoid double-free (handles all types including arrays/hashes)
    return puppet_value_copy(value);
}

puppet_value_t *puppet_eval_binop(puppet_binop_t op, puppet_value_t *left, puppet_value_t *right) {
    switch (op) {
        case PUPPET_OP_ADD:
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_number(left->data.number + right->data.number);
            }
            break;
            
        case PUPPET_OP_SUB:
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_number(left->data.number - right->data.number);
            }
            break;
            
        case PUPPET_OP_MUL:
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_number(left->data.number * right->data.number);
            }
            break;
            
        case PUPPET_OP_DIV:
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                if (right->data.number != 0) {
                    return puppet_value_create_number(left->data.number / right->data.number);
                }
            }
            break;
            
        case PUPPET_OP_EQ:
            if (left->type == right->type) {
                switch (left->type) {
                    case PUPPET_VALUE_BOOL:
                        return puppet_value_create_bool(left->data.boolean == right->data.boolean);
                    case PUPPET_VALUE_NUMBER:
                        return puppet_value_create_bool(left->data.number == right->data.number);
                    case PUPPET_VALUE_STRING:
                        return puppet_value_create_bool(
                            left->data.string.len == right->data.string.len &&
                            strcmp(left->data.string.data, right->data.string.data) == 0
                        );
                    default:
                        break;
                }
            }
            return puppet_value_create_bool(false);
            
        case PUPPET_OP_NE:
            if (left->type == right->type) {
                switch (left->type) {
                    case PUPPET_VALUE_BOOL:
                        return puppet_value_create_bool(left->data.boolean != right->data.boolean);
                    case PUPPET_VALUE_NUMBER:
                        return puppet_value_create_bool(left->data.number != right->data.number);
                    case PUPPET_VALUE_STRING:
                        return puppet_value_create_bool(
                            left->data.string.len != right->data.string.len ||
                            strcmp(left->data.string.data, right->data.string.data) != 0
                        );
                    default:
                        break;
                }
            }
            return puppet_value_create_bool(true);
            
        case PUPPET_OP_LT:
            /* Comparisons with undef return false */
            if (left->type == PUPPET_VALUE_UNDEF || right->type == PUPPET_VALUE_UNDEF) {
                return puppet_value_create_bool(false);
            }
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_bool(left->data.number < right->data.number);
            }
            /* String comparison for version strings */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_STRING) {
                return puppet_value_create_bool(
                    strcmp(left->data.string.data, right->data.string.data) < 0);
            }
            /* Mixed string/number - convert string to number */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_NUMBER) {
                double left_num = atof(left->data.string.data);
                return puppet_value_create_bool(left_num < right->data.number);
            }
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_STRING) {
                double right_num = atof(right->data.string.data);
                return puppet_value_create_bool(left->data.number < right_num);
            }
            break;

        case PUPPET_OP_GT:
            if (left->type == PUPPET_VALUE_UNDEF || right->type == PUPPET_VALUE_UNDEF) {
                return puppet_value_create_bool(false);
            }
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_bool(left->data.number > right->data.number);
            }
            /* String comparison for version strings */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_STRING) {
                return puppet_value_create_bool(
                    strcmp(left->data.string.data, right->data.string.data) > 0);
            }
            /* Mixed string/number */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_NUMBER) {
                double left_num = atof(left->data.string.data);
                return puppet_value_create_bool(left_num > right->data.number);
            }
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_STRING) {
                double right_num = atof(right->data.string.data);
                return puppet_value_create_bool(left->data.number > right_num);
            }
            break;

        case PUPPET_OP_LE:
            if (left->type == PUPPET_VALUE_UNDEF || right->type == PUPPET_VALUE_UNDEF) {
                return puppet_value_create_bool(false);
            }
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_bool(left->data.number <= right->data.number);
            }
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_STRING) {
                return puppet_value_create_bool(
                    strcmp(left->data.string.data, right->data.string.data) <= 0);
            }
            /* Mixed string/number */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_NUMBER) {
                double left_num = atof(left->data.string.data);
                return puppet_value_create_bool(left_num <= right->data.number);
            }
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_STRING) {
                double right_num = atof(right->data.string.data);
                return puppet_value_create_bool(left->data.number <= right_num);
            }
            break;

        case PUPPET_OP_GE:
            if (left->type == PUPPET_VALUE_UNDEF || right->type == PUPPET_VALUE_UNDEF) {
                return puppet_value_create_bool(false);
            }
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_bool(left->data.number >= right->data.number);
            }
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_STRING) {
                return puppet_value_create_bool(
                    strcmp(left->data.string.data, right->data.string.data) >= 0);
            }
            /* Mixed string/number */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_NUMBER) {
                double left_num = atof(left->data.string.data);
                return puppet_value_create_bool(left_num >= right->data.number);
            }
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_STRING) {
                double right_num = atof(right->data.string.data);
                return puppet_value_create_bool(left->data.number >= right_num);
            }
            break;

        case PUPPET_OP_AND:
            /* Logical AND - returns true if both are truthy */
            {
                bool left_bool = (left->type == PUPPET_VALUE_BOOL) ? left->data.boolean :
                                 (left->type != PUPPET_VALUE_UNDEF);
                bool right_bool = (right->type == PUPPET_VALUE_BOOL) ? right->data.boolean :
                                  (right->type != PUPPET_VALUE_UNDEF);
                return puppet_value_create_bool(left_bool && right_bool);
            }

        case PUPPET_OP_OR:
            /* Logical OR - returns true if either is truthy */
            {
                bool left_bool = (left->type == PUPPET_VALUE_BOOL) ? left->data.boolean :
                                 (left->type != PUPPET_VALUE_UNDEF);
                bool right_bool = (right->type == PUPPET_VALUE_BOOL) ? right->data.boolean :
                                  (right->type != PUPPET_VALUE_UNDEF);
                return puppet_value_create_bool(left_bool || right_bool);
            }

        case PUPPET_OP_MOD:
            if (left->type == PUPPET_VALUE_NUMBER && right->type == PUPPET_VALUE_NUMBER) {
                if (right->data.number != 0) {
                    return puppet_value_create_number(
                        (int)left->data.number % (int)right->data.number);
                }
            }
            break;

        case PUPPET_OP_IN:
            /* Check if left is in right (array or string) */
            if (right->type == PUPPET_VALUE_ARRAY) {
                for (size_t i = 0; i < right->data.array->count; i++) {
                    puppet_value_t *elem = right->data.array->items[i];
                    if (left->type == elem->type) {
                        if (left->type == PUPPET_VALUE_STRING &&
                            strcmp(left->data.string.data, elem->data.string.data) == 0) {
                            return puppet_value_create_bool(true);
                        }
                        if (left->type == PUPPET_VALUE_NUMBER &&
                            left->data.number == elem->data.number) {
                            return puppet_value_create_bool(true);
                        }
                    }
                }
                return puppet_value_create_bool(false);
            }
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_STRING) {
                return puppet_value_create_bool(
                    strstr(right->data.string.data, left->data.string.data) != NULL);
            }
            break;

        case PUPPET_OP_MATCH:
            /* Regex match =~ */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_STRING) {
                regex_t regex;
                int ret = regcomp(&regex, right->data.string.data, REG_EXTENDED | REG_NOSUB);
                if (ret == 0) {
                    ret = regexec(&regex, left->data.string.data, 0, NULL, 0);
                    regfree(&regex);
                    return puppet_value_create_bool(ret == 0);
                }
            }
            return puppet_value_create_bool(false);

        case PUPPET_OP_NOT_MATCH:
            /* Regex non-match !~ */
            if (left->type == PUPPET_VALUE_STRING && right->type == PUPPET_VALUE_STRING) {
                regex_t regex;
                int ret = regcomp(&regex, right->data.string.data, REG_EXTENDED | REG_NOSUB);
                if (ret == 0) {
                    ret = regexec(&regex, left->data.string.data, 0, NULL, 0);
                    regfree(&regex);
                    return puppet_value_create_bool(ret != 0);
                }
            }
            return puppet_value_create_bool(true);

        default:
            break;
    }

    puppet_warn("Unsupported binary operation: op=%d left_type=%d right_type=%d",
                op, left->type, right->type);
    return puppet_value_create_undef();
}

puppet_value_t *puppet_eval_unop(puppet_unop_t op, puppet_value_t *operand) {
    switch (op) {
        case PUPPET_UNOP_NOT:
            // Convert operand to boolean and negate
            if (operand->type == PUPPET_VALUE_BOOL) {
                return puppet_value_create_bool(!operand->data.boolean);
            } else if (operand->type == PUPPET_VALUE_UNDEF) {
                return puppet_value_create_bool(true);
            }
            return puppet_value_create_bool(false);
            
        case PUPPET_UNOP_MINUS:
            if (operand->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_number(-operand->data.number);
            }
            break;
            
        case PUPPET_UNOP_PLUS:
            if (operand->type == PUPPET_VALUE_NUMBER) {
                return puppet_value_create_number(operand->data.number);
            }
            break;
            
        default:
            break;
    }

    puppet_warn("Unsupported unary operation");
    return puppet_value_create_undef();
}

/* Forward declaration */
static bool puppet_include_class_from_def(puppet_stmt_t *class_def, puppet_env_t *env);

/* ============================================================================
 * Resource Collector Helpers
 * ============================================================================ */

/**
 * Get an attribute value from a virtual resource instance
 */
static puppet_value_t *collector_get_attribute(
    puppet_stmt_t *stmt,
    size_t instance_idx,
    const char *attr_name,
    puppet_env_t *env
) {
    if (!stmt || stmt->type != PUPPET_STMT_RESOURCE) return NULL;
    if (instance_idx >= stmt->data.resource.instance_count) return NULL;

    puppet_resource_instance_t *instance = &stmt->data.resource.instances[instance_idx];

    for (size_t i = 0; i < instance->attr_count; i++) {
        if (strcmp(instance->attributes[i].name.data, attr_name) == 0) {
            return puppet_eval_expr(instance->attributes[i].value, env);
        }
    }
    return NULL;
}

/**
 * Check if a resource matches a collector filter expression
 * Handles: ==, !=, and, or operators with attribute comparisons
 */
static bool collector_matches_filter(
    puppet_expr_t *filter,
    puppet_stmt_t *resource_stmt,
    size_t instance_idx,
    puppet_env_t *env
) {
    if (!filter) return true;  /* No filter = match all */

    switch (filter->type) {
        case PUPPET_EXPR_BINOP: {
            puppet_binop_t op = filter->data.binop.op;

            /* Handle logical operators */
            if (op == PUPPET_OP_AND) {
                bool left = collector_matches_filter(filter->data.binop.left, resource_stmt, instance_idx, env);
                if (!left) return false;
                return collector_matches_filter(filter->data.binop.right, resource_stmt, instance_idx, env);
            }
            if (op == PUPPET_OP_OR) {
                bool left = collector_matches_filter(filter->data.binop.left, resource_stmt, instance_idx, env);
                if (left) return true;
                return collector_matches_filter(filter->data.binop.right, resource_stmt, instance_idx, env);
            }

            /* Handle comparison operators: left should be attribute name (variable) */
            if (op == PUPPET_OP_EQ || op == PUPPET_OP_NE) {
                /* Get attribute name from left side */
                const char *attr_name = NULL;
                if (filter->data.binop.left->type == PUPPET_EXPR_VARIABLE) {
                    attr_name = filter->data.binop.left->data.variable.data;
                } else if (filter->data.binop.left->type == PUPPET_EXPR_VALUE &&
                           filter->data.binop.left->data.value->type == PUPPET_VALUE_STRING) {
                    attr_name = filter->data.binop.left->data.value->data.string.data;
                }

                if (!attr_name) {
                    puppet_warn("Collector filter: left side must be attribute name");
                    return false;
                }

                /* Get the expected value from right side */
                puppet_value_t *expected = puppet_eval_expr(filter->data.binop.right, env);
                if (!expected) return false;

                /* Get the actual attribute value from resource */
                puppet_value_t *actual = collector_get_attribute(resource_stmt, instance_idx, attr_name, env);

                bool result = false;
                if (actual) {
                    puppet_value_t *cmp = puppet_eval_binop(PUPPET_OP_EQ, actual, expected);
                    bool is_equal = (cmp && cmp->type == PUPPET_VALUE_BOOL && cmp->data.boolean);
                    if (cmp) puppet_value_destroy(cmp);

                    result = (op == PUPPET_OP_EQ) ? is_equal : !is_equal;
                    puppet_value_destroy(actual);
                } else {
                    /* Attribute not found: == fails, != succeeds */
                    result = (op == PUPPET_OP_NE);
                }

                puppet_value_destroy(expected);
                return result;
            }
            break;
        }

        default:
            puppet_warn("Collector filter: unsupported expression type %d", filter->type);
            break;
    }

    return false;
}

/**
 * Execute a resource collector - realize matching virtual resources
 */
static void puppet_exec_collector(puppet_stmt_t *stmt, puppet_env_t *env) {
    if (!stmt || stmt->type != PUPPET_STMT_RESOURCE_COLLECTOR) return;
    if (!env->virtual_resources) return;

    const char *collect_type = stmt->data.collector.type.data;
    puppet_expr_t *filter = stmt->data.collector.search_expr;

    /* Build lowercase type prefix for matching (e.g., "user[") */
    size_t type_len = strlen(collect_type);
    char *type_lower = puppet_malloc(type_len + 2);
    for (size_t i = 0; i < type_len; i++) {
        type_lower[i] = tolower((unsigned char)collect_type[i]);
    }
    type_lower[type_len] = '[';
    type_lower[type_len + 1] = '\0';
    size_t prefix_len = type_len + 1;

    puppet_debug("Collector: looking for virtual %s resources", collect_type);

    /* Iterate through all virtual resources */
    size_t realized_count = 0;
    for (size_t i = 0; i < env->virtual_resources->bucket_count; i++) {
        puppet_hash_entry_t *entry = env->virtual_resources->buckets[i];
        while (entry) {
            puppet_hash_entry_t *next = entry->next;  /* Save next before potential removal */

            /* Check if this resource matches the type */
            if (strncmp(entry->key.data, type_lower, prefix_len) == 0) {
                /* Extract stored stmt pointer and instance index */
                puppet_stmt_t *res_stmt = (puppet_stmt_t *)entry->value->data.string.data;
                size_t instance_idx = (size_t)entry->value->data.string.len;

                /* Check filter if present */
                if (collector_matches_filter(filter, res_stmt, instance_idx, env)) {
                    puppet_debug("Collector: realizing %s", entry->key.data);
                    realize_single_resource(res_stmt, instance_idx, env);
                    realized_count++;
                }
            }
            entry = next;
        }
    }

    puppet_free(type_lower);
    puppet_debug("Collector: realized %zu %s resource(s)", realized_count, collect_type);
}

/* Forward declarations */
void puppet_exec_require(puppet_stmt_t *require_stmt, puppet_env_t *env);
void puppet_exec_contain(puppet_stmt_t *contain_stmt, puppet_env_t *env);

void puppet_exec_stmt(puppet_stmt_t *stmt, puppet_env_t *env) {
    if (!stmt) return;
    
    switch (stmt->type) {
        case PUPPET_STMT_ASSIGNMENT:
            puppet_exec_assignment(stmt->data.assignment.variable.data, 
                                  stmt->data.assignment.value, env);
            break;
            
        case PUPPET_STMT_CLASS_DEF:
            puppet_exec_class_def(stmt, env);
            break;
            
        case PUPPET_STMT_CLASS_INSTANCE:
            puppet_exec_class_instance(stmt, env);
            break;
            
        case PUPPET_STMT_NODE:
            puppet_exec_node(stmt, env);
            break;
            
        case PUPPET_STMT_INCLUDE:
            puppet_exec_include(stmt, env);
            break;

        case PUPPET_STMT_REQUIRE:
            puppet_exec_require(stmt, env);
            break;

        case PUPPET_STMT_CONTAIN:
            puppet_exec_contain(stmt, env);
            break;

        case PUPPET_STMT_FUNCTION_CALL:
            // Execute function call statement (stored as expression)
            if (stmt->data.expr) {
                puppet_value_t *result = puppet_eval_expr(stmt->data.expr, env);
                puppet_value_destroy(result);
            }
            break;

        case PUPPET_STMT_EXPRESSION:
            // Execute bare expression statement
            if (stmt->data.expr) {
                puppet_value_t *result = puppet_eval_expr(stmt->data.expr, env);
                puppet_value_destroy(result);
            }
            break;

        case PUPPET_STMT_RESOURCE:
            puppet_debug("Executing resource: %s", stmt->data.resource.type.data);

            // Handle class resources specially - they instantiate classes
            if (strcmp(stmt->data.resource.type.data, "class") == 0) {
                for (size_t i = 0; i < stmt->data.resource.instance_count; i++) {
                    puppet_resource_instance_t *instance = &stmt->data.resource.instances[i];
                    if (instance->title) {
                        puppet_value_t *title_val = puppet_eval_expr(instance->title, env);
                        const char *class_name = puppet_value_to_string(title_val);
                        puppet_debug("  Class resource: %s", class_name);

                        // Find the class definition
                        puppet_stmt_t *class_def = puppet_find_class_def(env, class_name);
                        if (!class_def && env->loader) {
                            class_def = puppet_loader_load_class(env->loader, class_name);
                        }

                        if (!class_def) {
                            puppet_error("Class '%s' not found", class_name);
                            puppet_value_destroy(title_val);
                            continue;
                        }

                        // Handle class inheritance - include parent class first
                        puppet_scope_t *parent_class_scope = NULL;
                        if (class_def->data.class_def.inherits && class_def->data.class_def.inherits->data) {
                            const char *parent_name = class_def->data.class_def.inherits->data;
                            /* Strip leading :: from parent name for lookups */
                            const char *parent_lookup_name = parent_name;
                            if (strncmp(parent_lookup_name, "::", 2) == 0) {
                                parent_lookup_name = parent_name + 2;
                            }
                            puppet_debug("Class %s inherits from %s", class_name, parent_name);

                            puppet_stmt_t *parent_def = puppet_find_class_def(env, parent_lookup_name);
                            if (!parent_def && env->loader) {
                                parent_def = puppet_loader_load_class(env->loader, parent_lookup_name);
                            }

                            if (parent_def) {
                                parent_class_scope = (puppet_scope_t *)puppet_hash_get(
                                    env->class_scopes, parent_lookup_name, strlen(parent_lookup_name));
                                if (!parent_class_scope) {
                                    puppet_include_class_from_def(parent_def, env);
                                    parent_class_scope = (puppet_scope_t *)puppet_hash_get(
                                        env->class_scopes, parent_lookup_name, strlen(parent_lookup_name));
                                }
                            } else {
                                puppet_warn("Parent class '%s' not found for class '%s'", parent_name, class_name);
                            }
                        }

                        // Pre-evaluate all attribute values in CALLER's scope (before pushing new class scope)
                        // This is critical - variables like $backups in "directories => $backups" must
                        // be looked up in the calling class's scope, not the new class being declared
                        size_t param_count = class_def->data.class_def.params.count;
                        puppet_value_t **pre_eval_values = NULL;
                        bool *attr_found = NULL;

                        if (param_count > 0) {
                            pre_eval_values = puppet_malloc(param_count * sizeof(puppet_value_t *));
                            attr_found = puppet_malloc(param_count * sizeof(bool));

                            for (size_t pi = 0; pi < param_count; pi++) {
                                puppet_param_t *param = &class_def->data.class_def.params.params[pi];
                                const char *param_name = param->name.data;
                                pre_eval_values[pi] = NULL;
                                attr_found[pi] = false;

                                // Look for matching attribute and evaluate in caller's scope
                                for (size_t ai = 0; ai < instance->attr_count; ai++) {
                                    if (instance->attributes[ai].name.data &&
                                        strcmp(instance->attributes[ai].name.data, param_name) == 0) {
                                        pre_eval_values[pi] = puppet_eval_expr(instance->attributes[ai].value, env);
                                        attr_found[pi] = true;
                                        break;
                                    }
                                }
                            }
                        }

                        // Now create scope for class, parented by inherited class scope if any
                        puppet_scope_t *scope_parent = parent_class_scope ? parent_class_scope : env->current_scope;
                        puppet_scope_t *class_scope = puppet_scope_create(scope_parent, class_name);
                        puppet_scope_push(env, class_scope);
                        puppet_scope_t *old_class_scope = env->class_scope;
                        env->class_scope = class_scope;

                        // Store class scope BEFORE executing body for $class::var lookups
                        puppet_hash_set(env->class_scopes, class_name, strlen(class_name), (puppet_value_t *)class_scope);

                        // Set class parameters using pre-evaluated values or defaults
                        for (size_t pi = 0; pi < param_count; pi++) {
                            puppet_param_t *param = &class_def->data.class_def.params.params[pi];
                            const char *param_name = param->name.data;
                            puppet_value_t *param_value;

                            if (attr_found[pi]) {
                                // Use pre-evaluated value from caller's scope
                                param_value = pre_eval_values[pi];
                            } else {
                                // Try Automatic Parameter Lookup (APL) from Hiera
                                param_value = puppet_apl_lookup(class_name, param_name, env);
                                if (!param_value) {
                                    // Fall back to default value if APL didn't find anything
                                    if (param->default_value) {
                                        param_value = puppet_eval_expr(param->default_value, env);
                                    } else {
                                        param_value = puppet_value_create_undef();
                                    }
                                }
                            }

                            puppet_scope_set_var(class_scope, param_name, param_value);
                        }

                        // Clean up temporary arrays
                        if (pre_eval_values) puppet_free(pre_eval_values);
                        if (attr_found) puppet_free(attr_found);

                        // Execute class body
                        printf("Including class: %s\n", class_name);
                        puppet_exec_stmt_list(&class_def->data.class_def.body, env);

                        // Add to catalog
                        if (env->build_catalog && env->catalog) {
                            puppet_catalog_add_class(env->catalog, class_name);
                        }

                        // Cleanup - pop scope but don't destroy (it's stored in class_scopes)
                        env->class_scope = old_class_scope;
                        (void)puppet_scope_pop(env);  // Pop but don't destroy
                        puppet_value_destroy(title_val);
                    }
                }
                break;
            }

            // Handle virtual resources - store but don't apply
            if (stmt->data.resource.style == PUPPET_RES_VIRTUAL) {
                for (size_t i = 0; i < stmt->data.resource.instance_count; i++) {
                    puppet_resource_instance_t *instance = &stmt->data.resource.instances[i];
                    if (instance->title) {
                        puppet_value_t *title_val = puppet_eval_expr(instance->title, env);
                        const char *title_str = puppet_value_to_string(title_val);

                        // Build resource identifier
                        size_t res_id_len = strlen(stmt->data.resource.type.data) + strlen(title_str) + 3;
                        char *resource_id = puppet_malloc(res_id_len);
                        snprintf(resource_id, res_id_len, "%s[%s]", stmt->data.resource.type.data, title_str);

                        // Check for duplicate virtual resource
                        puppet_value_t *existing = puppet_hash_get(env->virtual_resources,
                                                                   resource_id, strlen(resource_id));
                        if (existing) {
                            puppet_debug("Virtual resource %s already declared", resource_id);
                            puppet_free(resource_id);
                            puppet_value_destroy(title_val);
                            continue;
                        }

                        // Store in virtual resources (value is pointer to statement - don't free)
                        puppet_value_t *stmt_ptr = puppet_calloc(1, sizeof(puppet_value_t));
                        stmt_ptr->type = PUPPET_VALUE_UNDEF;  /* Use as opaque pointer */
                        stmt_ptr->data.string.data = (char*)stmt;  /* Store stmt pointer */
                        stmt_ptr->data.string.len = i;  /* Store instance index */
                        puppet_hash_set(env->virtual_resources, resource_id, strlen(resource_id), stmt_ptr);

                        puppet_debug("Stored virtual resource: %s", resource_id);
                        puppet_free(resource_id);
                        puppet_value_destroy(title_val);
                    }
                }
                break;  /* Virtual resources are not applied now */
            }

            // Normal resource execution
            // Evaluate resource titles and check for duplicates
            for (size_t i = 0; i < stmt->data.resource.instance_count; i++) {
                puppet_resource_instance_t *instance = &stmt->data.resource.instances[i];
                if (instance->title) {
                    puppet_value_t *title_val = puppet_eval_expr(instance->title, env);

                    // Handle array titles - expand into multiple resources
                    size_t title_count = 1;
                    puppet_value_t **titles = NULL;

                    if (title_val->type == PUPPET_VALUE_ARRAY) {
                        title_count = title_val->data.array->count;
                        titles = title_val->data.array->items;
                    }

                    for (size_t t = 0; t < title_count; t++) {
                        const char *title_str;
                        if (titles) {
                            title_str = puppet_value_to_string(titles[t]);
                        } else {
                            title_str = puppet_value_to_string(title_val);
                        }

                        // Build resource identifier (type::title)
                        size_t res_id_len = strlen(stmt->data.resource.type.data) + strlen(title_str) + 3;
                        char *resource_id = puppet_malloc(res_id_len);
                        snprintf(resource_id, res_id_len, "%s[%s]", stmt->data.resource.type.data, title_str);

                        // Check for duplicate resource
                        puppet_value_t *existing = puppet_hash_get(env->resource_catalog,
                                                                   resource_id, strlen(resource_id));
                        if (existing) {
                            fprintf(stderr, "Error: Duplicate declaration - %s is already declared\n", resource_id);
                            fprintf(stderr, "       Resource titles must be unique within their type\n");
                            puppet_free(resource_id);
                            continue;  // Skip this duplicate resource
                        }

                        // Add to duplicate detection catalog
                        puppet_value_t *marker = puppet_value_create_bool(true);
                        puppet_hash_set(env->resource_catalog, resource_id, strlen(resource_id), marker);

                        puppet_debug("  Title: %s", title_str);

                        // Check if this is the template target for output
                        bool is_template_target = (env->template_output_target &&
                                                   strcmp(title_str, env->template_output_target) == 0 &&
                                                   strcmp(stmt->data.resource.type.data, "file") == 0);

                        // Collect parameters for catalog
                        puppet_catalog_param_t *params = NULL;
                        size_t param_count = instance->attr_count;
                        if (env->build_catalog && param_count > 0) {
                            params = puppet_calloc(param_count, sizeof(puppet_catalog_param_t));
                        }

                        // Show attributes for this instance
                        size_t param_idx = 0;  // Separate index for params array
                        for (size_t j = 0; j < instance->attr_count; j++) {
                            // Skip attributes with NULL names (parser bug workaround)
                            if (!instance->attributes[j].name.data) {
                                puppet_debug("    [WARN] Skipping attribute with NULL name");
                                continue;
                            }

                            puppet_value_t *attr_val = puppet_eval_expr(instance->attributes[j].value, env);
                            const char *attr_str = puppet_value_to_string(attr_val);
                            puppet_debug("    %s => %s", instance->attributes[j].name.data, attr_str);

                            // If this is template output mode and we found the content attribute
                            // Output goes to stdout (clean, for piping) - no markers
                            if (is_template_target && strcmp(instance->attributes[j].name.data, "content") == 0) {
                                if (attr_val->type == PUPPET_VALUE_STRING) {
                                    printf("%s", attr_val->data.string.data);
                                    env->template_output_found = true;
                                }
                            }

                            // Store in catalog params if building catalog
                            if (env->build_catalog && params) {
                                params[param_idx].name = puppet_strdup(instance->attributes[j].name.data);
                                params[param_idx].value = puppet_value_copy(attr_val);
                                param_idx++;  // Increment only when we add a parameter
                            }

                            puppet_value_destroy(attr_val);
                        }

                        // Add to resource catalog if building
                        if (env->build_catalog && env->catalog) {
                            puppet_catalog_add_resource(env->catalog,
                                                        stmt->data.resource.type.data,
                                                        title_str,
                                                        params,
                                                        param_idx);  // Use actual count, not attr_count
                        }

                        puppet_free(resource_id);
                    }
                    puppet_value_destroy(title_val);
                }
            }
            break;

        case PUPPET_STMT_IF: {
            // Execute if/elsif/else chain
            puppet_if_branch_t *branch = stmt->data.if_stmt.branches;
            bool executed = false;

            while (branch && !executed) {
                puppet_value_t *cond = puppet_eval_expr(branch->condition, env);
                bool is_true = false;

                // Evaluate truthiness: false and undef are falsy, everything else is truthy
                if (cond) {
                    if (cond->type == PUPPET_VALUE_BOOL) {
                        is_true = cond->data.boolean;
                    } else if (cond->type == PUPPET_VALUE_UNDEF) {
                        is_true = false;
                    } else {
                        is_true = true;
                    }
                    puppet_value_destroy(cond);
                }

                if (is_true) {
                    puppet_exec_stmt_list(&branch->body, env);
                    executed = true;
                }
                branch = branch->next;
            }

            // Execute else branch if no condition matched
            if (!executed && stmt->data.if_stmt.else_body) {
                puppet_exec_stmt_list(stmt->data.if_stmt.else_body, env);
            }
            break;
        }

        case PUPPET_STMT_UNLESS: {
            // Execute unless (inverse of if)
            puppet_value_t *cond = puppet_eval_expr(stmt->data.unless_stmt.condition, env);
            bool is_false = true;

            if (cond) {
                if (cond->type == PUPPET_VALUE_BOOL) {
                    is_false = !cond->data.boolean;
                } else if (cond->type == PUPPET_VALUE_UNDEF) {
                    is_false = true;
                } else {
                    is_false = false;
                }
                puppet_value_destroy(cond);
            }

            if (is_false) {
                puppet_exec_stmt_list(&stmt->data.unless_stmt.body, env);
            }
            break;
        }

        case PUPPET_STMT_CASE: {
            // Execute case statement
            puppet_value_t *expr_val = puppet_eval_expr(stmt->data.case_stmt.expr, env);
            bool matched = false;

            for (size_t i = 0; i < stmt->data.case_stmt.when_count && !matched; i++) {
                puppet_case_when_t *when = &stmt->data.case_stmt.whens[i];
                puppet_value_t *test_val = puppet_eval_expr(when->test, env);

                // Check for match (using equality comparison)
                bool is_match = false;
                if (expr_val && test_val) {
                    if (expr_val->type == test_val->type) {
                        switch (expr_val->type) {
                            case PUPPET_VALUE_BOOL:
                                is_match = (expr_val->data.boolean == test_val->data.boolean);
                                break;
                            case PUPPET_VALUE_NUMBER:
                                is_match = (expr_val->data.number == test_val->data.number);
                                break;
                            case PUPPET_VALUE_STRING:
                                is_match = (expr_val->data.string.len == test_val->data.string.len &&
                                           memcmp(expr_val->data.string.data, test_val->data.string.data,
                                                  expr_val->data.string.len) == 0);
                                break;
                            default:
                                break;
                        }
                    }
                    // Also check if test is 'default' keyword (represented as special value)
                    // For now, we handle default_body separately
                }

                if (test_val) puppet_value_destroy(test_val);

                if (is_match) {
                    puppet_exec_stmt_list(&when->body, env);
                    matched = true;
                }
            }

            // Execute default branch if no when matched
            if (!matched && stmt->data.case_stmt.default_body) {
                puppet_exec_stmt_list(stmt->data.case_stmt.default_body, env);
            }

            if (expr_val) puppet_value_destroy(expr_val);
            break;
        }

        case PUPPET_STMT_RESOURCE_COLLECTOR:
            puppet_exec_collector(stmt, env);
            break;

        default:
            puppet_warn("Unimplemented statement type: %d", stmt->type);
            break;
    }
}

void puppet_exec_stmt_list(puppet_stmt_list_t *stmts, puppet_env_t *env) {
    for (size_t i = 0; i < stmts->count; i++) {
        puppet_exec_stmt(stmts->stmts[i], env);
    }
}

void puppet_exec_assignment(const char *var, puppet_expr_t *value, puppet_env_t *env) {
    puppet_value_t *val = puppet_eval_expr(value, env);

    // Use scoped variable assignment (defaults to local scope)
    puppet_env_set_scoped_var(env, var, val, PUPPET_VAR_LOCAL);

    // Debug output
    if (puppet_verbose) {
        const char *val_str;
        char num_buf[64];
        switch (val->type) {
            case PUPPET_VALUE_UNDEF:
                val_str = "undef";
                break;
            case PUPPET_VALUE_BOOL:
                val_str = val->data.boolean ? "true" : "false";
                break;
            case PUPPET_VALUE_STRING:
                val_str = val->data.string.data;
                break;
            case PUPPET_VALUE_NUMBER:
                snprintf(num_buf, sizeof(num_buf), "%.6g", val->data.number);
                val_str = num_buf;
                break;
            default:
                val_str = "(complex value)";
                break;
        }
        puppet_debug("Set $%s = %s", var, val_str);
    }
}

void puppet_exec_class_def(puppet_stmt_t *class_stmt, puppet_env_t *env) {
    const char *class_name = class_stmt->data.class_def.name.data;
    puppet_debug("Defining class: %s", class_name);

    // Register this class definition for later instantiation
    // The class body is NOT executed here - it will be executed when
    // the class is included via 'include', 'require', 'contain', or
    // resource-style instantiation (class { 'name': ... })
    puppet_register_class_def(env, class_stmt);
}

void puppet_exec_program(puppet_program_t *program, puppet_env_t *env) {
    /* Reset node matching state */
    env->node_matched = false;
    env->default_node = NULL;
    env->node_def_count = 0;  /* Reset node definition registry */

    /*
     * Special mode: when execute_all_nodes AND facts_db is set with multiple nodes,
     * we iterate over nodes in the facts database rather than executing node blocks
     * as we encounter them.
     */
    bool facts_db_iteration_mode = env->execute_all_nodes &&
                                    env->facts_db &&
                                    puppet_facts_db_node_count(env->facts_db) > 0;

    if (facts_db_iteration_mode) {
        /* Enable deferred node execution - collect node definitions */
        env->defer_node_execution = true;
        puppet_debug("Facts DB iteration mode: collecting node definitions");
    }

    /* Execute all statements (in defer mode, nodes are registered not executed) */
    puppet_exec_stmt_list(&program->statements, env);

    if (facts_db_iteration_mode) {
        /* Disable defer mode */
        env->defer_node_execution = false;

        /* Now iterate over all nodes in the facts database */
        size_t node_count = puppet_facts_db_node_count(env->facts_db);
        puppet_debug("Executing %zu nodes from facts database", node_count);

        for (size_t i = 0; i < node_count; i++) {
            const char *certname = puppet_facts_db_get_node_name(env->facts_db, i);
            if (!certname) continue;

            /* Find matching node definition */
            puppet_stmt_t *matching_node = puppet_find_matching_node(env, certname);

            if (matching_node) {
                /* Execute the matching node block for this certname */
                puppet_exec_node_for_certname(matching_node, certname, env);
            } else {
                puppet_warn("No matching node block found for '%s'", certname);
            }
        }
    } else {
        /* Fallback to default node if specific node was requested but not found */
        if (env->node_name && !env->node_matched && env->default_node) {
            puppet_debug("Node '%s' not found, falling back to 'default' node", env->node_name);
            /* Temporarily allow default node execution */
            char *saved_node_name = env->node_name;
            env->node_name = NULL;
            puppet_exec_node(env->default_node, env);
            env->node_name = saved_node_name;
        }
    }
}

/* Helper function to execute a registered class definition */
static bool puppet_include_class_from_def(puppet_stmt_t *class_def, puppet_env_t *env) {
    if (!class_def || class_def->type != PUPPET_STMT_CLASS_DEF || !env) return false;

    const char *class_name = class_def->data.class_def.name.data;
    printf("Including class: %s\n", class_name);

    /* Handle class inheritance - include parent class first */
    puppet_scope_t *parent_class_scope = NULL;
    if (class_def->data.class_def.inherits && class_def->data.class_def.inherits->data) {
        const char *parent_name = class_def->data.class_def.inherits->data;

        /* Strip leading :: from parent name for lookups */
        const char *parent_lookup_name = parent_name;
        if (strncmp(parent_lookup_name, "::", 2) == 0) {
            parent_lookup_name = parent_name + 2;
        }

        puppet_debug("Class %s inherits from %s", class_name, parent_name);

        /* Find and include the parent class */
        puppet_stmt_t *parent_def = puppet_find_class_def(env, parent_lookup_name);
        if (!parent_def && env->loader) {
            parent_def = puppet_loader_load_class(env->loader, parent_lookup_name);
        }

        if (parent_def) {
            /* Check if parent is already included */
            parent_class_scope = (puppet_scope_t *)puppet_hash_get(
                env->class_scopes, parent_lookup_name, strlen(parent_lookup_name));
            if (!parent_class_scope) {
                /* Include the parent class first */
                puppet_include_class_from_def(parent_def, env);
                parent_class_scope = (puppet_scope_t *)puppet_hash_get(
                    env->class_scopes, parent_lookup_name, strlen(parent_lookup_name));
            }
        } else {
            puppet_warn("Parent class '%s' not found for class '%s'", parent_name, class_name);
        }
    }

    /* Create a new scope for the class, parented by the inherited class scope if any */
    puppet_scope_t *scope_parent = parent_class_scope ? parent_class_scope : env->current_scope;
    puppet_scope_t *class_scope = puppet_scope_create(scope_parent, class_name);
    puppet_scope_push(env, class_scope);

    /* Set class scope in environment for enhanced variable lookup */
    puppet_scope_t *old_class_scope = env->class_scope;
    env->class_scope = class_scope;

    /* Store class scope BEFORE executing body - allows $class::var lookups during execution */
    puppet_hash_set(env->class_scopes, class_name, strlen(class_name), (puppet_value_t *)class_scope);

    /* Process class parameters - use APL (Automatic Parameter Lookup) for unset params */
    for (size_t i = 0; i < class_def->data.class_def.params.count; i++) {
        puppet_param_t *param = &class_def->data.class_def.params.params[i];
        const char *param_name = param->name.data;
        puppet_value_t *param_value = NULL;

        /* Try Automatic Parameter Lookup (APL) from Hiera first */
        param_value = puppet_apl_lookup(class_name, param_name, env);

        if (!param_value) {
            /* Fall back to default value if APL didn't find anything */
            if (param->default_value) {
                param_value = puppet_eval_expr(param->default_value, env);
            } else {
                param_value = puppet_value_create_undef();
            }
        }

        puppet_scope_set_var(class_scope, param_name, param_value);
    }

    /* Execute the class body */
    puppet_exec_stmt_list(&class_def->data.class_def.body, env);

    /* Add class to catalog if building */
    if (env->build_catalog && env->catalog) {
        puppet_catalog_add_class(env->catalog, class_name);
    }

    /* Restore old class scope */
    env->class_scope = old_class_scope;

    /* Pop the class scope but don't destroy (it's stored in class_scopes) */
    (void)puppet_scope_pop(env);

    return true;
}

void puppet_exec_include(puppet_stmt_t *include_stmt, puppet_env_t *env) {
    if (!include_stmt || include_stmt->type != PUPPET_STMT_INCLUDE) return;

    /* Process each included class */
    for (size_t i = 0; i < include_stmt->data.names.count; i++) {
        puppet_expr_t *name_expr = include_stmt->data.names.exprs[i];

        /* Extract class name from expression */
        if (name_expr && name_expr->type == PUPPET_EXPR_VALUE &&
            name_expr->data.value->type == PUPPET_VALUE_STRING) {

            const char *class_name = name_expr->data.value->data.string.data;

            /* First, try to find the class in registered definitions */
            puppet_stmt_t *class_def = puppet_find_class_def(env, class_name);
            if (class_def) {
                puppet_include_class_from_def(class_def, env);
                continue;
            }

            /* If not found, try to load from module files using the loader */
            if (env->loader) {
                if (!puppet_loader_include_class(env->loader, class_name, env)) {
                    puppet_warn("Failed to include class '%s'", class_name);
                }
            } else {
                puppet_error("Class '%s' not found", class_name);
            }
        }
    }
}

void puppet_exec_require(puppet_stmt_t *require_stmt, puppet_env_t *env) {
    if (!require_stmt || require_stmt->type != PUPPET_STMT_REQUIRE) return;

    /*
     * 'require' is like 'include' but also creates an ordering dependency:
     * all resources in the current scope will require (depend on) the
     * required class. For now, we just include the class.
     * TODO: Add dependency tracking for proper ordering.
     */

    for (size_t i = 0; i < require_stmt->data.names.count; i++) {
        puppet_expr_t *name_expr = require_stmt->data.names.exprs[i];

        if (name_expr && name_expr->type == PUPPET_EXPR_VALUE &&
            name_expr->data.value->type == PUPPET_VALUE_STRING) {

            const char *class_name = name_expr->data.value->data.string.data;

            /* First, try to find the class in registered definitions */
            puppet_stmt_t *class_def = puppet_find_class_def(env, class_name);
            if (class_def) {
                puppet_include_class_from_def(class_def, env);
                continue;
            }

            /* If not found, try to load from module files using the loader */
            if (env->loader) {
                if (!puppet_loader_include_class(env->loader, class_name, env)) {
                    puppet_warn("Failed to require class '%s'", class_name);
                }
            } else {
                puppet_error("Class '%s' not found", class_name);
            }
        }
    }
}

void puppet_exec_contain(puppet_stmt_t *contain_stmt, puppet_env_t *env) {
    if (!contain_stmt || contain_stmt->type != PUPPET_STMT_CONTAIN) return;

    /*
     * 'contain' is like 'include' but the contained class's dependencies
     * become dependencies of the containing class. This is important for
     * proper ordering when classes are used in dependency chains.
     * For now, we just include the class.
     * TODO: Add containment tracking for proper dependency propagation.
     */

    for (size_t i = 0; i < contain_stmt->data.names.count; i++) {
        puppet_expr_t *name_expr = contain_stmt->data.names.exprs[i];

        if (name_expr && name_expr->type == PUPPET_EXPR_VALUE &&
            name_expr->data.value->type == PUPPET_VALUE_STRING) {

            const char *class_name = name_expr->data.value->data.string.data;

            /* First, try to find the class in registered definitions */
            puppet_stmt_t *class_def = puppet_find_class_def(env, class_name);
            if (class_def) {
                puppet_include_class_from_def(class_def, env);
                continue;
            }

            /* If not found, try to load from module files using the loader */
            if (env->loader) {
                if (!puppet_loader_include_class(env->loader, class_name, env)) {
                    puppet_warn("Failed to contain class '%s'", class_name);
                }
            } else {
                puppet_error("Class '%s' not found", class_name);
            }
        }
    }
}

void puppet_env_set_loader(puppet_env_t *env, puppet_loader_t *loader) {
    if (!env) return;
    env->loader = loader;
}

/**
 * @brief Execute a node definition for a specific certname
 *
 * This is used when iterating over facts_db nodes. The certname is used
 * to set the correct facts before executing the node body.
 *
 * @param node_stmt Node definition to execute
 * @param certname Node certname (for facts lookup)
 * @param env Execution environment
 */
static void puppet_exec_node_for_certname(puppet_stmt_t *node_stmt, const char *certname, puppet_env_t *env) {
    if (!node_stmt || node_stmt->type != PUPPET_STMT_NODE || !certname) return;

    puppet_debug("Executing node block for certname: %s", certname);
    env->node_matched = true;

    /* Clear resource catalog for this node (each node has its own catalog) */
    if (env->resource_catalog) {
        for (size_t i = 0; i < env->resource_catalog->bucket_count; i++) {
            puppet_hash_entry_t *entry = env->resource_catalog->buckets[i];
            while (entry) {
                puppet_hash_entry_t *next = entry->next;
                puppet_free(entry->key.data);
                puppet_value_destroy(entry->value);
                puppet_free(entry);
                entry = next;
            }
            env->resource_catalog->buckets[i] = NULL;
        }
    }

    /* Switch to node-specific facts */
    if (env->facts_db) {
        if (puppet_facts_db_set_current_node(env->facts_db, certname) == 0) {
            puppet_debug("Using facts for node: %s", certname);
        } else {
            puppet_warn("No facts found for node %s", certname);
        }
    }

    /* Create a new scope for the node using the certname */
    puppet_scope_t *node_scope = puppet_scope_create(env->current_scope, certname);
    puppet_scope_push(env, node_scope);

    /* Set automatic variables using the certname */
    puppet_value_t *hostname_value = puppet_value_create_string(certname, strlen(certname));
    puppet_scope_set_var(node_scope, "hostname", hostname_value);

    /* Execute node body */
    puppet_exec_stmt_list(&node_stmt->data.node.body, env);

    /* Pop the node scope */
    puppet_scope_t *old_scope = puppet_scope_pop(env);
    puppet_scope_destroy(old_scope);
}

void puppet_exec_node(puppet_stmt_t *node_stmt, puppet_env_t *env) {
    if (!node_stmt || node_stmt->type != PUPPET_STMT_NODE) return;

    const char *node_name = node_stmt->data.node.name.data;
    bool is_default = (strcmp(node_name, "default") == 0);

    /* Store default node for potential fallback */
    if (is_default) {
        env->default_node = node_stmt;
    }

    /* If in defer mode (facts_db iteration), just register the node */
    if (env->defer_node_execution) {
        puppet_register_node_def(env, node_stmt);
        return;
    }

    /* Check if we should execute this node */
    bool should_execute = false;

    if (env->execute_all_nodes) {
        /* Execute all nodes when --all-nodes is specified */
        should_execute = true;
    } else if (!env->node_name) {
        /* No node specified - only execute 'default' node */
        should_execute = is_default;
    } else {
        /* Specific node requested - check for match (not default) */
        if (!is_default) {
            size_t name_len = strlen(node_name);
            /* Check if node name is a regex pattern (starts and ends with /) */
            if (name_len > 2 && node_name[0] == '/' && node_name[name_len - 1] == '/') {
                /* Extract regex pattern (without the slashes) */
                char *pattern = puppet_malloc(name_len - 1);
                strncpy(pattern, node_name + 1, name_len - 2);
                pattern[name_len - 2] = '\0';

                /* Compile and execute regex */
                regex_t regex;
                int ret = regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB);
                if (ret == 0) {
                    ret = regexec(&regex, env->node_name, 0, NULL, 0);
                    should_execute = (ret == 0);
                    regfree(&regex);
                }
                puppet_free(pattern);
            } else {
                /* Literal string match */
                should_execute = (strcmp(node_name, env->node_name) == 0);
            }
        }
    }

    if (should_execute) {
        puppet_debug("Executing node: %s", node_name);
        env->node_matched = true;

        /* Clear resource catalog for this node (each node has its own catalog) */
        if (env->resource_catalog) {
            /* Clear existing entries but keep the hash table structure */
            for (size_t i = 0; i < env->resource_catalog->bucket_count; i++) {
                puppet_hash_entry_t *entry = env->resource_catalog->buckets[i];
                while (entry) {
                    puppet_hash_entry_t *next = entry->next;
                    puppet_free(entry->key.data);
                    puppet_value_destroy(entry->value);
                    puppet_free(entry);
                    entry = next;
                }
                env->resource_catalog->buckets[i] = NULL;
            }
            /* Size tracking is handled internally */
        }

        /* Switch to node-specific facts if available */
        if (env->facts_db) {
            if (puppet_facts_db_set_current_node(env->facts_db, node_name) == 0) {
                puppet_debug("Using facts for node: %s", node_name);
            } else {
                puppet_debug("No facts found for node %s, using default facts", node_name);
            }
        }

        /* Create a new scope for the node */
        puppet_scope_t *node_scope = puppet_scope_create(env->current_scope, node_name);
        puppet_scope_push(env, node_scope);

        /* Set automatic variables like $hostname */
        puppet_value_t *hostname_value = puppet_value_create_string(node_name, strlen(node_name));
        puppet_scope_set_var(node_scope, "hostname", hostname_value);

        /* Execute node body */
        puppet_exec_stmt_list(&node_stmt->data.node.body, env);

        /* Pop the node scope */
        puppet_scope_t *old_scope = puppet_scope_pop(env);
        puppet_scope_destroy(old_scope);
    } else {
        /* Skip this node */
        if (!env->execute_all_nodes && env->node_name) {
            /* Only report skipping when a specific node was requested */
            puppet_debug("Skipping node: %s (looking for %s)", node_name, env->node_name);
        }
    }
}

void puppet_env_set_node(puppet_env_t *env, const char *node_name) {
    if (!env) return;
    
    puppet_free(env->node_name);
    env->node_name = node_name ? puppet_strdup(node_name) : NULL;
    env->execute_all_nodes = false;  /* Specific node mode */
}

void puppet_env_set_execute_all_nodes(puppet_env_t *env, bool execute_all) {
    if (!env) return;
    
    env->execute_all_nodes = execute_all;
    if (execute_all) {
        puppet_free(env->node_name);
        env->node_name = NULL;  /* Clear specific node when in all-nodes mode */
    }
}

void puppet_env_set_template_output(puppet_env_t *env, const char *template_target) {
    if (!env) return;

    puppet_free(env->template_output_target);
    env->template_output_target = template_target ? puppet_strdup(template_target) : NULL;
}

void puppet_env_enable_catalog(puppet_env_t *env, const char *certname, const char *environment) {
    if (!env) return;

    env->build_catalog = true;
    env->catalog = puppet_catalog_create(certname, environment);
}

puppet_catalog_t *puppet_env_get_catalog(puppet_env_t *env) {
    if (!env) return NULL;

    puppet_catalog_t *catalog = env->catalog;
    env->catalog = NULL;  /* Transfer ownership to caller */
    return catalog;
}

void puppet_exec_class_instance(puppet_stmt_t *class_instance_stmt, puppet_env_t *env) {
    if (!class_instance_stmt || class_instance_stmt->type != PUPPET_STMT_CLASS_INSTANCE) return;

    const char *class_name = class_instance_stmt->data.class_instance.class_name.data;
    puppet_debug("Instantiating class: %s", class_name);

    // Find the class definition - first check registered definitions
    puppet_stmt_t *class_def = puppet_find_class_def(env, class_name);

    // If not found in registered definitions, try to load from module files
    if (!class_def && env->loader) {
        class_def = puppet_loader_load_class(env->loader, class_name);
    }

    if (!class_def) {
        puppet_error("Class '%s' not found", class_name);
        return;
    }
    
    // Create a new scope for the class instance
    puppet_scope_t *class_scope = puppet_scope_create(env->current_scope, class_name);
    puppet_scope_push(env, class_scope);
    
    // Set class scope in environment for enhanced variable lookup
    puppet_scope_t *old_class_scope = env->class_scope;
    env->class_scope = class_scope;
    
    // Process class parameters and apply defaults first
    for (size_t i = 0; i < class_def->data.class_def.params.count; i++) {
        puppet_param_t *param = &class_def->data.class_def.params.params[i];
        const char *param_name = param->name.data;

        // Look for this parameter in provided arguments
        puppet_value_t *param_value = NULL;
        bool found_arg = false;

        for (size_t j = 0; j < class_instance_stmt->data.class_instance.arg_count; j++) {
            puppet_attribute_t *arg = &class_instance_stmt->data.class_instance.arguments[j];
            if (strcmp(arg->name.data, param_name) == 0) {
                param_value = puppet_eval_expr(arg->value, env);
                found_arg = true;
                break;
            }
        }

        // If not provided, use default value
        if (!found_arg && param->default_value) {
            param_value = puppet_eval_expr(param->default_value, env);
        } else if (!found_arg) {
            param_value = puppet_value_create_undef();
        }

        // Set the parameter value in class scope
        if (param_value) {
            puppet_scope_set_var(class_scope, param_name, param_value);

            // Debug output
            if (puppet_verbose) {
                const char *val_str;
                char num_buf[64];
                const char *source;
                if (!found_arg && !param->default_value) {
                    val_str = "undef";
                    source = " (no default)";
                } else {
                    source = found_arg ? " (provided)" : " (default)";
                    switch (param_value->type) {
                        case PUPPET_VALUE_BOOL:
                            val_str = param_value->data.boolean ? "true" : "false";
                            break;
                        case PUPPET_VALUE_NUMBER:
                            snprintf(num_buf, sizeof(num_buf), "%.6g", param_value->data.number);
                            val_str = num_buf;
                            break;
                        case PUPPET_VALUE_STRING:
                            val_str = param_value->data.string.data;
                            break;
                        default:
                            val_str = "(complex value)";
                            break;
                    }
                }
                puppet_debug("Set class parameter $%s = %s%s", param_name, val_str, source);
            }
        }
    }

    // Execute the class body
    puppet_debug("Executing class body for: %s", class_name);
    puppet_exec_stmt_list(&class_def->data.class_def.body, env);

    // Add class to catalog if building
    if (env->build_catalog && env->catalog) {
        puppet_catalog_add_class(env->catalog, class_name);
    }

    puppet_debug("Class %s instantiation complete", class_name);
    
    // Restore old class scope
    env->class_scope = old_class_scope;
    
    // Pop the class scope
    puppet_scope_t *old_scope = puppet_scope_pop(env);
    puppet_scope_destroy(old_scope);
}

/*
 * ===========================================================================
 * CLASS DEFINITION MANAGEMENT
 * ===========================================================================
 */

/**
 * @brief Register a class definition for later instantiation
 *
 * @param env Execution environment
 * @param class_def Class definition statement
 * @return 0 on success, -1 on error
 */
int puppet_register_class_def(puppet_env_t *env, puppet_stmt_t *class_def) {
    if (!env || !class_def || class_def->type != PUPPET_STMT_CLASS_DEF) return -1;
    
    // Expand class definition array if needed
    if (env->class_def_count >= env->class_def_capacity) {
        env->class_def_capacity *= 2;
        env->class_definitions = puppet_realloc(env->class_definitions, 
            env->class_def_capacity * sizeof(puppet_stmt_t*));
        if (!env->class_definitions) {
            return -1;
        }
    }
    
    // Add class definition to registry
    env->class_definitions[env->class_def_count] = class_def;
    env->class_def_count++;
    
    return 0;
}

/**
 * @brief Find a class definition by name
 *
 * @param env Execution environment
 * @param class_name Class name to find
 * @return Class definition statement or NULL if not found
 */
puppet_stmt_t *puppet_find_class_def(puppet_env_t *env, const char *class_name) {
    if (!env || !class_name) return NULL;

    for (size_t i = 0; i < env->class_def_count; i++) {
        puppet_stmt_t *class_def = env->class_definitions[i];
        if (class_def && class_def->type == PUPPET_STMT_CLASS_DEF) {
            const char *def_name = class_def->data.class_def.name.data;
            if (strcmp(def_name, class_name) == 0) {
                return class_def;
            }
        }
    }

    return NULL;
}

/*
 * ===========================================================================
 * NODE DEFINITION MANAGEMENT
 * ===========================================================================
 */

/**
 * @brief Register a node definition for later execution
 *
 * @param env Execution environment
 * @param node_def Node definition statement
 * @return 0 on success, -1 on error
 */
static int puppet_register_node_def(puppet_env_t *env, puppet_stmt_t *node_def) {
    if (!env || !node_def || node_def->type != PUPPET_STMT_NODE) return -1;

    // Expand node definition array if needed
    if (env->node_def_count >= env->node_def_capacity) {
        env->node_def_capacity *= 2;
        env->node_definitions = puppet_realloc(env->node_definitions,
            env->node_def_capacity * sizeof(puppet_stmt_t*));
        if (!env->node_definitions) {
            return -1;
        }
    }

    // Add node definition to registry
    env->node_definitions[env->node_def_count] = node_def;
    env->node_def_count++;

    return 0;
}

/**
 * @brief Find a node definition matching a certname
 *
 * Searches through registered node definitions to find one matching the certname.
 * Handles both literal node names and regex patterns.
 *
 * @param env Execution environment
 * @param certname Node certname to match
 * @return Matching node definition or NULL if not found
 */
static puppet_stmt_t *puppet_find_matching_node(puppet_env_t *env, const char *certname) {
    if (!env || !certname) return NULL;

    puppet_stmt_t *default_node = NULL;

    for (size_t i = 0; i < env->node_def_count; i++) {
        puppet_stmt_t *node_def = env->node_definitions[i];
        if (!node_def || node_def->type != PUPPET_STMT_NODE) continue;

        const char *node_name = node_def->data.node.name.data;

        // Check for default node
        if (strcmp(node_name, "default") == 0) {
            default_node = node_def;
            continue;
        }

        size_t name_len = strlen(node_name);

        // Check if node name is a regex pattern (starts and ends with /)
        if (name_len > 2 && node_name[0] == '/' && node_name[name_len - 1] == '/') {
            // Extract regex pattern (without the slashes)
            char *pattern = puppet_malloc(name_len - 1);
            strncpy(pattern, node_name + 1, name_len - 2);
            pattern[name_len - 2] = '\0';

            // Compile and execute regex
            regex_t regex;
            int ret = regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB);
            if (ret == 0) {
                ret = regexec(&regex, certname, 0, NULL, 0);
                regfree(&regex);
                if (ret == 0) {
                    puppet_free(pattern);
                    return node_def;  // Regex match found
                }
            }
            puppet_free(pattern);
        } else {
            // Literal string match
            if (strcmp(node_name, certname) == 0) {
                return node_def;
            }
        }
    }

    // Return default node if no specific match found
    return default_node;
}

/*
 * ===========================================================================
 * ENHANCED VARIABLE SYSTEM IMPLEMENTATION
 * ===========================================================================
 */

/**
 * @brief Enhanced variable lookup with full chain traversal
 *
 * Implements the complete Puppet variable lookup chain:
 * 1. Local scope (current function/class)
 * 2. Class scope (if inside class)
 * 3. Node scope (node-specific variables)  
 * 4. Global scope (top-level variables)
 * 5. Data providers (Hiera, external data sources)
 *
 * @param env Execution environment
 * @param name Variable name to look up
 * @return Variable value or NULL if not found
 */
puppet_value_t *puppet_variable_lookup_chain(puppet_env_t *env, const char *name) {
    if (!env || !name) return NULL;

    puppet_value_t *value = NULL;

    // Handle :: prefix (top-level/global scope indicator)
    // Variables like $::fqdn, $::hostname explicitly request top-level scope
    const char *lookup_name = name;
    bool top_level_only = false;
    if (strncmp(name, "::", 2) == 0) {
        lookup_name = name + 2;  // Skip the :: prefix
        top_level_only = true;
    }

    // Handle class-qualified variable names like $secrets::root, $apt::params::provider
    // Look for :: in the name (after handling leading ::)
    const char *last_sep = strrchr(lookup_name, ':');
    if (last_sep && last_sep > lookup_name && *(last_sep - 1) == ':') {
        // This is a class-qualified variable like "secrets::root" or "apt::params::provider"
        // Split into class_name and var_name at the last ::
        size_t class_len = (last_sep - 1) - lookup_name;
        char *class_name = puppet_malloc(class_len + 1);
        strncpy(class_name, lookup_name, class_len);
        class_name[class_len] = '\0';
        const char *var_name = last_sep + 1;

        // First check the class_scopes registry (for previously included classes)
        if (env->class_scopes) {
            puppet_scope_t *stored_scope = (puppet_scope_t *)puppet_hash_get(
                env->class_scopes, class_name, strlen(class_name));
            if (stored_scope) {
                // Use recursive=true to search parent scopes (for inherited class variables)
                value = puppet_scope_get_var(stored_scope, var_name, true);
                puppet_free(class_name);
                return value;
            } else {
                puppet_debug("Class %s not found in class_scopes", class_name);
            }
        }

        // Look up the class scope - search through the scope stack
        puppet_scope_t *scope = env->current_scope;
        while (scope) {
            if (scope->name.data && strcmp(scope->name.data, class_name) == 0) {
                // Use recursive=true to search parent scopes
                value = puppet_scope_get_var(scope, var_name, true);
                puppet_free(class_name);
                return value;  // Return even if NULL - variable should be in this scope
            }
            scope = scope->parent;
        }

        // Also check if class_scope matches (current class being executed)
        if (env->class_scope && env->class_scope->name.data &&
            strcmp(env->class_scope->name.data, class_name) == 0) {
            // Use recursive=true to search parent scopes
            value = puppet_scope_get_var(env->class_scope, var_name, true);
            puppet_free(class_name);
            return value;
        }

        puppet_free(class_name);
        // Class scope not found - fall through to return NULL
        return NULL;
    }

    // If top-level only, skip local and class scopes
    if (!top_level_only) {
        // 1. Local scope (current scope, non-recursive)
        value = puppet_scope_get_var(env->current_scope, lookup_name, false);
        if (value) return value;

        // 2. Class scope (if we're inside a class)
        if (env->class_scope && env->class_scope != env->current_scope) {
            value = puppet_scope_get_var(env->class_scope, lookup_name, false);
            if (value) return value;
        }

        // 3. Node scope (node-specific variables)
        if (env->node_scope && env->node_scope != env->current_scope) {
            value = puppet_scope_get_var(env->node_scope, lookup_name, false);
            if (value) return value;
        }
    }

    // 4. Global scope (top-level variables)
    if (env->global_scope != env->current_scope || top_level_only) {
        value = puppet_scope_get_var(env->global_scope, lookup_name, false);
        if (value) return value;
    }

    // 5. Facts lookup
    if (env->facts_db) {
        // Special handling for $facts - return the whole facts hash
        if (strcmp(lookup_name, "facts") == 0) {
            value = puppet_facts_get_all_as_hash(env);
            if (value) return value;
        }
        // Direct fact access (e.g., $hostname, $operatingsystem)
        value = puppet_facts_get(env, lookup_name);
        if (value) return value;
    }
    
    // 6. Data providers (Hiera, external data sources)
    if (!top_level_only) {
        for (size_t i = 0; i < env->data_provider_count; i++) {
            puppet_data_provider_t *provider = env->data_providers[i];
            if (provider && provider->lookup) {
                value = provider->lookup(lookup_name, env, provider->data);
                if (value) return value;
            }
        }
    }

    // 7. Not found
    return NULL;
}

/**
 * @brief Look up variable in specific scope type
 *
 * @param env Execution environment
 * @param name Variable name
 * @param scope Scope type to search
 * @return Variable value or NULL if not found
 */
puppet_value_t *puppet_variable_lookup_scoped(puppet_env_t *env, const char *name, puppet_var_scope_t scope) {
    if (!env || !name) return NULL;
    
    switch (scope) {
        case PUPPET_VAR_LOCAL:
            return puppet_scope_get_var(env->current_scope, name, false);
            
        case PUPPET_VAR_CLASS:
            return env->class_scope ? 
                puppet_scope_get_var(env->class_scope, name, false) : NULL;
                
        case PUPPET_VAR_NODE:
            return env->node_scope ? 
                puppet_scope_get_var(env->node_scope, name, false) : NULL;
                
        case PUPPET_VAR_GLOBAL:
            return puppet_scope_get_var(env->global_scope, name, false);
            
        case PUPPET_VAR_FACT:
            // Facts would be handled by a fact provider
            // For now, fall through to data providers
            for (size_t i = 0; i < env->data_provider_count; i++) {
                puppet_data_provider_t *provider = env->data_providers[i];
                if (provider && provider->lookup) {
                    puppet_value_t *value = provider->lookup(name, env, provider->data);
                    if (value) return value;
                }
            }
            return NULL;
            
        default:
            return NULL;
    }
}

/**
 * @brief Set variable in specific scope
 *
 * @param env Execution environment
 * @param name Variable name
 * @param value Variable value
 * @param scope Target scope type
 */
void puppet_env_set_scoped_var(puppet_env_t *env, const char *name, puppet_value_t *value, puppet_var_scope_t scope) {
    if (!env || !name) return;
    
    switch (scope) {
        case PUPPET_VAR_LOCAL:
            puppet_scope_set_var(env->current_scope, name, value);
            break;
            
        case PUPPET_VAR_CLASS:
            if (env->class_scope) {
                puppet_scope_set_var(env->class_scope, name, value);
            } else {
                // Create class scope if it doesn't exist
                env->class_scope = puppet_scope_create(env->global_scope, "class");
                puppet_scope_set_var(env->class_scope, name, value);
            }
            break;
            
        case PUPPET_VAR_NODE:
            puppet_scope_set_var(env->node_scope, name, value);
            break;
            
        case PUPPET_VAR_GLOBAL:
            puppet_scope_set_var(env->global_scope, name, value);
            break;
            
        case PUPPET_VAR_FACT:
            // Facts are typically read-only, but we could support setting
            // them in node scope for now
            puppet_scope_set_var(env->node_scope, name, value);
            break;
    }
}

/*
 * ===========================================================================
 * DATA PROVIDER MANAGEMENT
 * ===========================================================================
 */

/**
 * @brief Register a data provider (Hiera, etc.)
 *
 * @param env Execution environment
 * @param provider Data provider to register
 * @return 0 on success, -1 on error
 */
int puppet_register_data_provider(puppet_env_t *env, puppet_data_provider_t *provider) {
    if (!env || !provider) return -1;
    
    // Expand provider array if needed
    if (env->data_provider_count >= env->data_provider_capacity) {
        env->data_provider_capacity *= 2;
        env->data_providers = puppet_realloc(env->data_providers, 
            env->data_provider_capacity * sizeof(puppet_data_provider_t*));
        if (!env->data_providers) {
            return -1;
        }
    }
    
    // Add provider to array
    env->data_providers[env->data_provider_count] = provider;
    env->data_provider_count++;
    
    return 0;
}

/**
 * @brief Unregister data provider by name
 *
 * @param env Execution environment
 * @param name Provider name to remove
 */
void puppet_unregister_data_provider(puppet_env_t *env, const char *name) {
    if (!env || !name) return;
    
    for (size_t i = 0; i < env->data_provider_count; i++) {
        puppet_data_provider_t *provider = env->data_providers[i];
        if (provider && provider->name && strcmp(provider->name, name) == 0) {
            // Clean up provider
            if (provider->cleanup) {
                provider->cleanup(provider->data);
            }
            puppet_free(provider->name);
            puppet_free(provider);
            
            // Shift remaining providers down
            for (size_t j = i; j < env->data_provider_count - 1; j++) {
                env->data_providers[j] = env->data_providers[j + 1];
            }
            env->data_provider_count--;
            break;
        }
    }
}

/**
 * @brief Get data provider by name
 *
 * @param env Execution environment
 * @param name Provider name to find
 * @return Provider pointer or NULL if not found
 */
puppet_data_provider_t *puppet_get_data_provider(puppet_env_t *env, const char *name) {
    if (!env || !name) return NULL;
    
    for (size_t i = 0; i < env->data_provider_count; i++) {
        puppet_data_provider_t *provider = env->data_providers[i];
        if (provider && provider->name && strcmp(provider->name, name) == 0) {
            return provider;
        }
    }
    
    return NULL;
}

/*
 * ===========================================================================
 * FACTS DATABASE IMPLEMENTATION
 * ===========================================================================
 */

#include "puppet_json_parser.h"

puppet_facts_db_t *puppet_facts_db_create(void) {
    puppet_facts_db_t *facts_db = puppet_calloc(1, sizeof(puppet_facts_db_t));
    facts_db->node_count = 0;
    facts_db->node_capacity = 4;
    facts_db->nodes = puppet_calloc(facts_db->node_capacity, sizeof(puppet_node_facts_t));
    facts_db->node_index = puppet_calloc(1, sizeof(puppet_hash_t));
    facts_db->node_index->bucket_count = 16;
    facts_db->node_index->buckets = puppet_calloc(facts_db->node_index->bucket_count, sizeof(puppet_hash_entry_t*));
    facts_db->current_node = NULL;
    
    return facts_db;
}

void puppet_facts_db_destroy(puppet_facts_db_t *facts_db) {
    if (!facts_db) return;
    
    // Clean up nodes
    for (size_t i = 0; i < facts_db->node_count; i++) {
        puppet_node_facts_t *node = &facts_db->nodes[i];
        puppet_free(node->certname);
        puppet_free(node->environment);
        
        // Clean up facts hash table
        if (node->facts) {
            for (size_t j = 0; j < node->facts->bucket_count; j++) {
                puppet_hash_entry_t *entry = node->facts->buckets[j];
                while (entry) {
                    puppet_hash_entry_t *next = entry->next;
                    puppet_string_free(entry->key);
                    puppet_value_destroy(entry->value);
                    puppet_free(entry);
                    entry = next;
                }
            }
            puppet_free(node->facts->buckets);
            puppet_free(node->facts);
        }
    }
    puppet_free(facts_db->nodes);
    
    // Clean up node index
    for (size_t i = 0; i < facts_db->node_index->bucket_count; i++) {
        puppet_hash_entry_t *entry = facts_db->node_index->buckets[i];
        while (entry) {
            puppet_hash_entry_t *next = entry->next;
            puppet_string_free(entry->key);
            puppet_value_destroy(entry->value); // Safe to destroy index values
            puppet_free(entry);
            entry = next;
        }
    }
    puppet_free(facts_db->node_index->buckets);
    puppet_free(facts_db->node_index);
    
    puppet_free(facts_db->current_node);
    puppet_free(facts_db);
}

static int puppet_facts_db_add_node(puppet_facts_db_t *facts_db, const char *certname, const char *environment) {
    if (!facts_db || !certname) return -1;
    
    // Expand array if needed
    if (facts_db->node_count >= facts_db->node_capacity) {
        facts_db->node_capacity *= 2;
        facts_db->nodes = puppet_realloc(facts_db->nodes, facts_db->node_capacity * sizeof(puppet_node_facts_t));
    }
    
    // Initialize new node
    puppet_node_facts_t *node = &facts_db->nodes[facts_db->node_count];
    node->certname = puppet_strdup(certname);
    node->environment = environment ? puppet_strdup(environment) : NULL;
    node->facts = puppet_calloc(1, sizeof(puppet_hash_t));
    node->facts->bucket_count = 16;
    node->facts->buckets = puppet_calloc(node->facts->bucket_count, sizeof(puppet_hash_entry_t*));
    
    // Add to index (store array index as number)
    puppet_value_t *index_value = puppet_value_create_number((double)facts_db->node_count);
    puppet_hash_set(facts_db->node_index, certname, strlen(certname), index_value);
    
    facts_db->node_count++;
    return 0;
}

static void puppet_facts_add_fact(puppet_node_facts_t *node, const char *fact_name, json_value_t *json_val) {
    if (!node || !fact_name || !json_val) return;

    puppet_value_t *puppet_val = json_value_to_puppet_value(json_val);
    puppet_hash_set(node->facts, fact_name, strlen(fact_name), puppet_val);
}

/* YAML facts support - add fact directly from puppet_value_t */
static void puppet_facts_add_from_value(puppet_node_facts_t *node, const char *fact_name, puppet_value_t *value) {
    if (!node || !fact_name || !value) return;
    puppet_hash_set(node->facts, fact_name, strlen(fact_name), puppet_value_copy(value));
}

/* Process YAML-loaded puppet_value_t hash recursively */
static void puppet_facts_process_value(puppet_node_facts_t *node, const char *prefix, puppet_value_t *obj) {
    if (!node || !obj || obj->type != PUPPET_VALUE_HASH) return;

    puppet_hash_t *hash = obj->data.hash;
    for (size_t b = 0; b < hash->bucket_count; b++) {
        puppet_hash_entry_t *entry = hash->buckets[b];
        while (entry) {
            const char *key = entry->key.data;
            puppet_value_t *value = entry->value;

            /* Create fully qualified fact name */
            char *fact_name;
            if (prefix && strlen(prefix) > 0) {
                size_t len = strlen(prefix) + strlen(key) + 2;
                fact_name = puppet_malloc(len);
                snprintf(fact_name, len, "%s.%s", prefix, key);
            } else {
                fact_name = puppet_strdup(key);
            }

            if (value->type == PUPPET_VALUE_HASH) {
                /* Recursively process nested hashes */
                puppet_facts_process_value(node, fact_name, value);
            } else {
                /* Add leaf fact */
                puppet_facts_add_from_value(node, fact_name, value);
            }

            /* Also add top-level key for direct access */
            if (!prefix || strlen(prefix) == 0) {
                puppet_facts_add_from_value(node, key, value);
            }

            puppet_free(fact_name);
            entry = entry->next;
        }
    }
}

static void puppet_facts_process_object(puppet_node_facts_t *node, const char *prefix, json_value_t *obj) {
    if (!node || !obj || obj->type != JSON_VALUE_OBJECT) return;
    
    for (size_t i = 0; i < obj->data.object.count; i++) {
        const char *key = obj->data.object.keys[i];
        json_value_t *value = obj->data.object.values[i];
        
        // Create fully qualified fact name
        char *fact_name;
        if (prefix && strlen(prefix) > 0) {
            size_t len = strlen(prefix) + strlen(key) + 2; // +2 for '.' and '\0'
            fact_name = puppet_malloc(len);
            snprintf(fact_name, len, "%s.%s", prefix, key);
        } else {
            fact_name = puppet_strdup(key);
        }
        
        if (value->type == JSON_VALUE_OBJECT) {
            // Recursively process nested objects
            puppet_facts_process_object(node, fact_name, value);
        } else {
            // Add leaf fact
            puppet_facts_add_fact(node, fact_name, value);
        }
        
        // Also add top-level key for direct access (e.g., $os instead of just $os.name)
        if (!prefix || strlen(prefix) == 0) {
            puppet_facts_add_fact(node, key, value);
        }
        
        puppet_free(fact_name);
    }
}

static int puppet_facts_load_facter_format(puppet_facts_db_t *facts_db, json_value_t *root) {
    if (!facts_db || !root || root->type != JSON_VALUE_OBJECT) return -1;
    
    // Facter format: single object with facts
    // Determine node name from hostname or use "localhost"
    json_value_t *hostname_val = json_object_get(root, "hostname");
    json_value_t *networking = json_object_get(root, "networking");
    json_value_t *fqdn_val = networking ? json_object_get(networking, "fqdn") : NULL;
    
    const char *node_name = "localhost";
    if (fqdn_val && fqdn_val->type == JSON_VALUE_STRING) {
        node_name = fqdn_val->data.string_value;
    } else if (hostname_val && hostname_val->type == JSON_VALUE_STRING) {
        node_name = hostname_val->data.string_value;
    }
    
    // Add node
    if (puppet_facts_db_add_node(facts_db, node_name, NULL) < 0) {
        return -1;
    }
    
    puppet_node_facts_t *node = &facts_db->nodes[facts_db->node_count - 1];
    
    // Process all facts
    puppet_facts_process_object(node, NULL, root);
    
    // Set as current node
    puppet_facts_db_set_current_node(facts_db, node_name);
    
    return 0;
}

static int puppet_facts_load_puppetdb_format(puppet_facts_db_t *facts_db, json_value_t *root) {
    if (!facts_db || !root || root->type != JSON_VALUE_ARRAY) return -1;

    // PuppetDB format: array of node objects
    for (size_t i = 0; i < root->data.array.count; i++) {
        json_value_t *node_obj = root->data.array.elements[i];
        if (node_obj->type != JSON_VALUE_OBJECT) continue;

        json_value_t *certname = json_object_get(node_obj, "certname");
        json_value_t *environment = json_object_get(node_obj, "environment");
        json_value_t *facts = json_object_get(node_obj, "facts");

        if (!certname || certname->type != JSON_VALUE_STRING || !facts) continue;

        const char *node_name = certname->data.string_value;
        const char *env_name = (environment && environment->type == JSON_VALUE_STRING) ?
                               environment->data.string_value : NULL;

        // Add node
        if (puppet_facts_db_add_node(facts_db, node_name, env_name) < 0) {
            continue;
        }

        puppet_node_facts_t *node = &facts_db->nodes[facts_db->node_count - 1];

        // Process facts object
        puppet_facts_process_object(node, NULL, facts);
    }

    return 0;
}

/* Load facts from YAML file */
static int puppet_facts_load_yaml(puppet_facts_db_t *facts_db, const char *filepath) {
    if (!facts_db || !filepath) return -1;

    puppet_value_t *root = puppet_hiera_load_yaml(filepath);
    if (!root) {
        puppet_error("Failed to parse YAML facts file: %s", filepath);
        return -1;
    }

    if (root->type != PUPPET_VALUE_HASH) {
        puppet_error("YAML facts file must be a hash: %s", filepath);
        puppet_value_destroy(root);
        return -1;
    }

    /* Check for multi-node format: { facts: { node1: {...}, node2: {...} } } */
    puppet_value_t *facts_root = puppet_hash_get(root->data.hash, "facts", 5);
    if (facts_root && facts_root->type == PUPPET_VALUE_HASH) {
        /* Multi-node format - iterate over all nodes */
        puppet_hash_t *nodes_hash = facts_root->data.hash;
        for (size_t b = 0; b < nodes_hash->bucket_count; b++) {
            puppet_hash_entry_t *entry = nodes_hash->buckets[b];
            while (entry) {
                const char *node_name = entry->key.data;
                puppet_value_t *node_facts = entry->value;

                if (node_facts && node_facts->type == PUPPET_VALUE_HASH) {
                    /* Add this node */
                    if (puppet_facts_db_add_node(facts_db, node_name, NULL) == 0) {
                        puppet_node_facts_t *node = &facts_db->nodes[facts_db->node_count - 1];
                        puppet_facts_process_value(node, NULL, node_facts);
                    }
                }
                entry = entry->next;
            }
        }
        puppet_value_destroy(root);
        return 0;
    }

    /* Single-node format - determine node name from fqdn or hostname */
    const char *node_name = "localhost";
    puppet_value_t *fqdn_val = puppet_hash_get(root->data.hash, "fqdn", 4);
    puppet_value_t *hostname_val = puppet_hash_get(root->data.hash, "hostname", 8);

    if (fqdn_val && fqdn_val->type == PUPPET_VALUE_STRING) {
        node_name = fqdn_val->data.string.data;
    } else if (hostname_val && hostname_val->type == PUPPET_VALUE_STRING) {
        node_name = hostname_val->data.string.data;
    }

    /* Add node */
    if (puppet_facts_db_add_node(facts_db, node_name, NULL) < 0) {
        puppet_value_destroy(root);
        return -1;
    }

    puppet_node_facts_t *node = &facts_db->nodes[facts_db->node_count - 1];

    /* Process all facts */
    puppet_facts_process_value(node, NULL, root);

    /* Set as current node */
    puppet_facts_db_set_current_node(facts_db, node_name);

    puppet_value_destroy(root);
    return 0;
}

/* Check if filepath has YAML extension */
static bool is_yaml_file(const char *filepath) {
    if (!filepath) return false;
    size_t len = strlen(filepath);
    if (len >= 5 && strcmp(filepath + len - 5, ".yaml") == 0) return true;
    if (len >= 4 && strcmp(filepath + len - 4, ".yml") == 0) return true;
    return false;
}

int puppet_facts_db_load_file(puppet_facts_db_t *facts_db, const char *filepath) {
    if (!facts_db || !filepath) return -1;

    /* Check for YAML file first */
    if (is_yaml_file(filepath)) {
        return puppet_facts_load_yaml(facts_db, filepath);
    }

    /* Try JSON parsing */
    json_value_t *root = json_parse_file(filepath);
    if (!root) {
        puppet_error("Failed to parse facts file: %s", filepath);
        return -1;
    }

    int result;
    if (root->type == JSON_VALUE_ARRAY) {
        // PuppetDB format
        result = puppet_facts_load_puppetdb_format(facts_db, root);
    } else if (root->type == JSON_VALUE_OBJECT) {
        // Facter format
        result = puppet_facts_load_facter_format(facts_db, root);
    } else {
        puppet_error("Unsupported facts file format");
        result = -1;
    }
    
    json_value_destroy(root);
    return result;
}

int puppet_facts_db_load_json(puppet_facts_db_t *facts_db, const char *certname,
                               void *facts_json_ptr) {
    json_value_t *facts_json = (json_value_t *)facts_json_ptr;
    if (!facts_db || !certname || !facts_json || facts_json->type != JSON_VALUE_OBJECT) {
        return -1;
    }

    /* Add node with the given certname */
    if (puppet_facts_db_add_node(facts_db, certname, NULL) < 0) {
        return -1;
    }

    puppet_node_facts_t *node = &facts_db->nodes[facts_db->node_count - 1];

    /* Process all facts from the JSON object */
    puppet_facts_process_object(node, NULL, facts_json);

    /* Set as current node */
    puppet_facts_db_set_current_node(facts_db, certname);

    return 0;
}

int puppet_facts_db_set_current_node(puppet_facts_db_t *facts_db, const char *certname) {
    if (!facts_db || !certname) return -1;

    // Find node index
    puppet_value_t *index_value = puppet_hash_get(facts_db->node_index, certname, strlen(certname));
    if (!index_value || index_value->type != PUPPET_VALUE_NUMBER) {
        puppet_debug("Node '%s' not found in facts database", certname);
        return -1;
    }

    size_t index = (size_t)index_value->data.number;
    if (index >= facts_db->node_count) {
        puppet_warn("Invalid node index for '%s'", certname);
        return -1;
    }

    // Set current node
    puppet_free(facts_db->current_node);
    facts_db->current_node = puppet_strdup(certname);

    return 0;
}

size_t puppet_facts_db_node_count(puppet_facts_db_t *facts_db) {
    if (!facts_db) return 0;
    return facts_db->node_count;
}

const char *puppet_facts_db_get_node_name(puppet_facts_db_t *facts_db, size_t index) {
    if (!facts_db || index >= facts_db->node_count) return NULL;
    return facts_db->nodes[index].certname;
}

puppet_value_t *puppet_facts_get(puppet_env_t *env, const char *fact_name) {
    if (!fact_name) {
        return NULL;
    }

    /* Hardcoded facts - puppetversion is always "puppetc" */
    if (strcmp(fact_name, "puppetversion") == 0) {
        return puppet_value_create_string("puppetc", 7);
    }

    if (!env || !env->facts_db) {
        return NULL;
    }

    puppet_facts_db_t *facts_db = env->facts_db;
    if (!facts_db->current_node) {
        return NULL;
    }
    
    // Find current node by index
    puppet_value_t *index_value = puppet_hash_get(facts_db->node_index, facts_db->current_node, strlen(facts_db->current_node));
    if (!index_value || index_value->type != PUPPET_VALUE_NUMBER) {
        return NULL;
    }
    
    size_t index = (size_t)index_value->data.number;
    if (index >= facts_db->node_count) {
        return NULL;
    }
    
    puppet_node_facts_t *node = &facts_db->nodes[index];
    
    // Look up fact
    puppet_value_t *fact_value = puppet_hash_get(node->facts, fact_name, strlen(fact_name));
    if (!fact_value) {
        return NULL;
    }
    
    // Return copy to avoid double-free
    switch (fact_value->type) {
        case PUPPET_VALUE_UNDEF:
            return puppet_value_create_undef();
        case PUPPET_VALUE_BOOL:
            return puppet_value_create_bool(fact_value->data.boolean);
        case PUPPET_VALUE_NUMBER:
            return puppet_value_create_number(fact_value->data.number);
        case PUPPET_VALUE_STRING:
            return puppet_value_create_string(fact_value->data.string.data, fact_value->data.string.len);
        default:
            return puppet_value_create_undef();
    }
}

/**
 * @brief Get all facts as a nested hash for $facts access
 */
puppet_value_t *puppet_facts_get_all_as_hash(puppet_env_t *env) {
    if (!env || !env->facts_db) {
        return NULL;
    }

    puppet_facts_db_t *facts_db = env->facts_db;
    if (!facts_db->current_node) return NULL;

    /* Find current node */
    puppet_value_t *index_value = puppet_hash_get(facts_db->node_index,
        facts_db->current_node, strlen(facts_db->current_node));
    if (!index_value || index_value->type != PUPPET_VALUE_NUMBER) return NULL;

    size_t index = (size_t)index_value->data.number;
    if (index >= facts_db->node_count) return NULL;

    puppet_node_facts_t *node = &facts_db->nodes[index];
    if (!node->facts) return NULL;

    /* Create root hash for $facts */
    puppet_value_t *root = puppet_value_create_hash();

    /* Iterate through all facts and build nested structure */
    for (size_t i = 0; i < node->facts->bucket_count; i++) {
        puppet_hash_entry_t *entry = node->facts->buckets[i];
        while (entry) {
            const char *fact_name = entry->key.data;
            puppet_value_t *fact_value = entry->value;

            /* Split dotted name and create nested hashes */
            puppet_value_t *current = root;
            char *name_copy = puppet_strdup(fact_name);
            char *token = strtok(name_copy, ".");
            char *next_token = strtok(NULL, ".");

            while (token) {
                if (!next_token) {
                    /* Last token - set the value */
                    puppet_value_t *val_copy = puppet_value_copy(fact_value);
                    puppet_hash_set(current->data.hash, token, strlen(token), val_copy);
                } else {
                    /* Intermediate token - get or create nested hash */
                    puppet_value_t *nested = puppet_hash_get(current->data.hash,
                        token, strlen(token));
                    if (!nested || nested->type != PUPPET_VALUE_HASH) {
                        nested = puppet_value_create_hash();
                        puppet_hash_set(current->data.hash, token, strlen(token), nested);
                    }
                    current = nested;
                }
                token = next_token;
                next_token = strtok(NULL, ".");
            }

            puppet_free(name_copy);
            entry = entry->next;
        }
    }

    /* Add hardcoded facts */
    puppet_value_t *puppetversion = puppet_value_create_string("puppetc", 7);
    puppet_hash_set(root->data.hash, "puppetversion", 13, puppetversion);

    return root;
}

int puppet_env_set_facts_db(puppet_env_t *env, puppet_facts_db_t *facts_db) {
    if (!env) return -1;
    
    if (env->facts_db) {
        puppet_facts_db_destroy(env->facts_db);
    }
    
    env->facts_db = facts_db;
    return 0;
}