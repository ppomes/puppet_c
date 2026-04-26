/*
 * Native (Ruby-free) ERB renderer for the simple template subset.
 *
 * Inspired by the Haskell language-puppet/puppetresources approach: parse
 * and evaluate the most common Puppet ERB constructs in C, falling back to
 * the embedded Ruby ERB only for templates that use features outside the
 * supported subset (control flow, arithmetic, interpolated strings,
 * method calls, etc.).
 *
 * Supported:
 *   <%= @var %>                           instance var (= scope variable)
 *   <%= scope.lookupvar('cls::var') %>    qualified scope lookup
 *   <%= scope['name'] %>                  same, bracket form
 *   <%= 'literal' %>  <%= "literal" %>    plain string literals (no #{})
 *   <%= expr[idx] %>                      array/hash indexing, chainable
 *   <%# comment %>                        comment block (renders empty)
 *   <%- ... %>  <%= ... -%>               trim markers (Ruby-erb '-' mode)
 *
 * Anything else (<% if %>, arithmetic, "#{...}", method calls, etc.)
 * causes the renderer to return NULL so the caller falls back to Ruby.
 */
#ifndef PUPPET_ERB_NATIVE_H
#define PUPPET_ERB_NATIVE_H

#include "puppet_interpreter.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Render a template using the native engine.
 *
 *   content        - template source
 *   template_name  - cache key (typically the file path); NULL bypasses
 *                    the AST cache but still attempts a render
 *   env            - interpreter env used for variable resolution
 *
 * Returns a heap-allocated rendered string (caller frees with puppet_free)
 * on success, or NULL when the template uses unsupported constructs and
 * the caller should fall back to the Ruby engine.
 */
char *puppet_erb_native_render(const char *content,
                               const char *template_name,
                               puppet_env_t *env);

/* Drop the AST cache (test/debug helper). */
void puppet_erb_native_cache_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* PUPPET_ERB_NATIVE_H */
