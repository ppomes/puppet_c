/*
 * ERB Template Integration for Puppet C Parser
 * 
 * This module provides ERB template processing capabilities by embedding
 * the Ruby interpreter.
 * 
 * Features:
 * - Full Ruby VM embedding for native ERB support
 * - Puppet value to Ruby object conversion
 * - ERB template processing with variable interpolation
 * - Proper handling of facts and Puppet variables
 * - Memory management for Ruby integration
 */

#include "puppet_erb.h"
#include "puppet_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>

#include <ruby.h>
#include <ruby/encoding.h>

// Global Ruby context for VM management
static puppet_ruby_context_t *global_ruby_ctx = NULL;

// Global mutex for Ruby VM access (Ruby is not thread-safe)
static pthread_mutex_t ruby_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Initialize Ruby VM for ERB template processing
 * 
 * This function sets up the Ruby interpreter with full environment including
 * load paths and encoding configuration. Creates a fresh context each time
 * to avoid state issues.
 * 
 * @return Initialized Ruby context
 */
puppet_ruby_context_t *puppet_ruby_init(void) {
    // Acquire lock for thread-safe initialization
    pthread_mutex_lock(&ruby_mutex);

    // Return existing context if already initialized - Ruby can only be initialized once
    if (global_ruby_ctx && global_ruby_ctx->initialized) {
        pthread_mutex_unlock(&ruby_mutex);
        return global_ruby_ctx;
    }

    puppet_ruby_context_t *ctx = puppet_calloc(1, sizeof(puppet_ruby_context_t));

    // Initialize Ruby VM with proper setup
    {
        int argc = 1;
        char *argv[] = {"puppet", NULL};
        char **ruby_argv = argv;

        ruby_sysinit(&argc, &ruby_argv);
        ruby_init();
        ruby_init_loadpath();

        // Suppress "already initialized constant" warnings from Ruby stdlib
        // These occur when tmpdir.rb redefines TMP_RUBY_PREFIX
        rb_eval_string("$VERBOSE = nil");

        // Use ruby_options() with -e "nil" to avoid stdin blocking
        char *ruby_specific_argv[] = {"puppet", "-e", "nil", NULL};
        void *node = ruby_options(3, ruby_specific_argv);
        if (!node) {
            printf("Warning: Ruby options processing failed\n");
        }
    }

    // Load ERB library
    int state = 0;
    (void)rb_eval_string_protect("require 'erb'", &state);
    if (state != 0) {
        printf("Error: Failed to load ERB library (state=%d)\n", state);
        if (state == 6) {  // TAG_RAISE - exception occurred
            VALUE exception = rb_errinfo();
            if (!NIL_P(exception)) {
                VALUE message = rb_obj_as_string(exception);
                printf("Ruby exception: %s\n", StringValueCStr(message));
            }
        }
        ctx->initialized = 0;
    }

    // Define a Puppet scope class for lookupvar support in ERB templates
    // This mimics Puppet's scope.lookupvar('classname::varname') behavior
    (void)rb_eval_string_protect(
        "class PuppetScope\n"
        "  def initialize(vars)\n"
        "    @vars = vars\n"
        "  end\n"
        "  def lookupvar(name)\n"
        "    # Remove leading :: if present\n"
        "    name = name.sub(/^::/, '') if name.start_with?('::')\n"
        "    # Try direct lookup first\n"
        "    return @vars[name] if @vars.key?(name)\n"
        "    # Try with underscores replaced for nested names (classname::varname)\n"
        "    safe_name = name.gsub('::', '__')\n"
        "    return @vars[safe_name] if @vars.key?(safe_name)\n"
        "    # Return empty string for undefined variables (like real Puppet ERB)\n"
        "    ''\n"
        "  end\n"
        "  def [](name)\n"
        "    lookupvar(name)\n"
        "  end\n"
        "  def has_variable?(name)\n"
        "    # Remove leading :: if present\n"
        "    name = name.sub(/^::/, '') if name.start_with?('::')\n"
        "    return true if @vars.key?(name)\n"
        "    safe_name = name.gsub('::', '__')\n"
        "    @vars.key?(safe_name)\n"
        "  end\n"
        "end\n"
        "$puppet_vars = {}\n"
        "# Define has_variable? in main scope for templates that call it directly\n"
        "def has_variable?(name)\n"
        "  $scope.has_variable?(name) if $scope\n"
        "end\n",
        &state);
    if (state != 0) {
        printf("Warning: Failed to define PuppetScope class (state=%d)\n", state);
    }

    ctx->initialized = 1;

    global_ruby_ctx = ctx;
    pthread_mutex_unlock(&ruby_mutex);
    return ctx;
}

/**
 * Clean up Ruby context and release resources
 * 
 * Note: Ruby VM cleanup is skipped to avoid state corruption issues.
 * Only the context memory is freed.
 * 
 * @param ctx Ruby context to clean up
 */
void puppet_ruby_cleanup(puppet_ruby_context_t *ctx) {
    if (!ctx || !ctx->initialized) return;
    
    // ruby_cleanup(0) is intentionally skipped - causes state corruption
    ctx->initialized = 0;
    
    // Clear global reference if this was the global context
    if (global_ruby_ctx == ctx) {
        global_ruby_ctx = NULL;
    }
    puppet_free(ctx);
}

/**
 * Convert Puppet values to Ruby objects
 * 
 * This function provides bidirectional data conversion between Puppet's
 * internal value system and Ruby's object system, enabling seamless
 * integration for ERB template processing.
 * 
 * @param value Puppet value to convert
 * @param ctx Ruby context for conversion
 * @return Ruby VALUE object (cast to void* for API compatibility)
 */
void *puppet_value_to_ruby(puppet_value_t *value, puppet_ruby_context_t *ctx) {
    if (!ctx || !ctx->initialized || !value) {
        return (void*)Qnil;
    }
    
    switch (value->type) {
        case PUPPET_VALUE_UNDEF:
            return (void*)Qnil;
            
        case PUPPET_VALUE_BOOL:
            return (void*)(value->data.boolean ? Qtrue : Qfalse);
            
        case PUPPET_VALUE_STRING:
            // Create Ruby string with proper length handling
            return (void*)rb_str_new(value->data.string.data, value->data.string.len);
            
        case PUPPET_VALUE_NUMBER: {
            // Preserve integer vs float distinction
            if (value->data.number == (long)value->data.number) {
                return (void*)LONG2NUM((long)value->data.number);
            } else {
                return (void*)rb_float_new(value->data.number);
            }
        }
            
        case PUPPET_VALUE_ARRAY: {
            // Convert Puppet arrays to Ruby arrays recursively
            VALUE ary = rb_ary_new();
            for (size_t i = 0; i < value->data.array->count; i++) {
                VALUE elem = (VALUE)puppet_value_to_ruby(value->data.array->items[i], ctx);
                rb_ary_push(ary, elem);
            }
            return (void*)ary;
        }
            
        case PUPPET_VALUE_HASH: {
            VALUE hash = rb_hash_new();
            /* Iterate over all buckets and entries */
            for (size_t i = 0; i < value->data.hash->bucket_count; i++) {
                puppet_hash_entry_t *entry = value->data.hash->buckets[i];
                while (entry) {
                    VALUE key = rb_str_new(entry->key.data, entry->key.len);
                    VALUE val = (VALUE)puppet_value_to_ruby(entry->value, ctx);
                    rb_hash_aset(hash, key, val);
                    entry = entry->next;
                }
            }
            return (void*)hash;
        }
            
        default:
            return (void*)Qnil;
    }
}

puppet_value_t *ruby_to_puppet_value(void *ruby_obj, puppet_ruby_context_t *ctx) {
    if (!ctx || !ctx->initialized) {
        return puppet_value_create_undef();
    }
    
    VALUE obj = (VALUE)ruby_obj;
    
    switch (TYPE(obj)) {
        case T_NIL:
            return puppet_value_create_undef();
            
        case T_TRUE:
            return puppet_value_create_bool(true);
            
        case T_FALSE:
            return puppet_value_create_bool(false);
            
        case T_STRING: {
            const char *str = StringValueCStr(obj);
            return puppet_value_create_string(str, strlen(str));
        }
            
        case T_FIXNUM:
            return puppet_value_create_number((double)NUM2LONG(obj));
            
        case T_FLOAT:
            return puppet_value_create_number(NUM2DBL(obj));
            
        case T_ARRAY: {
            puppet_value_t *array = puppet_value_create_array();
            long len = RARRAY_LEN(obj);
            for (long i = 0; i < len; i++) {
                VALUE elem = rb_ary_entry(obj, i);
                puppet_value_t *puppet_elem = ruby_to_puppet_value((void*)elem, ctx);
                puppet_array_append(array->data.array, puppet_elem);
            }
            return array;
        }
            
        default:
            // Convert to string as fallback
            VALUE str_obj = rb_obj_as_string(obj);
            const char *str = StringValueCStr(str_obj);
            return puppet_value_create_string(str, strlen(str));
    }
}

static void puppet_set_ruby_variable(const char *name, puppet_value_t *value, puppet_ruby_context_t *ctx) {
    VALUE ruby_val = (VALUE)puppet_value_to_ruby(value, ctx);

    // Convert variable name to Ruby instance variable format (for ERB templates)
    // ERB templates access variables as @variable, not $variable
    // Also convert dots to underscores since Ruby variables can't contain dots
    char var_name[256];
    char clean_name[256];
    strncpy(clean_name, name, sizeof(clean_name) - 1);
    clean_name[sizeof(clean_name) - 1] = '\0';

    // Replace dots with underscores for Ruby variable names
    for (char *p = clean_name; *p; p++) {
        if (*p == '.') *p = '_';
    }

    snprintf(var_name, sizeof(var_name), "@%s", clean_name);

    // Set as instance variable in the main object
    VALUE main_obj = rb_eval_string("self");
    rb_iv_set(main_obj, var_name, ruby_val);

    // Also set as global variable for backward compatibility
    char global_var_name[256];
    snprintf(global_var_name, sizeof(global_var_name), "$%s", clean_name);
    rb_gv_set(global_var_name, ruby_val);

    // Also add to $puppet_vars hash for scope.lookupvar() support
    VALUE puppet_vars = rb_gv_get("$puppet_vars");
    if (!NIL_P(puppet_vars) && TYPE(puppet_vars) == T_HASH) {
        VALUE key = rb_str_new2(name);  // Use original name with :: for scope lookups
        rb_hash_aset(puppet_vars, key, ruby_val);
        // Also store with clean name
        if (strcmp(name, clean_name) != 0) {
            VALUE clean_key = rb_str_new2(clean_name);
            rb_hash_aset(puppet_vars, clean_key, ruby_val);
        }
    }
}

static void puppet_export_env_to_ruby(puppet_env_t *env, puppet_ruby_context_t *ruby_ctx) {
    if (!env || !ruby_ctx) return;

    // Export all variables from current scope chain
    puppet_scope_t *scope = env->current_scope;
    while (scope) {
        if (scope->variables) {
            for (size_t i = 0; i < scope->variables->bucket_count; i++) {
                puppet_hash_entry_t *entry = scope->variables->buckets[i];
                while (entry) {
                    puppet_set_ruby_variable(entry->key.data, entry->value, ruby_ctx);
                    entry = entry->next;
                }
            }
        }
        scope = scope->parent;
    }

    // Export variables from all class scopes (for scope.lookupvar('class::var'))
    if (env->class_scopes) {
        for (size_t i = 0; i < env->class_scopes->bucket_count; i++) {
            puppet_hash_entry_t *class_entry = env->class_scopes->buckets[i];
            while (class_entry) {
                const char *class_name = class_entry->key.data;
                puppet_scope_t *class_scope = (puppet_scope_t *)class_entry->value;

                if (class_scope && class_scope->variables) {
                    for (size_t j = 0; j < class_scope->variables->bucket_count; j++) {
                        puppet_hash_entry_t *var_entry = class_scope->variables->buckets[j];
                        while (var_entry) {
                            // Export as class::varname format for scope.lookupvar()
                            char qualified_name[512];
                            snprintf(qualified_name, sizeof(qualified_name), "%s::%s",
                                     class_name, var_entry->key.data);
                            puppet_set_ruby_variable(qualified_name, var_entry->value, ruby_ctx);
                            var_entry = var_entry->next;
                        }
                    }
                }
                class_entry = class_entry->next;
            }
        }
    }

    // Export facts as variables too (facts become @factname in ERB)
    if (env->facts_db && env->facts_db->current_node) {
        
        // Find the current node's facts
        puppet_value_t *node_lookup = puppet_hash_get(env->facts_db->node_index, 
                                                      env->facts_db->current_node, 
                                                      strlen(env->facts_db->current_node));
        if (node_lookup && node_lookup->type == PUPPET_VALUE_NUMBER) {
            size_t node_idx = (size_t)node_lookup->data.number;
            if (node_idx < env->facts_db->node_count) {
                puppet_node_facts_t *node_facts = &env->facts_db->nodes[node_idx];
                
                // Export all facts as @variables
                for (size_t i = 0; i < node_facts->facts->bucket_count; i++) {
                    puppet_hash_entry_t *entry = node_facts->facts->buckets[i];
                    while (entry) {
                        puppet_set_ruby_variable(entry->key.data, entry->value, ruby_ctx);
                        entry = entry->next;
                    }
                }
            }
        }
    }
}

char *puppet_erb_render(const char *template_content, puppet_env_t *env, puppet_ruby_context_t *ruby_ctx, const char *template_name) {
    // Skip ERB rendering if flag is set (for parallel mode)
    if (env && env->skip_erb) {
        return puppet_strdup("[ERB skipped in parallel mode]");
    }

    // Use Ruby ERB - this is now required
    if (!ruby_ctx || !ruby_ctx->initialized) {
        printf("Error: Ruby ERB context not initialized\n");
        return NULL;
    }

    // Acquire Ruby mutex for thread-safety (Ruby VM is not thread-safe)
    pthread_mutex_lock(&ruby_mutex);

    // Clear $puppet_vars hash before populating
    int state = 0;
    (void)rb_eval_string_protect("$puppet_vars = {}", &state);

    // Export Puppet variables to Ruby globals
    puppet_export_env_to_ruby(env, ruby_ctx);

    // Create scope object for scope.lookupvar() support in templates
    // Use a method to define local 'scope' variable in the ERB binding context
    (void)rb_eval_string_protect("$scope = PuppetScope.new($puppet_vars)", &state);
    if (state != 0) {
        printf("Warning: Failed to create scope object (state=%d)\n", state);
    }

    // Create ERB template string
    VALUE template_str = rb_str_new2(template_content);
    rb_gv_set("$template_content", template_str);

    // Process with ERB - define 'scope' as local variable in the binding
    // Use trim_mode: '-' to handle -%> (strip trailing newlines after tags)
    VALUE result = rb_eval_string_protect(
        "scope = $scope; erb = ERB.new($template_content, trim_mode: '-'); erb.result(binding)", &state);
    
    if (state == 0) {
        const char *rendered = StringValueCStr(result);
        char *result_str = puppet_strdup(rendered);
        pthread_mutex_unlock(&ruby_mutex);
        return result_str;
    } else {
        if (template_name) {
            printf("Error: ERB processing failed in %s (state=%d)\n", template_name, state);
        } else {
            printf("Error: ERB processing failed (state=%d)\n", state);
        }
        if (state == 6) {  // TAG_RAISE - exception occurred
            VALUE exception = rb_errinfo();
            if (!NIL_P(exception)) {
                VALUE message = rb_obj_as_string(exception);
                printf("Ruby exception: %s\n", StringValueCStr(message));
            }
        }
        pthread_mutex_unlock(&ruby_mutex);
        return NULL;
    }
}

char *puppet_erb_file(const char *template_path, puppet_env_t *env, puppet_ruby_context_t *ruby_ctx) {
    /* Check if path is a directory (happens with invalid template paths) */
    struct stat st;
    if (stat(template_path, &st) != 0) {
        printf("Error: Cannot stat template file: %s\n", template_path);
        return NULL;
    }
    if (S_ISDIR(st.st_mode)) {
        printf("Error: Template path is a directory, not a file: %s\n", template_path);
        return NULL;
    }

    FILE *file = fopen(template_path, "r");
    if (!file) {
        printf("Error: Cannot open template file: %s\n", template_path);
        return NULL;
    }

    // Read entire file
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    if (file_size < 0) {
        printf("Error: Cannot determine size of template file: %s\n", template_path);
        fclose(file);
        return NULL;
    }
    fseek(file, 0, SEEK_SET);

    char *content = puppet_malloc(file_size + 1);
    size_t bytes_read = fread(content, 1, file_size, file);
    (void)bytes_read; /* Suppress warning - we trust file_size from ftell */
    content[file_size] = '\0';
    fclose(file);

    char *result = puppet_erb_render(content, env, ruby_ctx, template_path);
    puppet_free(content);
    return result;
}

int puppet_ruby_available(void) {
    return 1;
}

const char *puppet_ruby_version(void) {
    return "Ruby 3.4.0";  // Static version string
}

// Forward declaration for loader
#include "puppet_loader.h"

/**
 * Resolve a Puppet template path to a filesystem path
 *
 * Puppet template paths are in format: module_name/template_file.erb
 * These resolve to: <modules_path>/module_name/templates/template_file.erb
 *
 * @param template_path The Puppet template path (e.g., "base_config/system_info.erb")
 * @param env The execution environment with loader
 * @return Resolved filesystem path (caller must free) or NULL on error
 */
static char *resolve_template_path(const char *template_path, puppet_env_t *env) {
    if (!template_path) return NULL;

    // First, check if the path is a direct file path that exists
    FILE *test = fopen(template_path, "r");
    if (test) {
        fclose(test);
        return puppet_strdup(template_path);
    }

    // Find the first '/' to split module_name and template_file
    const char *slash = strchr(template_path, '/');
    if (!slash) {
        printf("Error: Invalid template path '%s' - must be in format 'module/template.erb'\n",
               template_path);
        return NULL;
    }

    // Extract module name
    size_t module_len = slash - template_path;
    char *module_name = puppet_malloc(module_len + 1);
    strncpy(module_name, template_path, module_len);
    module_name[module_len] = '\0';

    // Template file is everything after the slash
    const char *template_file = slash + 1;

    // Get the modules path from loader
    const char *modules_path = NULL;
    if (env && env->loader && env->loader->modules_path) {
        modules_path = env->loader->modules_path;
    }

    // Build full path: modules_path/module_name/templates/template_file
    char *full_path = NULL;
    if (modules_path) {
        size_t path_len = strlen(modules_path) + 1 + module_len + strlen("/templates/") + strlen(template_file) + 1;
        full_path = puppet_malloc(path_len);
        snprintf(full_path, path_len, "%s/%s/templates/%s", modules_path, module_name, template_file);
    } else {
        // Fallback: try common paths
        const char *search_paths[] = {
            "/etc/puppet/modules",
            "/etc/puppet/code/modules",
            "/etc/puppetlabs/code/modules",
            "modules",
            NULL
        };

        for (int i = 0; search_paths[i]; i++) {
            size_t path_len = strlen(search_paths[i]) + 1 + module_len + strlen("/templates/") + strlen(template_file) + 1;
            full_path = puppet_malloc(path_len);
            snprintf(full_path, path_len, "%s/%s/templates/%s", search_paths[i], module_name, template_file);

            // Check if file exists
            FILE *test = fopen(full_path, "r");
            if (test) {
                fclose(test);
                puppet_free(module_name);
                return full_path;
            }
            puppet_free(full_path);
            full_path = NULL;
        }

        printf("Error: Could not find template '%s' in any modules path\n", template_path);
    }

    puppet_free(module_name);
    return full_path;
}

// Common template() function implementation
puppet_value_t *puppet_func_template(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        printf("Error: template() function requires at least 1 argument\n");
        return puppet_value_create_undef();
    }

    // Skip ERB in parallel mode - return placeholder
    if (env && env->skip_erb) {
        return puppet_value_create_string("[template skipped in parallel mode]",
                                          strlen("[template skipped in parallel mode]"));
    }

    // Initialize Ruby if needed (once for all templates)
    static puppet_ruby_context_t *ruby_ctx = NULL;
    if (!ruby_ctx) {
        ruby_ctx = puppet_ruby_init();
        if (!ruby_ctx) {
            return puppet_value_create_undef();
        }
    }

    // Concatenate all template outputs
    char *full_output = NULL;
    size_t full_len = 0;

    for (size_t i = 0; i < args->count; i++) {
        // Evaluate the template path argument
        puppet_value_t *path_value = puppet_eval_expr(args->exprs[i], env);
        if (path_value->type != PUPPET_VALUE_STRING) {
            printf("Error: template() function requires string arguments\n");
            puppet_value_destroy(path_value);
            if (full_output) puppet_free(full_output);
            return puppet_value_create_undef();
        }

        // Resolve the template path to a filesystem path
        char *resolved_path = resolve_template_path(path_value->data.string.data, env);
        puppet_value_destroy(path_value);

        if (!resolved_path) {
            if (full_output) puppet_free(full_output);
            return puppet_value_create_undef();
        }

        // Render the template
        char *rendered = puppet_erb_file(resolved_path, env, ruby_ctx);
        puppet_free(resolved_path);

        if (!rendered) {
            if (full_output) puppet_free(full_output);
            return puppet_value_create_undef();
        }

        // Append to full output
        size_t rendered_len = strlen(rendered);
        if (full_output) {
            full_output = puppet_realloc(full_output, full_len + rendered_len + 1);
            memcpy(full_output + full_len, rendered, rendered_len + 1);
            full_len += rendered_len;
        } else {
            full_output = rendered;
            full_len = rendered_len;
            rendered = NULL;  // Don't free - transferred ownership
        }
        if (rendered) puppet_free(rendered);
    }

    if (!full_output) {
        return puppet_value_create_undef();
    }

    puppet_value_t *result = puppet_value_create_string(full_output, full_len);
    puppet_free(full_output);
    return result;
}