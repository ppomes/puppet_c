#ifndef PUPPET_ERB_H
#define PUPPET_ERB_H

#include "puppet_ast.h"
#include "puppet_interpreter.h"

typedef struct puppet_ruby_context {
    int initialized;
    void *ruby_vm;  // Opaque pointer to Ruby VM
} puppet_ruby_context_t;

// Ruby context management
puppet_ruby_context_t *puppet_ruby_init(void);
void puppet_ruby_cleanup(puppet_ruby_context_t *ctx);

// Value conversion between Puppet and Ruby
void *puppet_value_to_ruby(puppet_value_t *value, puppet_ruby_context_t *ctx);
puppet_value_t *ruby_to_puppet_value(void *ruby_obj, puppet_ruby_context_t *ctx);

// ERB template processing
char *puppet_erb_render(const char *template_content, puppet_env_t *env, puppet_ruby_context_t *ruby_ctx, const char *template_name);
char *puppet_erb_file(const char *template_path, puppet_env_t *env, puppet_ruby_context_t *ruby_ctx);

// Simple template processing (fallback)
char *puppet_simple_template_render(const char *template, puppet_env_t *env);

// Template function for interpreter
puppet_value_t *puppet_func_template(puppet_expr_list_t *args, puppet_env_t *env);

// Utility functions
int puppet_ruby_available(void);
const char *puppet_ruby_version(void);

#endif