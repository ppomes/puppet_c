/*
 * Native ERB renderer — see puppet_erb_native.h for the supported subset.
 *
 * Strategy borrowed from Haskell language-puppet (Puppet/Runner/Erb,
 * Erb/Parser, Erb/Evaluate): cache a parsed AST per template path, render
 * by walking the AST, fall back to Ruby for unsupported constructs.
 *
 * The parser is hand-written (no lex/yacc) and deliberately strict: any
 * input it cannot recognise causes the public entry point to return NULL
 * so the caller invokes the Ruby fallback. Better to be fast on 70% and
 * correct on 100% than to half-render an unsupported template.
 */

#include "puppet_erb_native.h"
#include "puppet_memory.h"
#include "puppet_stdlib.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- AST types ---------------- */

typedef enum {
    EXPR_LITERAL_STR,   /* 'foo' or "foo" (no #{} interpolation) */
    EXPR_VAR,           /* @name or bare scope variable */
    EXPR_SCOPE_LOOKUP,  /* scope.lookupvar('x') / scope['x'] — name is value */
    EXPR_INDEX          /* expr [ idx_expr ] */
} expr_kind_t;

typedef struct expr_node {
    expr_kind_t kind;
    /* For LITERAL_STR / VAR / SCOPE_LOOKUP: the bare name/string */
    char *str;
    /* For INDEX: target and index expression */
    struct expr_node *target;
    struct expr_node *index;
    /* SCOPE_LOOKUP only: 1 for the scope['x'] index form, which is strict in
     * Puppet 8 (a missing qualified variable raises); 0 for the defensive
     * scope.lookupvar('x') form, which keeps returning nil. (Item 23.) */
    int strict_scope;
} expr_node_t;

typedef enum {
    NODE_TEXT,    /* raw text segment */
    NODE_PUTS,    /* <%= expr %> */
    NODE_COMMENT  /* <%# ... %> -- emits nothing */
} node_kind_t;

typedef struct ast_node {
    node_kind_t kind;
    /* For TEXT: literal string (heap, owned) */
    char *text;
    size_t text_len;
    /* For PUTS: the expression to evaluate */
    expr_node_t *expr;
    /* Trim markers inherited from preceding/following tag */
    bool drop_prev;   /* preceding text should drop trailing whitespace+nl */
    bool drop_next;   /* following text should drop one leading newline */
} ast_node_t;

typedef struct {
    ast_node_t *items;
    size_t count;
    size_t cap;
} ast_t;

/* ---------------- Free helpers ---------------- */

static void expr_free(expr_node_t *e) {
    if (!e) return;
    if (e->str) puppet_free(e->str);
    if (e->target) expr_free(e->target);
    if (e->index) expr_free(e->index);
    puppet_free(e);
}

static void ast_free(ast_t *ast) {
    if (!ast) return;
    for (size_t i = 0; i < ast->count; i++) {
        ast_node_t *n = &ast->items[i];
        if (n->text) puppet_free(n->text);
        if (n->expr) expr_free(n->expr);
    }
    puppet_free(ast->items);
    puppet_free(ast);
}

static void ast_push(ast_t *ast, ast_node_t node) {
    if (ast->count == ast->cap) {
        size_t newcap = ast->cap ? ast->cap * 2 : 16;
        ast->items = puppet_realloc(ast->items, newcap * sizeof(ast_node_t));
        ast->cap = newcap;
    }
    ast->items[ast->count++] = node;
}

/* ---------------- Expression parser ---------------- */

/*
 * The expression parser operates on a NUL-terminated buffer holding the
 * content between <%= and %> (with the optional - markers stripped). It
 * returns NULL on any unsupported construct so the caller can fall back.
 */

typedef struct {
    const char *p;
    const char *end;
} pcur_t;

static void skip_ws(pcur_t *c) {
    while (c->p < c->end && isspace((unsigned char)*c->p)) c->p++;
}

static bool peek(pcur_t *c, char ch) {
    skip_ws(c);
    return c->p < c->end && *c->p == ch;
}

static bool consume(pcur_t *c, char ch) {
    skip_ws(c);
    if (c->p < c->end && *c->p == ch) { c->p++; return true; }
    return false;
}

static bool starts_with(pcur_t *c, const char *s) {
    skip_ws(c);
    size_t n = strlen(s);
    if ((size_t)(c->end - c->p) < n) return false;
    return memcmp(c->p, s, n) == 0;
}

static bool consume_str(pcur_t *c, const char *s) {
    if (!starts_with(c, s)) return false;
    c->p += strlen(s);
    return true;
}

/* Parse an identifier: [A-Za-z_][A-Za-z0-9_]* — returns heap copy or NULL. */
static char *parse_ident(pcur_t *c) {
    skip_ws(c);
    if (c->p >= c->end) return NULL;
    unsigned char first = (unsigned char)*c->p;
    if (!isalpha(first) && first != '_') return NULL;
    const char *s = c->p;
    while (c->p < c->end) {
        unsigned char ch = (unsigned char)*c->p;
        if (isalnum(ch) || ch == '_') c->p++;
        else break;
    }
    size_t n = c->p - s;
    char *out = puppet_malloc(n + 1);
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

/*
 * Parse a quoted string literal — single or double quoted.
 * Rejects double-quoted strings containing #{ (Ruby interpolation), to
 * leave that case for the Ruby fallback.
 *
 * Returns heap copy of the contents (without quotes), or NULL on failure.
 */
static char *parse_string_literal(pcur_t *c) {
    skip_ws(c);
    if (c->p >= c->end) return NULL;
    char q = *c->p;
    if (q != '\'' && q != '"') return NULL;
    c->p++;
    const char *s = c->p;
    while (c->p < c->end && *c->p != q) {
        if (*c->p == '\\' && c->p + 1 < c->end) { c->p += 2; continue; }
        if (q == '"' && *c->p == '#' && c->p + 1 < c->end && c->p[1] == '{') {
            return NULL;  /* unsupported interpolation */
        }
        c->p++;
    }
    if (c->p >= c->end || *c->p != q) return NULL;
    size_t n = c->p - s;
    c->p++;  /* skip closing quote */
    /* Process simple escapes for double-quoted strings. */
    char *out = puppet_malloc(n + 1);
    size_t oi = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            char nxt = s[i + 1];
            switch (nxt) {
                case 'n': out[oi++] = '\n'; break;
                case 't': out[oi++] = '\t'; break;
                case 'r': out[oi++] = '\r'; break;
                case '\\': out[oi++] = '\\'; break;
                case '\'': out[oi++] = '\''; break;
                case '"': out[oi++] = '"'; break;
                default: out[oi++] = nxt; break;
            }
            i++;
        } else {
            out[oi++] = s[i];
        }
    }
    out[oi] = '\0';
    return out;
}

/* Forward decl */
static expr_node_t *parse_term(pcur_t *c);

/*
 * After parsing a primary term, consume any chain of [ idx ] suffixes.
 * Each suffix wraps the current expression in an EXPR_INDEX node.
 */
static expr_node_t *parse_indexes(pcur_t *c, expr_node_t *base) {
    while (peek(c, '[')) {
        c->p++;  /* consume [ */
        expr_node_t *idx = parse_term(c);
        if (!idx) { expr_free(base); return NULL; }
        if (!consume(c, ']')) {
            expr_free(base);
            expr_free(idx);
            return NULL;
        }
        expr_node_t *node = puppet_calloc(1, sizeof(expr_node_t));
        node->kind = EXPR_INDEX;
        node->target = base;
        node->index = idx;
        base = node;
    }
    return base;
}

/*
 * Parse a single primary term (no operators):
 *   - 'string' or "string"
 *   - @ident
 *   - scope.lookupvar('x')  /  scope['x']
 *   - bare ident treated as @ident (Puppet-friendly)
 *
 * Followed by zero or more [...] index suffixes.
 *
 * Returns NULL if the input doesn't match the supported forms.
 */
static expr_node_t *parse_term(pcur_t *c) {
    skip_ws(c);
    if (c->p >= c->end) return NULL;
    expr_node_t *base = NULL;

    /* String literal */
    if (*c->p == '\'' || *c->p == '"') {
        char *s = parse_string_literal(c);
        if (!s) return NULL;
        base = puppet_calloc(1, sizeof(expr_node_t));
        base->kind = EXPR_LITERAL_STR;
        base->str = s;
        return parse_indexes(c, base);
    }

    /* Instance variable @name */
    if (*c->p == '@') {
        c->p++;
        char *name = parse_ident(c);
        if (!name) return NULL;
        base = puppet_calloc(1, sizeof(expr_node_t));
        base->kind = EXPR_VAR;
        base->str = name;
        return parse_indexes(c, base);
    }

    /* scope.lookupvar('x') / scope['x'] / scope::ident (rare, unsupported) */
    if (c->end - c->p >= 5 && memcmp(c->p, "scope", 5) == 0 &&
        (c->p + 5 == c->end || !isalnum((unsigned char)c->p[5]) || c->p[5] == '\0')) {
        const char *save = c->p;
        c->p += 5;
        skip_ws(c);
        if (c->p < c->end && *c->p == '[') {
            c->p++;
            char *key = parse_string_literal(c);
            if (!key) { c->p = save; return NULL; }
            if (!consume(c, ']')) { puppet_free(key); c->p = save; return NULL; }
            base = puppet_calloc(1, sizeof(expr_node_t));
            base->kind = EXPR_SCOPE_LOOKUP;
            base->str = key;
            base->strict_scope = 1;
            return parse_indexes(c, base);
        }
        if (consume_str(c, ".lookupvar(")) {
            char *key = parse_string_literal(c);
            if (!key) { c->p = save; return NULL; }
            if (!consume(c, ')')) { puppet_free(key); c->p = save; return NULL; }
            base = puppet_calloc(1, sizeof(expr_node_t));
            base->kind = EXPR_SCOPE_LOOKUP;
            base->str = key;
            return parse_indexes(c, base);
        }
        c->p = save;
    }

    /* Bare identifier — treat as instance variable (matches puppetc's
     * Ruby fallback which sets both $name and @name). */
    char *name = parse_ident(c);
    if (!name) return NULL;
    /* Reject reserved words / known method-call patterns to be safe. */
    if (strcmp(name, "if") == 0 || strcmp(name, "do") == 0 ||
        strcmp(name, "end") == 0 || strcmp(name, "true") == 0 ||
        strcmp(name, "false") == 0 || strcmp(name, "nil") == 0 ||
        strcmp(name, "unless") == 0 || strcmp(name, "case") == 0 ||
        strcmp(name, "while") == 0 || strcmp(name, "for") == 0 ||
        strcmp(name, "begin") == 0) {
        puppet_free(name);
        return NULL;
    }
    /* If the identifier is followed by ( or ., it's a method call we
     * don't support (e.g. .each, .empty?, .to_s). */
    skip_ws(c);
    if (c->p < c->end && (*c->p == '(' || *c->p == '.')) {
        puppet_free(name);
        return NULL;
    }
    base = puppet_calloc(1, sizeof(expr_node_t));
    base->kind = EXPR_VAR;
    base->str = name;
    return parse_indexes(c, base);
}

/*
 * Top-level expression parse: a single term with optional indexes.
 * Anything left in the buffer after parsing means an unsupported
 * construct, so we fail.
 */
static expr_node_t *parse_expression(const char *src, size_t len) {
    pcur_t c = { src, src + len };
    expr_node_t *e = parse_term(&c);
    if (!e) return NULL;
    skip_ws(&c);
    if (c.p != c.end) { expr_free(e); return NULL; }
    return e;
}

/* ---------------- Tokenizer + AST builder ---------------- */

/*
 * Walk the template content, splitting it into TEXT segments and
 * <%= ... %> / <%# ... %> blocks. Returns NULL on unsupported constructs:
 *   - <% statement %>  (no = and no # — control flow / assignment)
 *   - missing %> closer
 *   - parse_expression failure inside a <%= block
 */
static ast_t *build_ast(const char *content) {
    ast_t *ast = puppet_calloc(1, sizeof(ast_t));
    const char *p = content;
    const char *end = content + strlen(content);

    while (p < end) {
        /* Find next '<%' */
        const char *open = p;
        while (open < end) {
            if (open[0] == '<' && open + 1 < end && open[1] == '%') break;
            open++;
        }
        /* Emit text segment p..open as a TEXT node (may be empty) */
        if (open > p) {
            ast_node_t n = {0};
            n.kind = NODE_TEXT;
            n.text_len = open - p;
            n.text = puppet_malloc(n.text_len + 1);
            memcpy(n.text, p, n.text_len);
            n.text[n.text_len] = '\0';
            ast_push(ast, n);
        }
        if (open >= end) break;

        /* Parse the tag */
        const char *tagstart = open + 2;  /* skip <% */
        bool drop_prev = false;
        bool is_comment = false;

        if (tagstart < end && *tagstart == '-') {
            drop_prev = true;
            tagstart++;
        }
        if (tagstart < end && *tagstart == '=') {
            tagstart++;
        } else if (tagstart < end && *tagstart == '#') {
            is_comment = true;
            tagstart++;
        } else {
            /* Plain <% statement %> — unsupported (control flow / assign). */
            ast_free(ast);
            return NULL;
        }

        /* Find matching %> (handle -%>) */
        const char *q = tagstart;
        const char *close = NULL;
        bool drop_next = false;
        while (q + 1 < end) {
            if (q[0] == '%' && q[1] == '>') { close = q; break; }
            if (q[0] == '-' && q + 2 < end && q[1] == '%' && q[2] == '>') {
                close = q + 1;
                drop_next = true;
                break;
            }
            q++;
        }
        if (!close) { ast_free(ast); return NULL; }

        size_t inner_len = (drop_next ? close - 1 : close) - tagstart;
        const char *inner = tagstart;

        if (is_comment) {
            ast_node_t n = {0};
            n.kind = NODE_COMMENT;
            n.drop_prev = drop_prev;
            n.drop_next = drop_next;
            ast_push(ast, n);
        } else {
            /* is_puts */
            expr_node_t *e = parse_expression(inner, inner_len);
            if (!e) { ast_free(ast); return NULL; }
            ast_node_t n = {0};
            n.kind = NODE_PUTS;
            n.expr = e;
            n.drop_prev = drop_prev;
            n.drop_next = drop_next;
            ast_push(ast, n);
        }

        p = close + 2;  /* skip %> */
    }

    return ast;
}

/* ---------------- Evaluation ---------------- */

/*
 * Append a puppet_value_t rendered as a string to the buffer.
 * Mirrors what Ruby's string interpolation does for the supported types.
 */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} sbuf_t;

static void sbuf_append(sbuf_t *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t newcap = b->cap ? b->cap * 2 : 256;
        while (newcap < b->len + n + 1) newcap *= 2;
        b->data = puppet_realloc(b->data, newcap);
        b->cap = newcap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void sbuf_append_cstr(sbuf_t *b, const char *s) {
    sbuf_append(b, s, strlen(s));
}

/* Forward decl */
static bool render_value(sbuf_t *b, puppet_value_t *v);

static bool render_value(sbuf_t *b, puppet_value_t *v) {
    if (!v || v->type == PUPPET_VALUE_UNDEF) {
        /* Ruby ERB renders nil as "" via to_s. Match that. */
        return true;
    }
    switch (v->type) {
        case PUPPET_VALUE_STRING:
            sbuf_append(b, v->data.string.data, v->data.string.len);
            return true;
        case PUPPET_VALUE_BOOL:
            sbuf_append_cstr(b, v->data.boolean ? "true" : "false");
            return true;
        case PUPPET_VALUE_NUMBER: {
            char tmp[64];
            double n = v->data.number;
            if (n == (long)n) snprintf(tmp, sizeof(tmp), "%ld", (long)n);
            else snprintf(tmp, sizeof(tmp), "%g", n);
            sbuf_append_cstr(b, tmp);
            return true;
        }
        case PUPPET_VALUE_ARRAY: {
            /* Ruby's Array#to_s: "[a, b, c]" with quoted strings. To avoid
             * format mismatches for templates that depend on this, reject
             * here so we fall back to Ruby. */
            return false;
        }
        case PUPPET_VALUE_HASH:
            return false;
        default:
            return false;
    }
}

/*
 * Evaluate an expression to a puppet_value_t*. The returned value is NOT
 * owned by the caller — callers must not free it. (Lookups return
 * pointers into the env; constants return pointers into a small per-call
 * cache held in the eval context.)
 *
 * Returns NULL on evaluation failure (template should fall back).
 */
typedef struct {
    /* Scratch values for literals, freed at end of render. */
    puppet_value_t **scratch;
    size_t scratch_count;
    size_t scratch_cap;
    /* Set when a strict scope['a::b'] lookup missed (item 23). The native
     * render aborts so the Ruby fallback re-renders and raises the proper
     * "Undefined variable" error. */
    bool strict_miss;
} eval_ctx_t;

static puppet_value_t *eval_alloc_scratch(eval_ctx_t *ctx, puppet_value_t *v) {
    if (ctx->scratch_count == ctx->scratch_cap) {
        size_t nc = ctx->scratch_cap ? ctx->scratch_cap * 2 : 8;
        ctx->scratch = puppet_realloc(ctx->scratch, nc * sizeof(puppet_value_t *));
        ctx->scratch_cap = nc;
    }
    ctx->scratch[ctx->scratch_count++] = v;
    return v;
}

static void eval_ctx_cleanup(eval_ctx_t *ctx) {
    for (size_t i = 0; i < ctx->scratch_count; i++) {
        puppet_value_destroy(ctx->scratch[i]);
    }
    puppet_free(ctx->scratch);
}

static puppet_value_t *eval_expr(expr_node_t *e, puppet_env_t *env, eval_ctx_t *ctx);

static puppet_value_t *eval_expr(expr_node_t *e, puppet_env_t *env, eval_ctx_t *ctx) {
    if (!e) return NULL;
    switch (e->kind) {
        case EXPR_LITERAL_STR: {
            puppet_value_t *v = puppet_value_create_string(e->str, strlen(e->str));
            return eval_alloc_scratch(ctx, v);
        }
        case EXPR_VAR:
        case EXPR_SCOPE_LOOKUP: {
            puppet_value_t *v = puppet_variable_lookup_chain(env, e->str);
            if (v) return v;
            /* Fall back to facts (puppet_facts_get returns a fresh value
             * in some cases — track for free). */
            v = puppet_facts_get(env, e->str);
            if (v) return eval_alloc_scratch(ctx, v);
            /* Puppet 8 strict resolver: scope['a::b'] on an undefined
             * class-qualified variable must raise, not render empty. Abort
             * the native render; the Ruby fallback raises the real error
             * (item 23). lookupvar / @var stay forgiving. */
            if (e->kind == EXPR_SCOPE_LOOKUP && e->strict_scope) {
                const char *n = e->str;
                if (n[0] == ':' && n[1] == ':') n += 2;
                if (strstr(n, "::")) ctx->strict_miss = true;
            }
            /* Undefined → empty string (matches Ruby ERB lookupvar). */
            return NULL;
        }
        case EXPR_INDEX: {
            puppet_value_t *base = eval_expr(e->target, env, ctx);
            puppet_value_t *idx = eval_expr(e->index, env, ctx);
            if (!base) return NULL;
            if (!idx) return NULL;
            if (base->type == PUPPET_VALUE_HASH && idx->type == PUPPET_VALUE_STRING) {
                return puppet_hash_get(base->data.hash, idx->data.string.data, idx->data.string.len);
            }
            if (base->type == PUPPET_VALUE_ARRAY && idx->type == PUPPET_VALUE_NUMBER) {
                size_t i = (size_t)idx->data.number;
                if (i < base->data.array->count) return base->data.array->items[i];
                return NULL;
            }
            return NULL;
        }
    }
    return NULL;
}

/*
 * Apply trim semantics:
 *   drop_prev on the *current* tag → strip trailing whitespace+newline
 *                                    from the previously emitted text
 *   drop_next on the *current* tag → strip one leading newline from
 *                                    the next text segment
 *
 * We follow Ruby ERB trim_mode: '-' semantics:
 *   <%-  : if the line containing the tag is whitespace-only up to the
 *          tag, remove that whitespace (and the preceding newline). We
 *          implement a simpler variant: drop trailing spaces/tabs from
 *          the prev text up to (but not including) the line's start.
 *   -%>  : remove a single trailing newline (and trailing whitespace
 *          on the same line) from the buffer right after %>. Implemented
 *          here as: skip one optional run of spaces+tabs then one
 *          newline at the start of the next text segment.
 */
static void apply_drop_prev_to_buffer(sbuf_t *b) {
    /* Trim trailing spaces/tabs from buffer, up to (not past) a newline. */
    while (b->len > 0) {
        char c = b->data[b->len - 1];
        if (c == ' ' || c == '\t') { b->len--; b->data[b->len] = '\0'; }
        else break;
    }
}

static size_t skip_drop_next_at(const char *s, size_t n) {
    /* Skip optional spaces/tabs then a single \n or \r\n at the start. */
    size_t i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i < n && s[i] == '\r' && i + 1 < n && s[i + 1] == '\n') return i + 2;
    if (i < n && (s[i] == '\n' || s[i] == '\r')) return i + 1;
    return 0;  /* nothing to drop */
}

/* ---------------- Cache ---------------- */

typedef struct cache_entry {
    char *key;        /* template_name (path) — owned */
    ast_t *ast;       /* parsed AST — owned */
    bool unsupported; /* parse returned NULL: cache the negative result */
    struct cache_entry *next;
} cache_entry_t;

static cache_entry_t *g_cache = NULL;
static pthread_mutex_t g_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

static cache_entry_t *cache_lookup(const char *key) {
    for (cache_entry_t *e = g_cache; e; e = e->next) {
        if (strcmp(e->key, key) == 0) return e;
    }
    return NULL;
}

static cache_entry_t *cache_insert(const char *key, ast_t *ast, bool unsupported) {
    cache_entry_t *e = puppet_calloc(1, sizeof(cache_entry_t));
    e->key = puppet_strdup(key);
    e->ast = ast;
    e->unsupported = unsupported;
    e->next = g_cache;
    g_cache = e;
    return e;
}

void puppet_erb_native_cache_clear(void) {
    pthread_mutex_lock(&g_cache_mutex);
    cache_entry_t *e = g_cache;
    while (e) {
        cache_entry_t *next = e->next;
        if (e->ast) ast_free(e->ast);
        puppet_free(e->key);
        puppet_free(e);
        e = next;
    }
    g_cache = NULL;
    pthread_mutex_unlock(&g_cache_mutex);
}

/* ---------------- Public entry ---------------- */

char *puppet_erb_native_render(const char *content,
                               const char *template_name,
                               puppet_env_t *env) {
    if (!content) return NULL;

    ast_t *ast = NULL;
    bool from_cache = false;

    if (template_name) {
        pthread_mutex_lock(&g_cache_mutex);
        cache_entry_t *e = cache_lookup(template_name);
        if (e) {
            if (e->unsupported) {
                pthread_mutex_unlock(&g_cache_mutex);
                return NULL;  /* known-unsupported, fall back without re-parse */
            }
            ast = e->ast;
            from_cache = true;
        }
        pthread_mutex_unlock(&g_cache_mutex);
    }

    if (!ast) {
        ast = build_ast(content);
        if (template_name) {
            pthread_mutex_lock(&g_cache_mutex);
            /* Only insert if no other thread beat us to it. */
            if (!cache_lookup(template_name)) {
                cache_insert(template_name, ast, ast == NULL);
                from_cache = true;  /* AST is now owned by cache */
            }
            pthread_mutex_unlock(&g_cache_mutex);
        }
        if (!ast) return NULL;
    }

    /* Render */
    sbuf_t buf = {0};
    eval_ctx_t ctx = {0};

    /* Pending drop-next from previous tag: applied to the next TEXT node. */
    bool pending_drop_next = false;

    bool ok = true;
    for (size_t i = 0; i < ast->count && ok; i++) {
        ast_node_t *n = &ast->items[i];
        switch (n->kind) {
            case NODE_TEXT: {
                const char *t = n->text;
                size_t tn = n->text_len;
                if (pending_drop_next) {
                    size_t skipped = skip_drop_next_at(t, tn);
                    t += skipped;
                    tn -= skipped;
                    pending_drop_next = false;
                }
                sbuf_append(&buf, t, tn);
                break;
            }
            case NODE_PUTS: {
                if (n->drop_prev) apply_drop_prev_to_buffer(&buf);
                puppet_value_t *v = eval_expr(n->expr, env, &ctx);
                if (ctx.strict_miss) { ok = false; break; }
                if (!render_value(&buf, v)) { ok = false; break; }
                if (n->drop_next) pending_drop_next = true;
                break;
            }
            case NODE_COMMENT:
                if (n->drop_prev) apply_drop_prev_to_buffer(&buf);
                if (n->drop_next) pending_drop_next = true;
                break;
        }
    }

    eval_ctx_cleanup(&ctx);

    if (!ok) {
        puppet_free(buf.data);
        if (!from_cache) ast_free(ast);
        return NULL;
    }

    char *out = buf.data;
    if (!out) out = puppet_strdup("");
    if (!from_cache) ast_free(ast);
    return out;
}
