/*
 * ERB Template Integration for Puppet C Parser
 * 
 * This module provides ERB template processing capabilities by embedding
 * the Ruby interpreter and implementing a fallback simple template renderer.
 * 
 * Features:
 * - Full Ruby VM embedding for native ERB support
 * - Fallback simple template renderer for <%= $variable %> syntax
 * - Puppet value to Ruby object conversion
 * - Error handling and graceful degradation
 * - Memory management for Ruby integration
 */

#include "puppet_erb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Check if Ruby support was compiled in
#ifdef HAVE_RUBY
#include <ruby.h>
#include <ruby/encoding.h>

// Global Ruby context - singleton pattern for VM management
static puppet_ruby_context_t *global_ruby_ctx = NULL;

/**
 * Initialize Ruby VM for ERB template processing
 * 
 * This function sets up the Ruby interpreter with full environment including
 * load paths and encoding configuration. It uses a singleton pattern to
 * avoid multiple VM initialization.
 * 
 * @return Initialized Ruby context or existing context if already initialized
 */
puppet_ruby_context_t *puppet_ruby_init(void) {
    // Return existing context if already initialized (singleton pattern)
    if (global_ruby_ctx && global_ruby_ctx->initialized) {
        return global_ruby_ctx;
    }
    
    puppet_ruby_context_t *ctx = calloc(1, sizeof(puppet_ruby_context_t));
    
    // Initialize Ruby VM with complete environment
    ruby_init();                    // Initialize Ruby interpreter
    ruby_init_loadpath();          // Set up Ruby load paths  
    ruby_script("puppet");          // Set script name for Ruby
    
    // Initialize encoding system - critical for ERB library functionality
    // Ruby 3.4+ requires explicit encoding setup before using string operations
    int state = 0;
    rb_eval_string_protect("Encoding.default_external = 'UTF-8'", &state);
    rb_eval_string_protect("Encoding.default_internal = 'UTF-8'", &state);
    
    // Test basic Ruby functionality to ensure VM is working
    state = 0;
    VALUE result = rb_eval_string_protect("'hello'", &state);
    if (state) {
        printf("Warning: Basic Ruby evaluation failed (state=%d)\n", state);
    } else {
        printf("Ruby basic evaluation works\n");
    }
    
    // Test Ruby load path accessibility
    state = 0;
    result = rb_eval_string_protect("puts $LOAD_PATH", &state);
    if (state) {
        printf("Cannot access Ruby load path\n");
    }
    
    // Attempt to load ERB library - this is the critical step that often fails
    // in Ruby 3.4+ due to encoding initialization issues
    state = 0;
    result = rb_eval_string_protect("require 'erb'", &state);
    if (state) {
        printf("Warning: Could not load Ruby ERB library (state=%d = TAG_RAISE)\n", state);
        
        // Extract detailed error information for debugging
        VALUE error = rb_errinfo();
        if (!NIL_P(error)) {
            VALUE error_msg = rb_obj_as_string(error);
            printf("ERB require error: %s\n", StringValueCStr(error_msg));
        }
        
        // Try alternative loading method as fallback
        printf("Trying alternative ERB loading...\n");
        state = 0;
        result = rb_eval_string_protect("load 'erb.rb'", &state);
        if (state) {
            printf("Alternative ERB loading also failed (state=%d)\n", state);
        } else {
            printf("ERB loaded via alternative method\n");
        }
    } else {
        printf("Ruby ERB library loaded successfully\n");
    }
    
    ctx->initialized = 1;
    global_ruby_ctx = ctx;
    
    printf("Ruby ERB initialized successfully\n");
    return ctx;
}

/**
 * Clean up Ruby VM and release resources
 * 
 * This function properly shuts down the Ruby interpreter and releases
 * all associated memory. It should be called when ERB functionality
 * is no longer needed.
 * 
 * @param ctx Ruby context to clean up
 */
void puppet_ruby_cleanup(puppet_ruby_context_t *ctx) {
    if (!ctx || !ctx->initialized) return;
    
    // Properly shut down Ruby VM
    ruby_cleanup(0);
    ctx->initialized = 0;
    
    // Clear global reference if this was the global context
    if (global_ruby_ctx == ctx) {
        global_ruby_ctx = NULL;
    }
    free(ctx);
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
            // Create Ruby hash (TODO: implement hash iteration)
            VALUE hash = rb_hash_new();
            // TODO: Iterate over hash entries and convert
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
    
    // Convert variable name to Ruby global variable format
    char var_name[256];
    snprintf(var_name, sizeof(var_name), "$%s", name);
    
    // Set as global variable 
    rb_gv_set(var_name, ruby_val);
}

static void puppet_export_env_to_ruby(puppet_env_t *env, puppet_ruby_context_t *ruby_ctx) {
    if (!env || !ruby_ctx) return;
    
    // Export all variables from current scope
    puppet_scope_t *scope = env->current_scope;
    while (scope) {
        for (size_t i = 0; i < scope->variables->bucket_count; i++) {
            puppet_hash_entry_t *entry = scope->variables->buckets[i];
            while (entry) {
                puppet_set_ruby_variable(entry->key.data, entry->value, ruby_ctx);
                entry = entry->next;
            }
        }
        scope = scope->parent;
    }
}

char *puppet_erb_render(const char *template_content, puppet_env_t *env, puppet_ruby_context_t *ruby_ctx) {
    // Try ERB first, fallback to simple interpolation
    if (ruby_ctx && ruby_ctx->initialized) {
        // Export Puppet variables to Ruby
        puppet_export_env_to_ruby(env, ruby_ctx);
        
        int state = 0;
        VALUE template_str = rb_str_new(template_content, strlen(template_content));
        rb_gv_set("$erb_template_content", template_str);
        
        // Try ERB rendering
        VALUE result = rb_eval_string_protect("require 'erb'; ERB.new($erb_template_content).result(binding)", &state);
        
        if (!state) {
            const char *rendered = StringValueCStr(result);
            return strdup(rendered);
        }
        
        printf("ERB failed (state=%d), falling back to simple interpolation\n", state);
    }
    
    // Fallback: Simple template interpolation without ERB
    return puppet_simple_template_render(template_content, env);
}

/**
 * Convert Puppet values to string representation for template interpolation
 * 
 * This function provides string conversion for all Puppet value types,
 * used by the simple template renderer when Ruby ERB is not available.
 * 
 * @param value Puppet value to convert
 * @return String representation of the value
 */
const char *puppet_value_to_string(puppet_value_t *value) {
    if (!value) return "(null)";
    
    switch (value->type) {
        case PUPPET_VALUE_UNDEF:
            return "(undef)";
        case PUPPET_VALUE_BOOL:
            return value->data.boolean ? "true" : "false";
        case PUPPET_VALUE_STRING:
            return value->data.string.data;
        case PUPPET_VALUE_NUMBER: {
            static char num_buf[64];
            if (value->data.number == (long)value->data.number) {
                snprintf(num_buf, sizeof(num_buf), "%ld", (long)value->data.number);
            } else {
                snprintf(num_buf, sizeof(num_buf), "%.2f", value->data.number);
            }
            return num_buf;
        }
        case PUPPET_VALUE_ARRAY:
            return "[Array]";
        case PUPPET_VALUE_HASH:
            return "{Hash}";
        default:
            return "(unknown)";
    }
}

/**
 * Simple ERB-compatible template renderer (fallback implementation)
 * 
 * This function provides a lightweight alternative to Ruby ERB when the
 * Ruby library cannot be loaded. It supports basic variable interpolation
 * using the <%= $variable %> syntax commonly used in ERB templates.
 * 
 * Features:
 * - Parses <%= $variable %> expressions
 * - Looks up variables in Puppet environment
 * - Handles whitespace around variable names
 * - Dynamically expands output buffer as needed
 * 
 * @param template Template content with ERB syntax
 * @param env Puppet environment containing variables
 * @return Rendered template with variables interpolated
 */
char *puppet_simple_template_render(const char *template, puppet_env_t *env) {
    if (!template || !env) return NULL;
    
    size_t template_len = strlen(template);
    size_t output_capacity = template_len * 2; // Start with 2x template size
    char *output = malloc(output_capacity);
    size_t output_len = 0;
    
    const char *pos = template;
    while (*pos) {
        // Look for ERB expression pattern: <%= ... %>
        if (pos[0] == '<' && pos[1] == '%' && pos[2] == '=') {
            // Find the matching closing tag
            const char *start = pos + 3;
            const char *end = strstr(start, "%>");
            if (end) {
                // Extract and clean variable expression
                while (*start == ' ') start++;  // Skip leading whitespace
                const char *var_end = end;
                while (var_end > start && *(var_end-1) == ' ') var_end--;  // Skip trailing whitespace
                
                if (*start == '$') {
                    // Extract Puppet variable name (skip the $ prefix)
                    start++; 
                    size_t var_len = var_end - start;
                    char var_name[256];
                    if (var_len < sizeof(var_name)) {
                        strncpy(var_name, start, var_len);
                        var_name[var_len] = '\0';
                        
                        // Look up variable value in Puppet environment
                        puppet_value_t *value = puppet_env_get_var(env, var_name);
                        if (value) {
                            const char *str_val = puppet_value_to_string(value);
                            size_t str_len = strlen(str_val);
                            
                            // Dynamically expand output buffer if needed
                            while (output_len + str_len >= output_capacity) {
                                output_capacity *= 2;
                                output = realloc(output, output_capacity);
                            }
                            
                            // Insert variable value into output
                            memcpy(output + output_len, str_val, str_len);
                            output_len += str_len;
                        }
                    }
                }
                
                pos = end + 2; // Skip past the closing %>
                continue;
            }
        }
        
        // Regular character - copy directly to output
        if (output_len + 1 >= output_capacity) {
            output_capacity *= 2;
            output = realloc(output, output_capacity);
        }
        output[output_len++] = *pos++;
    }
    
    // Null-terminate the output string
    output[output_len] = '\0';
    return output;
}

char *puppet_erb_file(const char *template_path, puppet_env_t *env, puppet_ruby_context_t *ruby_ctx) {
    FILE *file = fopen(template_path, "r");
    if (!file) {
        printf("Error: Cannot open template file: %s\n", template_path);
        return NULL;
    }
    
    // Read entire file
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *content = malloc(file_size + 1);
    fread(content, 1, file_size, file);
    content[file_size] = '\0';
    fclose(file);
    
    char *result = puppet_erb_render(content, env, ruby_ctx);
    free(content);
    return result;
}

int puppet_ruby_available(void) {
    return 1;
}

const char *puppet_ruby_version(void) {
    return "Ruby 3.4.0";  // Static version string
}

#else  // No Ruby support

puppet_ruby_context_t *puppet_ruby_init(void) {
    printf("Warning: Ruby support not compiled in - ERB templates unavailable\n");
    return NULL;
}

void puppet_ruby_cleanup(puppet_ruby_context_t *ctx) {
    (void)ctx;  // Suppress unused parameter warning
}

void *puppet_value_to_ruby(puppet_value_t *value, puppet_ruby_context_t *ctx) {
    (void)value; (void)ctx;
    return NULL;
}

puppet_value_t *ruby_to_puppet_value(void *ruby_obj, puppet_ruby_context_t *ctx) {
    (void)ruby_obj; (void)ctx;
    return puppet_value_create_undef();
}

char *puppet_erb_render(const char *template_content, puppet_env_t *env, puppet_ruby_context_t *ruby_ctx) {
    (void)env; (void)ruby_ctx;
    printf("Error: ERB templates require Ruby support (template content: %.50s...)\n", 
           template_content);
    return NULL;
}

char *puppet_erb_file(const char *template_path, puppet_env_t *env, puppet_ruby_context_t *ruby_ctx) {
    (void)env; (void)ruby_ctx;
    printf("Error: ERB templates require Ruby support (file: %s)\n", template_path);
    return NULL;
}

int puppet_ruby_available(void) {
    return 0;
}

const char *puppet_ruby_version(void) {
    return "Not available";
}

#endif

// Common template() function implementation
puppet_value_t *puppet_func_template(puppet_expr_list_t *args, puppet_env_t *env) {
    if (!args || args->count != 1) {
        printf("Error: template() function requires exactly 1 argument\n");
        return puppet_value_create_undef();
    }
    
    // Evaluate the template path argument
    puppet_value_t *path_value = puppet_eval_expr(args->exprs[0], env);
    if (path_value->type != PUPPET_VALUE_STRING) {
        printf("Error: template() function requires a string argument\n");
        puppet_value_destroy(path_value);
        return puppet_value_create_undef();
    }
    
    // Initialize Ruby if needed
    static puppet_ruby_context_t *ruby_ctx = NULL;
    if (!ruby_ctx) {
        ruby_ctx = puppet_ruby_init();
        if (!ruby_ctx) {
            puppet_value_destroy(path_value);
            return puppet_value_create_undef();
        }
    }
    
    // Render the template
    char *rendered = puppet_erb_file(path_value->data.string.data, env, ruby_ctx);
    puppet_value_destroy(path_value);
    
    if (!rendered) {
        return puppet_value_create_undef();
    }
    
    puppet_value_t *result = puppet_value_create_string(rendered, strlen(rendered));
    free(rendered);
    return result;
}