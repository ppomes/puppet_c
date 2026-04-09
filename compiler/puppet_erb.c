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
#include "puppet_stdlib.h"
#include "puppet_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <pthread.h>

#include <ruby.h>
#include <ruby/encoding.h>

// Global Ruby context for VM management
static puppet_ruby_context_t *global_ruby_ctx = NULL;

// Global mutex for Ruby VM access (Ruby is not thread-safe)
static pthread_mutex_t ruby_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Trim whitespace up to and including ONE newline
 *
 * EPP trim marker (-%>) should only remove trailing whitespace
 * up to and including a single newline, not all whitespace.
 */
static const char *trim_one_newline(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (p < end && (*p == '\n' || *p == '\r')) {
        if (*p == '\r' && p + 1 < end && *(p + 1) == '\n') p += 2;
        else p++;
    }
    return p;
}

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
        "  # Support for nested template inclusion: scope.function_template(['module/template.erb'])\n"
        "  def function_template(args)\n"
        "    return '' unless args.is_a?(Array) && !args.empty?\n"
        "    result = ''\n"
        "    args.each do |template_path|\n"
        "      # Resolve template path: module/file.erb -> modules_path/module/templates/file.erb\n"
        "      full_path = nil\n"
        "      if File.exist?(template_path)\n"
        "        full_path = template_path\n"
        "      elsif $puppet_modules_path && template_path.include?('/')\n"
        "        parts = template_path.split('/', 2)\n"
        "        module_name = parts[0]\n"
        "        template_file = parts[1]\n"
        "        candidate = File.join($puppet_modules_path, module_name, 'templates', template_file)\n"
        "        full_path = candidate if File.exist?(candidate)\n"
        "      end\n"
        "      if full_path && File.exist?(full_path)\n"
        "        content = File.read(full_path)\n"
        "        # Render with current binding (same scope/variables)\n"
        "        erb = ERB.new(content, trim_mode: '-')\n"
        "        # Create a binding with 'scope' available\n"
        "        scope = self\n"
        "        result += erb.result(binding)\n"
        "      else\n"
        "        $stderr.puts \"Warning: Template not found: #{template_path}\"\n"
        "      end\n"
        "    end\n"
        "    result\n"
        "  end\n"
        "end\n"
        "$puppet_vars = {}\n"
        "$puppet_modules_path = nil\n"
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

    // Replace characters invalid in Ruby identifiers with underscores
    // Ruby identifiers only allow [a-zA-Z0-9_] (and :: for scope)
    for (char *p = clean_name; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 128 || (!isalnum(c) && c != '_' && c != ':')) {
            *p = '_';
        }
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

    // Set modules path for nested template resolution (scope.function_template)
    if (env && env->loader && env->loader->modules_path) {
        VALUE modules_path = rb_str_new2(env->loader->modules_path);
        rb_gv_set("$puppet_modules_path", modules_path);
    } else {
        rb_gv_set("$puppet_modules_path", Qnil);
    }

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

/**
 * Resolve EPP template path (same logic as ERB but for .epp files)
 */
static char *resolve_epp_template_path(const char *template_name, puppet_env_t *env) {
    // Format: module_name/template_file.epp
    // Look in: <modulepath>/module_name/templates/template_file.epp

    if (!template_name) return NULL;

    // First, check if the path is a direct file path that exists
    FILE *test = fopen(template_name, "r");
    if (test) {
        fclose(test);
        return puppet_strdup(template_name);
    }

    // Find the separator
    const char *slash = strchr(template_name, '/');
    if (!slash) {
        fprintf(stderr, "[ERROR] Invalid EPP template path: %s (expected module/template format)\n", template_name);
        return NULL;
    }

    // Extract module name and template file
    size_t module_len = slash - template_name;
    char *module_name = puppet_malloc(module_len + 1);
    strncpy(module_name, template_name, module_len);
    module_name[module_len] = '\0';

    const char *template_file = slash + 1;

    // Get the modules path from loader (same as ERB)
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

        struct stat st;
        if (stat(full_path, &st) == 0) {
            puppet_free(module_name);
            return full_path;
        }
        puppet_free(full_path);
    }

    fprintf(stderr, "[ERROR] EPP template not found: %s\n", template_name);
    puppet_free(module_name);
    return NULL;
}

/**
 * Read file contents into a string
 */
static char *read_file_contents(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    char *content = puppet_malloc(size + 1);
    size_t read = fread(content, 1, size, f);
    content[read] = '\0';
    fclose(f);

    return content;
}

/**
 * Simple EPP renderer - handles variable substitution
 *
 * EPP templates use:
 * - <%- | params | -%>  - parameter header (at start)
 * - <%= $var %>         - output variable value
 * - <% code %>          - execute code (no output)
 * - <%# comment %>      - comment
 * - <%- / -%>           - trim whitespace variants
 */
/* Forward declarations for EPP tree-sitter support */
#include "puppet_ts_parser.h"
#include "puppet_interpreter.h"

/* Helper: append string to EPP output buffer */
static void epp_append(char **output, size_t *out_len, size_t *out_capacity,
                       const char *str, size_t len) {
    while (*out_len + len + 1 > *out_capacity) {
        *out_capacity *= 2;
        *output = puppet_realloc(*output, *out_capacity);
    }
    memcpy(*output + *out_len, str, len);
    *out_len += len;
    (*output)[*out_len] = '\0';
}

/* Helper: trim trailing whitespace from output */
static void epp_trim_trailing(char *output, size_t *out_len) {
    while (*out_len > 0 && (output[*out_len - 1] == ' ' || output[*out_len - 1] == '\t')) {
        (*out_len)--;
    }
    output[*out_len] = '\0';
}

/* Forward declaration for recursive EPP rendering */
static char *puppet_epp_render_range(const char *start, const char *end,
                                     puppet_hash_t *params, puppet_env_t *env);

/**
 * Render EPP template with full tree-sitter support
 *
 * Handles:
 * - <%= expr %> : Output expression result
 * - <% code %>  : Execute Puppet code (if, each, etc.)
 * - <%# comment %> : Comments (ignored)
 * - <%- / -%> : Whitespace trimming variants
 */
static char *puppet_epp_render(const char *content, puppet_hash_t *params, puppet_env_t *env) {
    if (!content) return NULL;

    /* Params are already set up in env scope by puppet_func_epp */
    return puppet_epp_render_range(content, content + strlen(content), params, env);
}

static char *puppet_epp_render_range(const char *start, const char *end,
                                     puppet_hash_t *params, puppet_env_t *env) {
    size_t out_capacity = (end - start) * 2 + 1;
    size_t out_len = 0;
    char *output = puppet_malloc(out_capacity);
    output[0] = '\0';

    const char *p = start;

    /* Skip parameter header if present: <%- | ... | -%> or <% | ... | %>
     * The | must appear at the start (after optional whitespace), not after code */
    if (p < end && (strncmp(p, "<%-", 3) == 0 || strncmp(p, "<%", 2) == 0)) {
        const char *s = p + (p[2] == '-' ? 3 : 2);
        /* Skip whitespace */
        while (s < end && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) s++;
        /* Parameter header starts with | */
        if (s < end && *s == '|') {
            const char *pipe1 = s;
            const char *pipe2 = NULL;
            for (s = pipe1 + 1; s < end && *s != '%'; s++) {
                if (*s == '|') { pipe2 = s; break; }
            }
            if (pipe2) {
                const char *tag_end = strstr(pipe2, "-%>");
                if (tag_end && tag_end < end) {
                    p = tag_end + 3;
                    while (p < end && (*p == '\n' || *p == '\r')) p++;
                } else {
                    tag_end = strstr(pipe2, "%>");
                    if (tag_end && tag_end < end) {
                        p = tag_end + 2;
                        while (p < end && (*p == '\n' || *p == '\r')) p++;
                    }
                }
            }
        }
    }

    while (p < end) {
        /* Check for EPP tags */
        if (p + 1 < end && p[0] == '<' && p[1] == '%') {
            int trim_before = (p + 2 < end && p[2] == '-');
            const char *tag_start = p + (trim_before ? 3 : 2);

            if (trim_before) {
                epp_trim_trailing(output, &out_len);
            }

            while (tag_start < end && (*tag_start == ' ' || *tag_start == '\t')) tag_start++;

            /* Find closing tag */
            const char *tag_end = strstr(tag_start, "%>");
            if (!tag_end || tag_end >= end) {
                /* Malformed - copy as-is */
                epp_append(&output, &out_len, &out_capacity, p, 1);
                p++;
                continue;
            }

            int trim_after = (tag_end > tag_start && *(tag_end - 1) == '-');
            const char *code_end = trim_after ? tag_end - 1 : tag_end;

            /* Comment: <%# ... %> */
            if (*tag_start == '#') {
                p = tag_end + 2;
                if (trim_after) {
                    p = trim_one_newline(p, end);
                }
                continue;
            }

            /* Output expression: <%= ... %> */
            if (*tag_start == '=') {
                tag_start++;
                while (tag_start < code_end && (*tag_start == ' ' || *tag_start == '\t')) tag_start++;

                /* Trim trailing whitespace from expression */
                while (code_end > tag_start && (*(code_end-1) == ' ' || *(code_end-1) == '\t')) code_end--;

                size_t expr_len = code_end - tag_start;
                if (expr_len > 0) {
                    /* Parse and evaluate expression using tree-sitter */
                    puppet_expr_t *expr = puppet_ts_parse_expression(tag_start, expr_len);
                    if (expr && env) {
                        puppet_value_t *value = puppet_eval_expr(expr, env);
                        if (value) {
                            char *value_str = puppet_value_to_display_string(value);
                            if (value_str) {
                                epp_append(&output, &out_len, &out_capacity, value_str, strlen(value_str));
                                puppet_free(value_str);
                            }
                            puppet_value_destroy(value);
                        }
                        puppet_expr_destroy(expr);
                    } else if (expr) {
                        puppet_expr_destroy(expr);
                    }
                }

                p = tag_end + 2;
                if (trim_after) {
                    p = trim_one_newline(p, end);
                }
                continue;
            }

            /* Code block: <% ... %> */
            /* Trim trailing whitespace from code */
            while (code_end > tag_start && (*(code_end-1) == ' ' || *(code_end-1) == '\t')) code_end--;

            size_t code_len = code_end - tag_start;
            if (code_len > 0) {
                char *code = puppet_malloc(code_len + 1);
                memcpy(code, tag_start, code_len);
                code[code_len] = '\0';

                /* Check for 'each' or 'map' iterator: EXPR.each |VAR| { or EXPR.map |VAR| { */
                char *each_pos = strstr(code, ".each");
                char *map_pos = strstr(code, ".map");
                char *each_func = strstr(code, "each(");
                if (each_pos || map_pos || (each_func && each_func == code)) {
                    /* Parse the full expression as a statement to get AST with lambda */
                    /* The code ends with '{', wrap as: $_epp_iter = (code without {) { } */
                    size_t clean_len = code_len;
                    /* Strip trailing { and whitespace */
                    while (clean_len > 0 && (code[clean_len-1] == '{' || code[clean_len-1] == ' ' || code[clean_len-1] == '\t')) {
                        clean_len--;
                    }
                    size_t wrap_len = clean_len + 50;
                    char *wrap_code = puppet_malloc(wrap_len);
                    snprintf(wrap_code, wrap_len, "$_epp_iter = %.*s { }", (int)clean_len, code);

                    puppet_stmt_list_t *stmts = puppet_ts_parse_string(wrap_code, strlen(wrap_code));
                    puppet_free(wrap_code);

                    puppet_expr_t *iter_expr = NULL;
                    puppet_lambda_t *lambda = NULL;

                    /* Extract the funcall expression with lambda */
                    if (stmts && stmts->count > 0) {
                        puppet_stmt_t *stmt = stmts->stmts[0];
                        if (stmt && stmt->type == PUPPET_STMT_ASSIGNMENT && stmt->data.assignment.value) {
                            puppet_expr_t *expr = stmt->data.assignment.value;
                            if (expr->type == PUPPET_EXPR_FUNCALL && expr->data.funcall.lambda) {
                                iter_expr = expr;
                                lambda = expr->data.funcall.lambda;
                            }
                        }
                    }

                    /* Find matching closing brace - it's in a later tag: <% } %> */
                    const char *body_start = tag_end + 2;
                    if (trim_after) {
                        body_start = trim_one_newline(body_start, end);
                    }

                    /* Find <% } %> that closes this each */
                    int brace_depth = 1;
                    const char *body_end = body_start;
                    while (body_end < end && brace_depth > 0) {
                        if (body_end + 1 < end && body_end[0] == '<' && body_end[1] == '%') {
                            const char *inner_start = body_end + 2;
                            if (*inner_start == '-') inner_start++;
                            while (inner_start < end && (*inner_start == ' ' || *inner_start == '\t')) inner_start++;

                            /* Find the closing %> to know where the tag ends */
                            const char *tag_close = strstr(inner_start, "%>");
                            if (!tag_close) tag_close = end;

                            /* Count closing braces */
                            if (*inner_start == '}') {
                                brace_depth--;
                            }

                            /* Check if tag ends with { (iterator/block open)
                             * Only count as opening brace if { is at the END of the code,
                             * not a { inside a lambda expression in the middle
                             * This handles both standalone { and } elsif/else { patterns */
                            const char *last_brace = NULL;
                            for (const char *p = inner_start; p < tag_close; p++) {
                                if (*p == '{') last_brace = p;
                            }
                            if (last_brace) {
                                /* Check if there's only whitespace/trim markers between { and %> */
                                const char *after_brace = last_brace + 1;
                                while (after_brace < tag_close && (*after_brace == ' ' || *after_brace == '\t' || *after_brace == '-')) {
                                    after_brace++;
                                }
                                if (after_brace >= tag_close || *after_brace == '%') {
                                    /* The { is at the end of the tag - this is a block/iterator open */
                                    brace_depth++;
                                }
                                /* Otherwise, { is in the middle (lambda body) - don't count */
                            }

                            /* Exit if we've closed all braces */
                            if (brace_depth == 0) break;
                        }
                        body_end++;
                    }

                    /* Get lambda parameter names */
                    const char *param1_name = NULL;
                    const char *param2_name = NULL;
                    if (lambda && lambda->params.count >= 1 && lambda->params.params[0].name.data) {
                        param1_name = lambda->params.params[0].name.data;
                    }
                    if (lambda && lambda->params.count >= 2 && lambda->params.params[1].name.data) {
                        param2_name = lambda->params.params[1].name.data;
                    }

                    /* Evaluate the collection (first argument to each) */
                    puppet_value_t *collection = NULL;
                    if (iter_expr && iter_expr->data.funcall.args.count >= 1 && env) {
                        collection = puppet_eval_expr(iter_expr->data.funcall.args.exprs[0], env);
                    }

                    /* Iterate based on collection type */
                    if (collection && param1_name) {
                        if (collection->type == PUPPET_VALUE_ARRAY) {
                            puppet_array_t *arr = collection->data.array;
                            for (size_t i = 0; i < arr->count; i++) {
                                if (lambda->params.count == 1) {
                                    /* |$item| - just the value */
                                    puppet_env_set_var(env, param1_name, puppet_value_copy(arr->items[i]));
                                } else if (param2_name) {
                                    /* |$index, $item| - index and value */
                                    puppet_env_set_var(env, param1_name, puppet_value_create_number((double)i));
                                    puppet_env_set_var(env, param2_name, puppet_value_copy(arr->items[i]));
                                }

                                /* Render body */
                                char *body_output = puppet_epp_render_range(body_start, body_end, params, env);
                                if (body_output) {
                                    epp_append(&output, &out_len, &out_capacity, body_output, strlen(body_output));
                                    puppet_free(body_output);
                                }
                            }
                        } else if (collection->type == PUPPET_VALUE_HASH) {
                            puppet_hash_t *hash = collection->data.hash;
                            for (size_t i = 0; i < hash->bucket_count; i++) {
                                puppet_hash_entry_t *entry = hash->buckets[i];
                                while (entry) {
                                    if (lambda->params.count == 1) {
                                        /* |$item| - [key, value] array */
                                        puppet_value_t *pair = puppet_value_create_array();
                                        puppet_array_append(pair->data.array, puppet_value_create_string(entry->key.data, entry->key.len));
                                        puppet_array_append(pair->data.array, puppet_value_copy(entry->value));
                                        puppet_env_set_var(env, param1_name, pair);
                                    } else if (param2_name) {
                                        /* |$key, $value| - key and value separately */
                                        puppet_env_set_var(env, param1_name, puppet_value_create_string(entry->key.data, entry->key.len));
                                        puppet_env_set_var(env, param2_name, puppet_value_copy(entry->value));
                                    }

                                    /* Render body */
                                    char *body_output = puppet_epp_render_range(body_start, body_end, params, env);
                                    if (body_output) {
                                        epp_append(&output, &out_len, &out_capacity, body_output, strlen(body_output));
                                        puppet_free(body_output);
                                    }

                                    entry = entry->next;
                                }
                            }
                        }
                    }

                    if (collection) puppet_value_destroy(collection);
                    if (stmts) {
                        for (size_t i = 0; i < stmts->count; i++) {
                            puppet_stmt_destroy(stmts->stmts[i]);
                        }
                        puppet_free(stmts->stmts);
                        puppet_free(stmts);
                    }

                    /* Skip past closing <% } %> */
                    const char *close_tag = strstr(body_end, "%>");
                    p = close_tag ? close_tag + 2 : end;
                    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
                }
                /* Check for 'if' conditional: if EXPR { with elsif/else support */
                else if (strncmp(code, "if ", 3) == 0 || strncmp(code, "if(", 3) == 0) {
                    /* Parse entire if/elsif/else chain
                     * Structure: if COND { BODY } elsif COND { BODY } else { BODY }
                     * Each branch: condition (NULL for else), body_start, body_end
                     */
                    #define MAX_IF_BRANCHES 32
                    struct {
                        const char *cond_start;
                        size_t cond_len;
                        const char *body_start;
                        const char *body_end;
                        int is_else;  /* 1 for else branch (no condition) */
                    } branches[MAX_IF_BRANCHES];
                    memset(branches, 0, sizeof(branches));
                    int branch_count = 0;

                    /* First branch: the 'if' itself */
                    const char *cond_start = code + 2;
                    while (*cond_start == ' ') cond_start++;
                    const char *cond_end = strstr(cond_start, "{");
                    if (!cond_end) cond_end = code + code_len;
                    while (cond_end > cond_start && (*(cond_end-1) == ' ' || *(cond_end-1) == '{')) cond_end--;

                    branches[0].cond_start = cond_start;
                    branches[0].cond_len = cond_end - cond_start;
                    branches[0].is_else = 0;
                    branches[0].body_start = tag_end + 2;
                    if (trim_after) {
                        branches[0].body_start = trim_one_newline(branches[0].body_start, end);
                    }
                    branch_count = 1;

                    /* Scan for elsif/else branches and final closing brace */
                    int brace_depth = 1;
                    const char *scan = branches[0].body_start;
                    const char *chain_end = NULL;

                    while (scan < end && brace_depth > 0) {
                        if (scan + 1 < end && scan[0] == '<' && scan[1] == '%') {
                            const char *inner_start = scan + 2;
                            if (*inner_start == '-') inner_start++;
                            while (inner_start < end && (*inner_start == ' ' || *inner_start == '\t')) inner_start++;

                            /* Find the closing %> */
                            const char *tag_close = strstr(inner_start, "%>");
                            if (!tag_close) tag_close = end;

                            /* Count closing braces */
                            if (*inner_start == '}') {
                                /* Check what follows the closing brace */
                                const char *after_brace = inner_start + 1;
                                while (after_brace < tag_close && (*after_brace == ' ' || *after_brace == '\t')) after_brace++;

                                if (brace_depth == 1) {
                                    /* At depth 1, check for elsif/else */
                                    if (strncmp(after_brace, "elsif ", 6) == 0 || strncmp(after_brace, "elsif(", 6) == 0) {
                                        /* End current branch body here */
                                        branches[branch_count - 1].body_end = scan;

                                        /* Start new elsif branch */
                                        if (branch_count < MAX_IF_BRANCHES) {
                                            const char *elsif_cond = after_brace + 5;
                                            while (*elsif_cond == ' ') elsif_cond++;
                                            const char *elsif_cond_end = strstr(elsif_cond, "{");
                                            if (!elsif_cond_end) elsif_cond_end = tag_close;
                                            while (elsif_cond_end > elsif_cond &&
                                                   (*(elsif_cond_end-1) == ' ' || *(elsif_cond_end-1) == '{'))
                                                elsif_cond_end--;

                                            branches[branch_count].cond_start = elsif_cond;
                                            branches[branch_count].cond_len = elsif_cond_end - elsif_cond;
                                            branches[branch_count].is_else = 0;
                                            branches[branch_count].body_start = tag_close + 2;
                                            /* Check for trim marker before %> */
                                            if (tag_close > inner_start && *(tag_close - 1) == '-') {
                                                branches[branch_count].body_start = trim_one_newline(branches[branch_count].body_start, end);
                                            }
                                            branch_count++;
                                        }
                                        /* brace_depth stays at 1 (} followed by {) */
                                        scan = tag_close + 2;
                                        continue;
                                    } else if (strncmp(after_brace, "else", 4) == 0 &&
                                               (after_brace[4] == ' ' || after_brace[4] == '{')) {
                                        /* End current branch body here */
                                        branches[branch_count - 1].body_end = scan;

                                        /* Start else branch */
                                        if (branch_count < MAX_IF_BRANCHES) {
                                            branches[branch_count].cond_start = NULL;
                                            branches[branch_count].cond_len = 0;
                                            branches[branch_count].is_else = 1;
                                            branches[branch_count].body_start = tag_close + 2;
                                            /* Check for trim marker before %> */
                                            if (tag_close > inner_start && *(tag_close - 1) == '-') {
                                                branches[branch_count].body_start = trim_one_newline(branches[branch_count].body_start, end);
                                            }
                                            branch_count++;
                                        }
                                        scan = tag_close + 2;
                                        continue;
                                    } else {
                                        /* Plain closing brace - end of if chain */
                                        branches[branch_count - 1].body_end = scan;
                                        chain_end = tag_close + 2;
                                        /* Check if final brace has trim marker */
                                        if (tag_close > inner_start && *(tag_close - 1) == '-') {
                                            chain_end = trim_one_newline(chain_end, end);
                                        }
                                        brace_depth = 0;
                                        break;
                                    }
                                } else {
                                    /* Nested brace at depth > 1 - decrement */
                                    brace_depth--;
                                }
                            }

                            /* Check if tag ends with { (block open)
                             * This handles both standalone { and } elsif/else { patterns at depth > 1 */
                            const char *last_brace = NULL;
                            for (const char *bp = inner_start; bp < tag_close; bp++) {
                                if (*bp == '{') last_brace = bp;
                            }
                            if (last_brace) {
                                const char *after_last = last_brace + 1;
                                while (after_last < tag_close &&
                                       (*after_last == ' ' || *after_last == '\t' || *after_last == '-'))
                                    after_last++;
                                if (after_last >= tag_close || *after_last == '%') {
                                    brace_depth++;
                                }
                            }

                            /* Exit if we've closed all braces */
                            if (brace_depth == 0) break;
                        }
                        scan++;
                    }

                    /* Evaluate branches in order, execute first matching */
                    int executed = 0;
                    for (int i = 0; i < branch_count && !executed; i++) {
                        int should_execute = 0;

                        if (branches[i].is_else) {
                            should_execute = 1;  /* else always executes if reached */
                        } else if (branches[i].cond_start && branches[i].cond_len > 0) {
                            puppet_expr_t *cond_expr = puppet_ts_parse_expression(
                                branches[i].cond_start, branches[i].cond_len);
                            if (cond_expr && env) {
                                puppet_value_t *cond_val = puppet_eval_expr(cond_expr, env);
                                if (cond_val) {
                                    should_execute = puppet_value_is_truthy(cond_val);
                                    puppet_value_destroy(cond_val);
                                }
                                puppet_expr_destroy(cond_expr);
                            }
                        }

                        if (should_execute) {
                            char *body_output = puppet_epp_render_range(
                                branches[i].body_start, branches[i].body_end, params, env);
                            if (body_output) {
                                epp_append(&output, &out_len, &out_capacity, body_output, strlen(body_output));
                                puppet_free(body_output);
                            }
                            executed = 1;
                        }
                    }

                    /* Skip past the entire if/elsif/else chain */
                    p = chain_end ? chain_end : end;
                    /* Don't trim unconditionally - let the loop body preserve newlines */
                }
                /* Plain closing brace - skip */
                else if (code[0] == '}') {
                    p = tag_end + 2;
                    if (trim_after) {
                        p = trim_one_newline(p, end);
                    }
                }
                /* Other code - try to execute as statements */
                else {
                    puppet_stmt_list_t *stmts = puppet_ts_parse_string(code, code_len);
                    if (stmts && env) {
                        puppet_exec_stmt_list(stmts, env);
                        for (size_t i = 0; i < stmts->count; i++) {
                            puppet_stmt_destroy(stmts->stmts[i]);
                        }
                        puppet_free(stmts->stmts);
                        puppet_free(stmts);
                    }
                    p = tag_end + 2;
                    if (trim_after) {
                        p = trim_one_newline(p, end);
                    }
                }

                puppet_free(code);
                continue;
            }

            p = tag_end + 2;
            if (trim_after) {
                p = trim_one_newline(p, end);
            }
            continue;
        }

        /* Copy regular character */
        epp_append(&output, &out_len, &out_capacity, p, 1);
        p++;
    }

    return output;
}

/**
 * EPP function implementation
 * Usage:
 *   epp('module/template.epp')
 *   epp('module/template.epp', { 'key' => 'value', ... })
 */
puppet_value_t *puppet_func_epp(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count < 1) {
        fprintf(stderr, "[ERROR] epp() requires at least 1 argument\n");
        return puppet_value_create_undef();
    }

    // Skip EPP in parallel mode - return placeholder
    if (env && env->skip_erb) {
        return puppet_value_create_string("[epp template skipped in parallel mode]",
                                          strlen("[epp template skipped in parallel mode]"));
    }

    // Get template path
    puppet_value_t *path_value = puppet_eval_expr(args->exprs[0], env);
    if (!path_value || path_value->type != PUPPET_VALUE_STRING) {
        fprintf(stderr, "[ERROR] epp() first argument must be a string\n");
        if (path_value) puppet_value_destroy(path_value);
        return puppet_value_create_undef();
    }

    // Get parameters hash (optional)
    puppet_hash_t *params = NULL;
    puppet_value_t *params_value = NULL;
    if (args->count >= 2) {
        params_value = puppet_eval_expr(args->exprs[1], env);
        if (params_value && params_value->type == PUPPET_VALUE_HASH) {
            params = params_value->data.hash;
        } else if (params_value) {
            puppet_value_destroy(params_value);
            params_value = NULL;
        }
    }

    // Resolve template path
    char *template_path = resolve_epp_template_path(path_value->data.string.data, env);
    puppet_value_destroy(path_value);

    if (!template_path) {
        if (params_value) puppet_value_destroy(params_value);
        return puppet_value_create_undef();
    }

    // Read template content
    char *content = read_file_contents(template_path);
    puppet_free(template_path);

    if (!content) {
        fprintf(stderr, "[ERROR] Failed to read EPP template\n");
        if (params_value) puppet_value_destroy(params_value);
        return puppet_value_create_undef();
    }

    // Set params in environment scope for EPP access (copy values since params_value owns them)
    if (params && env) {
        for (size_t i = 0; i < params->bucket_count; i++) {
            puppet_hash_entry_t *e = params->buckets[i];
            while (e) {
                puppet_env_set_var(env, e->key.data, puppet_value_copy(e->value));
                e = e->next;
            }
        }
    }

    // Render the template
    char *rendered = puppet_epp_render(content, params, env);
    puppet_free(content);

    // Clean up params_value now that we've copied what we need
    if (params_value) {
        puppet_value_destroy(params_value);
    }

    if (!rendered) {
        return puppet_value_create_undef();
    }

    puppet_value_t *result = puppet_value_create_string(rendered, strlen(rendered));
    puppet_free(rendered);

    return result;
}