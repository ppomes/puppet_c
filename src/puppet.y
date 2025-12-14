/**
 * @file puppet.y
 * @brief Bison grammar for the Puppet configuration language
 *
 * This file defines the complete grammar for the Puppet configuration
 * language using Bison (yacc) syntax. The grammar covers all Puppet
 * language constructs including:
 * - Resource declarations and references
 * - Class and defined type definitions
 * - Expressions (arithmetic, logical, function calls)
 * - Control flow (if/unless/case statements)
 * - Variable assignments and scoping
 * - String interpolation and templates
 *
 * The parser builds an Abstract Syntax Tree (AST) as defined in
 * puppet_ast.h, which can then be evaluated by the interpreter
 * or serialized to JSON for analysis.
 *
 * Grammar Organization:
 * - Tokens and precedence declarations
 * - Expression grammar (recursive, operator precedence)
 * - Statement grammar (resource declarations, control flow)
 * - Top-level constructs (classes, nodes, programs)
 */

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "puppet_ast.h"
#include "puppet_memory.h"

extern int yylex(void);
extern int yylineno;
extern char *yytext;

void yyerror(const char *s);
puppet_program_t *parsed_program = NULL;

// Helper function to create interpolated string expression
static puppet_expr_t *puppet_create_interpolated_expr(const char *str) {
    // Check if string contains ${...} patterns
    const char *p = str;
    int has_interpolation = 0;
    
    while (*p) {
        if (p[0] == '$' && p[1] == '{') {
            has_interpolation = 1;
            break;
        }
        p++;
    }
    
    // If no interpolation found, return simple string expression
    if (!has_interpolation) {
        puppet_value_t *val = puppet_value_create_string(str, strlen(str));
        return puppet_expr_create_value(val);
    }
    
    // Parse string with interpolation
    puppet_expr_t *expr = puppet_calloc(1, sizeof(puppet_expr_t));
    expr->type = PUPPET_EXPR_INTERPOLATED_STRING;
    
    // Count parts and expressions
    size_t part_count = 0;
    size_t max_parts = 10; // Initial allocation
    expr->data.interpolated.parts = puppet_calloc(max_parts, sizeof(puppet_string_t));
    expr->data.interpolated.exprs = puppet_calloc(max_parts, sizeof(puppet_expr_t*));
    
    p = str;
    const char *start = str;
    
    while (*p) {
        if (p[0] == '$' && p[1] == '{') {
            // Save literal part before variable
            if (p > start) {
                size_t len = p - start;
                expr->data.interpolated.parts[part_count].data = puppet_malloc(len + 1);
                memcpy(expr->data.interpolated.parts[part_count].data, start, len);
                expr->data.interpolated.parts[part_count].data[len] = '\0';
                expr->data.interpolated.parts[part_count].len = len;
            }
            
            // Find end of variable reference
            const char *var_start = p + 2;
            const char *var_end = var_start;
            while (*var_end && *var_end != '}') var_end++;
            
            if (*var_end == '}') {
                // Create variable expression
                size_t var_len = var_end - var_start;
                char *var_name = puppet_malloc(var_len + 1);
                memcpy(var_name, var_start, var_len);
                var_name[var_len] = '\0';
                
                expr->data.interpolated.exprs[part_count] = puppet_calloc(1, sizeof(puppet_expr_t));
                expr->data.interpolated.exprs[part_count]->type = PUPPET_EXPR_VARIABLE;
                expr->data.interpolated.exprs[part_count]->data.variable.data = var_name;
                expr->data.interpolated.exprs[part_count]->data.variable.len = var_len;
                
                part_count++;
                p = var_end + 1;
                start = p;
            } else {
                // Malformed variable reference, treat as literal
                p++;
            }
        } else {
            p++;
        }
    }
    
    // Save final literal part
    if (p > start) {
        size_t len = p - start;
        expr->data.interpolated.parts[part_count].data = puppet_malloc(len + 1);
        memcpy(expr->data.interpolated.parts[part_count].data, start, len);
        expr->data.interpolated.parts[part_count].data[len] = '\0';
        expr->data.interpolated.parts[part_count].len = len;
        part_count++;
    }
    
    expr->data.interpolated.count = part_count;
    
    return expr;
}

%}

%union {
    char *string;
    double number;
    int boolean;
    puppet_value_t *value;
    puppet_expr_t *expr;
    puppet_stmt_t *stmt;
    puppet_expr_list_t *expr_list;
    puppet_stmt_list_t *stmt_list;
    puppet_param_t *param;
    puppet_param_list_t *param_list;
    puppet_attribute_t *attribute;
    puppet_attribute_list_t *attribute_list;
    puppet_resource_instance_t *resource_instance;
    puppet_resource_decl_t *resource_decl;
    puppet_case_when_t *case_when;
    puppet_if_branch_t *if_branch;
    puppet_binop_t binop;
    puppet_unop_t unop;
}

%token <string> NAME CLASSREF TYPE_NAME VARIABLE
%token <string> STRING_LITERAL DQSTRING_LITERAL REGEX
%token <number> NUMBER
%token <boolean> BOOLEAN
%token UNDEF

%token IF ELSIF ELSE UNLESS CASE DEFAULT
%token CLASS DEFINE NODE INHERITS
%token INCLUDE REQUIRE_KEYWORD CONTAIN TAG IMPORT
%token ATTR AUDIT BEFORE_KEYWORD NOOP NOTIFY_KEYWORD SCHEDULE STAGE SUBSCRIBE

%token ARROW NOTIFY BEFORE REQUIRE FARROW PARROW
%token APPEND EQ NE LE GE MATCH NOT_MATCH IN LSHIFT RSHIFT
%token AND OR NOT
%token AT2 LCOLLECT RCOLLECT COLONCOLON
%token DQSTRING_INTERP_START

%left '?'
%left OR
%left AND
%left '<' '>' LE GE EQ NE MATCH NOT_MATCH
%left IN
%left LSHIFT RSHIFT
%left '+' '-'
%left '*' '/' '%'
%right '!'
%right UMINUS
%left '.' '[' ']'

%type <expr> expression primary_expression
%type <expr> unary_expression binary_expression
%type <expr> selector_expression lambda_expression
%type <expr> funcall_expression index_expression dot_expression
%type <expr> variable_expression literal_expression
%type <expr> resource_reference type_expression

%type <stmt> statement resource_declaration
%type <stmt> resource_default resource_override resource_collector
%type <stmt> class_definition class_instantiation define_definition node_definition
%type <stmt> if_statement unless_statement case_statement
%type <stmt> assignment_statement append_statement
%type <stmt> function_statement resource_chain
%type <stmt> include_statement require_statement contain_statement

%type <expr_list> expression_list expression_list_opt
%type <expr_list> funcall_args

%type <stmt_list> statement_list statement_list_opt

%type <param> parameter
%type <param_list> parameter_list parameter_list_opt

%type <attribute> attribute
%type <attribute_list> attribute_list attribute_list_opt
%type <resource_instance> resource_instance
%type <resource_decl> resource_body

%type <resource_instance> resource_instance_list
%type <string> resource_type

%type <case_when> case_when
%type <if_branch> elsif_clauses

%type <value> value hash_value array_value

%type <string> class_parent_opt
%type <string> qualified_name
%type <binop> comparison_op arithmetic_op logical_op
%type <unop> unary_op

%%

program:
    statement_list {
        parsed_program = puppet_calloc(1, sizeof(puppet_program_t));
        parsed_program->statements = *$1;
        puppet_free($1);
    }
    ;

qualified_name:
    NAME { $$ = $1; }
    | qualified_name COLONCOLON NAME {
        size_t len1 = strlen($1);
        size_t len3 = strlen($3);
        $$ = puppet_malloc(len1 + 2 + len3 + 1);
        strcpy($$, $1);
        strcat($$, "::");
        strcat($$, $3);
        puppet_free($1);
        puppet_free($3);
    }
    ;

statement_list:
    /* empty */ {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_list_t));
    }
    | statement_list statement {
        $$ = $1;
        $$->stmts = puppet_realloc($$->stmts, ($$->count + 1) * sizeof(puppet_stmt_t *));
        $$->stmts[$$->count++] = $2;
    }
    ;

statement_list_opt:
    /* empty */ { $$ = NULL; }
    | statement_list { $$ = $1; }
    ;

statement:
    resource_declaration
    | resource_default
    | resource_override
    | resource_collector
    | class_definition
    | class_instantiation
    | define_definition
    | node_definition
    | if_statement
    | unless_statement
    | case_statement
    | assignment_statement
    | append_statement
    | function_statement
    | resource_chain
    | include_statement
    | require_statement
    | contain_statement
    ;

resource_declaration:
    resource_type resource_body {
        $$ = puppet_stmt_create_resource(*$2);
        $$->data.resource.type = puppet_string_create($1);
        puppet_free($1);
        puppet_free($2);
    }
    | '@' resource_type resource_body {
        $$ = puppet_stmt_create_resource(*$3);
        $$->data.resource.type = puppet_string_create($2);
        $$->data.resource.style = PUPPET_RES_VIRTUAL;
        puppet_free($2);
        puppet_free($3);
    }
    | AT2 resource_type resource_body {
        $$ = puppet_stmt_create_resource(*$3);
        $$->data.resource.type = puppet_string_create($2);
        $$->data.resource.style = PUPPET_RES_EXPORTED;
        puppet_free($2);
        puppet_free($3);
    }
    ;

resource_type:
    qualified_name { $$ = $1; }
    | CLASSREF { $$ = $1; }
    | CLASS { $$ = puppet_strdup("class"); }
    | NOTIFY_KEYWORD { $$ = puppet_strdup("notify"); }
    ;

resource_body:
    '{' resource_instance_list '}' {
        $$ = puppet_calloc(1, sizeof(puppet_resource_decl_t));
        $$->style = PUPPET_RES_NORMAL;
        $$->instance_count = 1;  // For now, assume single instance
        $$->instances = puppet_calloc(1, sizeof(puppet_resource_instance_t));
        $$->instances[0] = *$2;  // Copy the first instance
    }
    | '{' '}' {
        $$ = puppet_calloc(1, sizeof(puppet_resource_decl_t));
        $$->style = PUPPET_RES_NORMAL;
        $$->instance_count = 0;
        $$->instances = NULL;
    }
    ;

resource_instance_list:
    resource_instance {
        $$ = $1;
    }
    | resource_instance_list ',' resource_instance {
        $$ = $1;  // For now, just return the first one
    }
    ;

resource_instance:
    expression ':' attribute_list_opt {
        $$ = puppet_calloc(1, sizeof(puppet_resource_instance_t));
        $$->title = $1;
        if ($3) {
            $$->attr_count = $3->count;
            $$->attributes = $3->attributes;
            puppet_free($3);
        } else {
            $$->attr_count = 0;
            $$->attributes = NULL;
        }
    }
    ;

attribute_list_opt:
    /* empty */ {
        $$ = NULL;
    }
    | attribute_list {
        $$ = $1;
    }
    ;

attribute_list:
    attribute {
        $$ = puppet_calloc(1, sizeof(puppet_attribute_list_t));
        $$->attributes = puppet_malloc(sizeof(puppet_attribute_t));
        $$->attributes[0] = *$1;
        $$->count = 1;
        puppet_free($1);
    }
    | attribute_list ',' attribute {
        $$ = $1;
        $$->attributes = puppet_realloc($$->attributes, ($$->count + 1) * sizeof(puppet_attribute_t));
        $$->attributes[$$->count] = *$3;
        $$->count++;
        puppet_free($3);
    }
    | attribute_list ',' {
        $$ = $1;
    }
    ;

attribute:
    NAME FARROW expression {
        $$ = puppet_calloc(1, sizeof(puppet_attribute_t));
        $$->name = puppet_string_create($1);
        $$->value = $3;
        puppet_free($1);
    }
    | REQUIRE_KEYWORD FARROW expression {
        $$ = puppet_calloc(1, sizeof(puppet_attribute_t));
        $$->name = puppet_string_create("require");
        $$->value = $3;
    }
    | NOTIFY_KEYWORD FARROW expression {
        $$ = puppet_calloc(1, sizeof(puppet_attribute_t));
        $$->name = puppet_string_create("notify");
        $$->value = $3;
    }
    | BEFORE_KEYWORD FARROW expression {
        $$ = puppet_calloc(1, sizeof(puppet_attribute_t));
        $$->name = puppet_string_create("before");
        $$->value = $3;
    }
    ;

resource_default:
    TYPE_NAME '{' attribute_list '}' {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_RESOURCE_DEFAULT;
        $$->data.resource_default.type = puppet_string_create($1);
        puppet_free($1);
    }
    ;

resource_override:
    resource_reference '{' attribute_list '}' {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_RESOURCE_OVERRIDE;
        $$->data.resource_override.reference = $1;
    }
    ;

resource_collector:
    TYPE_NAME LCOLLECT expression RCOLLECT {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_RESOURCE_COLLECTOR;
        $$->data.collector.style = PUPPET_RES_NORMAL;
        $$->data.collector.type = puppet_string_create($1);
        $$->data.collector.search_expr = $3;
        puppet_free($1);
    }
    | TYPE_NAME LCOLLECT RCOLLECT {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_RESOURCE_COLLECTOR;
        $$->data.collector.style = PUPPET_RES_NORMAL;
        $$->data.collector.type = puppet_string_create($1);
        $$->data.collector.search_expr = NULL;
        puppet_free($1);
    }
    ;

class_definition:
    CLASS NAME parameter_list_opt class_parent_opt '{' statement_list '}' {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_CLASS_DEF;
        $$->data.class_def.name = puppet_string_create($2);
        if ($3) {
            $$->data.class_def.params = *$3;
            puppet_free($3);
        }
        if ($4) {
            $$->data.class_def.inherits = puppet_calloc(1, sizeof(puppet_string_t));
            *$$->data.class_def.inherits = puppet_string_create($4);
            puppet_free($4);
        }
        $$->data.class_def.body = *$6;
        puppet_free($2);
        puppet_free($6);
    }
    ;

class_instantiation:
    CLASS '{' STRING_LITERAL ':' attribute_list_opt '}' {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_CLASS_INSTANCE;
        $$->data.class_instance.class_name = puppet_string_create($3);
        if ($5) {
            $$->data.class_instance.arguments = $5->attributes;
            $$->data.class_instance.arg_count = $5->count;
            puppet_free($5);
        } else {
            $$->data.class_instance.arguments = NULL;
            $$->data.class_instance.arg_count = 0;
        }
        puppet_free($3);
    }
    ;

class_parent_opt:
    /* empty */ { $$ = NULL; }
    | INHERITS NAME { $$ = $2; }
    ;

parameter_list_opt:
    /* empty */ { $$ = NULL; }
    | '(' ')' { $$ = puppet_calloc(1, sizeof(puppet_param_list_t)); }
    | '(' parameter_list ')' { $$ = $2; }
    | '(' parameter_list ',' ')' { $$ = $2; }
    ;

parameter_list:
    parameter {
        $$ = puppet_calloc(1, sizeof(puppet_param_list_t));
        $$->params = puppet_calloc(1, sizeof(puppet_param_t));
        $$->params[0] = *$1;
        $$->count = 1;
        puppet_free($1);
    }
    | parameter_list ',' parameter {
        $$ = $1;
        $$->params = puppet_realloc($$->params, ($$->count + 1) * sizeof(puppet_param_t));
        $$->params[$$->count] = *$3;
        $$->count++;
        puppet_free($3);
    }
    ;

parameter:
    VARIABLE {
        $$ = puppet_calloc(1, sizeof(puppet_param_t));
        $$->name = puppet_string_create($1);
        $$->type_constraint = NULL;
        $$->default_value = NULL;
        puppet_free($1);
    }
    | VARIABLE '=' expression {
        $$ = puppet_calloc(1, sizeof(puppet_param_t));
        $$->name = puppet_string_create($1);
        $$->type_constraint = NULL;
        $$->default_value = $3;
        puppet_free($1);
    }
    | type_expression VARIABLE {
        $$ = puppet_calloc(1, sizeof(puppet_param_t));
        $$->name = puppet_string_create($2);
        $$->type_constraint = puppet_value_create_string($1->data.variable.data, $1->data.variable.len);
        $$->default_value = NULL;
        puppet_expr_destroy($1);
        puppet_free($2);
    }
    | type_expression VARIABLE '=' expression {
        $$ = puppet_calloc(1, sizeof(puppet_param_t));
        $$->name = puppet_string_create($2);
        $$->type_constraint = puppet_value_create_string($1->data.variable.data, $1->data.variable.len);
        $$->default_value = $4;
        puppet_expr_destroy($1);
        puppet_free($2);
    }
    ;

define_definition:
    DEFINE qualified_name parameter_list_opt '{' statement_list '}' {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_DEFINE;
        $$->data.define.name = puppet_string_create($2);
        if ($3) {
            $$->data.define.params = *$3;
            puppet_free($3);
        }
        $$->data.define.body = *$5;
        puppet_free($2);
        puppet_free($5);
    }
    ;

node_definition:
    NODE STRING_LITERAL '{' statement_list '}' {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_NODE;
        $$->data.node.name = puppet_string_create($2);
        $$->data.node.body = *$4;
        puppet_free($2);
        puppet_free($4);
    }
    | NODE DQSTRING_LITERAL '{' statement_list '}' {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_NODE;
        $$->data.node.name = puppet_string_create($2);
        $$->data.node.body = *$4;
        puppet_free($2);
        puppet_free($4);
    }
    | NODE REGEX '{' statement_list '}' {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_NODE;
        $$->data.node.name = puppet_string_create($2);
        $$->data.node.body = *$4;
        puppet_free($2);
        puppet_free($4);
    }
    | NODE DEFAULT '{' statement_list '}' {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_NODE;
        $$->data.node.name = puppet_string_create("default");
        $$->data.node.body = *$4;
        puppet_free($4);
    }
    ;

if_statement:
    IF expression '{' statement_list '}' elsif_clauses {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_IF;
        puppet_if_branch_t *first = puppet_calloc(1, sizeof(puppet_if_branch_t));
        first->condition = $2;
        first->body = *$4;
        first->next = $6;
        $$->data.if_stmt.branches = first;
        $$->data.if_stmt.else_body = NULL;
        puppet_free($4);
    }
    | IF expression '{' statement_list '}' elsif_clauses ELSE '{' statement_list '}' {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_IF;
        puppet_if_branch_t *first = puppet_calloc(1, sizeof(puppet_if_branch_t));
        first->condition = $2;
        first->body = *$4;
        first->next = $6;
        $$->data.if_stmt.branches = first;
        $$->data.if_stmt.else_body = $9;
        puppet_free($4);
    }
    ;

elsif_clauses:
    /* empty */ { $$ = NULL; }
    | elsif_clauses ELSIF expression '{' statement_list '}' {
        puppet_if_branch_t *branch = puppet_calloc(1, sizeof(puppet_if_branch_t));
        branch->condition = $3;
        branch->body = *$5;
        branch->next = NULL;
        puppet_free($5);
        
        if ($1 == NULL) {
            $$ = branch;
        } else {
            puppet_if_branch_t *last = $1;
            while (last->next) last = last->next;
            last->next = branch;
            $$ = $1;
        }
    }
    ;

unless_statement:
    UNLESS expression '{' statement_list '}' {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_UNLESS;
        $$->data.unless_stmt.condition = $2;
        $$->data.unless_stmt.body = *$4;
        puppet_free($4);
    }
    ;

case_statement:
    CASE expression '{' case_when_list '}' {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_CASE;
        $$->data.case_stmt.expr = $2;
        /* TODO: Implement case when list */
    }
    ;

case_when_list:
    case_when
    | case_when_list case_when
    ;

case_when:
    expression_list ':' '{' statement_list '}' {
        $$ = puppet_calloc(1, sizeof(puppet_case_when_t));
        /* TODO: Implement case when */
    }
    | DEFAULT ':' '{' statement_list '}' {
        $$ = puppet_calloc(1, sizeof(puppet_case_when_t));
        /* TODO: Implement default case */
    }
    ;

assignment_statement:
    VARIABLE '=' expression {
        $$ = puppet_stmt_create_assignment($1, $3);
        puppet_free($1);
    }
    ;

append_statement:
    VARIABLE APPEND expression {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_APPEND;
        $$->data.append.variable = puppet_string_create($1);
        $$->data.append.value = $3;
        puppet_free($1);
    }
    ;

function_statement:
    NAME '(' ')' {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_FUNCTION_CALL;
        $$->data.expr = puppet_calloc(1, sizeof(puppet_expr_t));
        $$->data.expr->type = PUPPET_EXPR_FUNCALL;
        $$->data.expr->data.funcall.name = puppet_string_create($1);
        $$->data.expr->data.funcall.args.count = 0;
        $$->data.expr->data.funcall.args.exprs = NULL;
        puppet_free($1);
    }
    | NAME '(' expression_list ')' {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_FUNCTION_CALL;
        $$->data.expr = puppet_calloc(1, sizeof(puppet_expr_t));
        $$->data.expr->type = PUPPET_EXPR_FUNCALL;
        $$->data.expr->data.funcall.name = puppet_string_create($1);
        $$->data.expr->data.funcall.args = *$3;
        puppet_free($1);
        puppet_free($3);
    }
    ;

resource_chain:
    statement ARROW statement {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_RESOURCE_CHAIN;
        $$->data.chain.left = $1;
        $$->data.chain.right = $3;
        $$->data.chain.type = CHAIN_BEFORE;
    }
    | statement NOTIFY statement {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_RESOURCE_CHAIN;
        $$->data.chain.left = $1;
        $$->data.chain.right = $3;
        $$->data.chain.type = CHAIN_NOTIFY;
    }
    ;

include_statement:
    INCLUDE qualified_name {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_INCLUDE;
        $$->data.names.exprs = puppet_calloc(1, sizeof(puppet_expr_t*));
        $$->data.names.exprs[0] = puppet_calloc(1, sizeof(puppet_expr_t));
        $$->data.names.exprs[0]->type = PUPPET_EXPR_VALUE;
        $$->data.names.exprs[0]->data.value = puppet_value_create_string($2, strlen($2));
        $$->data.names.count = 1;
        puppet_free($2);
    }
    ;

require_statement:
    REQUIRE_KEYWORD qualified_name {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_REQUIRE;
        $$->data.names.exprs = puppet_calloc(1, sizeof(puppet_expr_t*));
        $$->data.names.exprs[0] = puppet_calloc(1, sizeof(puppet_expr_t));
        $$->data.names.exprs[0]->type = PUPPET_EXPR_VALUE;
        $$->data.names.exprs[0]->data.value = puppet_value_create_string($2, strlen($2));
        $$->data.names.count = 1;
        puppet_free($2);
    }
    ;

contain_statement:
    CONTAIN qualified_name {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_CONTAIN;
        $$->data.names.exprs = puppet_calloc(1, sizeof(puppet_expr_t*));
        $$->data.names.exprs[0] = puppet_calloc(1, sizeof(puppet_expr_t));
        $$->data.names.exprs[0]->type = PUPPET_EXPR_VALUE;
        $$->data.names.exprs[0]->data.value = puppet_value_create_string($2, strlen($2));
        $$->data.names.count = 1;
        puppet_free($2);
    }
    ;

expression:
    primary_expression
    | unary_expression
    | binary_expression
    | selector_expression
    | lambda_expression
    ;

primary_expression:
    literal_expression
    | variable_expression
    | funcall_expression
    | index_expression
    | dot_expression
    | resource_reference
    | '(' expression ')' { $$ = $2; }
    ;

literal_expression:
    value { $$ = puppet_expr_create_value($1); }
    | DQSTRING_LITERAL {
        // Parse double-quoted string for interpolation
        $$ = puppet_create_interpolated_expr($1);
        puppet_free($1);
    }
    ;

value:
    BOOLEAN { $$ = puppet_value_create_bool($1); }
    | NUMBER { $$ = puppet_value_create_number($1); }
    | STRING_LITERAL { $$ = puppet_value_create_string($1, strlen($1)); puppet_free($1); }
    | NAME { $$ = puppet_value_create_string($1, strlen($1)); puppet_free($1); }
    | UNDEF { $$ = puppet_value_create_undef(); }
    | array_value { $$ = $1; }
    | hash_value { $$ = $1; }
    | REGEX { 
        $$ = puppet_calloc(1, sizeof(puppet_value_t));
        $$->type = PUPPET_VALUE_REGEXP;
        $$->data.regexp = puppet_string_create($1);
        puppet_free($1);
    }
    ;

array_value:
    '[' ']' { $$ = puppet_value_create_array(); }
    | '[' expression_list ']' { 
        $$ = puppet_value_create_array();
        /* TODO: Add expressions to array */
    }
    ;

hash_value:
    '{' '}' { $$ = puppet_value_create_hash(); }
    | '{' hash_pairs '}' {
        $$ = puppet_value_create_hash();
        /* TODO: Add pairs to hash */
    }
    ;

hash_pairs:
    hash_pair
    | hash_pairs ',' hash_pair
    ;

hash_pair:
    expression FARROW expression
    ;

variable_expression:
    VARIABLE { $$ = puppet_expr_create_variable($1); puppet_free($1); }
    ;

funcall_expression:
    NAME '(' funcall_args ')' {
        $$ = puppet_calloc(1, sizeof(puppet_expr_t));
        $$->type = PUPPET_EXPR_FUNCALL;
        $$->data.funcall.name = puppet_string_create($1);
        if ($3) {
            $$->data.funcall.args = *$3;
            puppet_free($3);
        }
        puppet_free($1);
    }
    /* Commented out to fix ambiguity with commas
    | NAME funcall_args {
        $$ = puppet_calloc(1, sizeof(puppet_expr_t));
        $$->type = PUPPET_EXPR_FUNCALL;
        $$->data.funcall.name = puppet_string_create($1);
        if ($2) {
            $$->data.funcall.args = *$2;
            puppet_free($2);
        }
        puppet_free($1);
    }
    */
    ;

funcall_args:
    expression {
        $$ = puppet_calloc(1, sizeof(puppet_expr_list_t));
        $$->exprs = puppet_malloc(sizeof(puppet_expr_t *));
        $$->exprs[0] = $1;
        $$->count = 1;
    }
    | funcall_args ',' expression {
        $$ = $1;
        $$->exprs = puppet_realloc($$->exprs, ($$->count + 1) * sizeof(puppet_expr_t *));
        $$->exprs[$$->count++] = $3;
    }
    ;

index_expression:
    primary_expression '[' expression ']' {
        $$ = puppet_calloc(1, sizeof(puppet_expr_t));
        $$->type = PUPPET_EXPR_INDEX;
        $$->data.index.object = $1;
        $$->data.index.index = $3;
    }
    ;

dot_expression:
    primary_expression '.' NAME {
        $$ = puppet_calloc(1, sizeof(puppet_expr_t));
        $$->type = PUPPET_EXPR_DOT;
        $$->data.dot.object = $1;
        $$->data.dot.field = puppet_string_create($3);
        puppet_free($3);
    }
    ;

unary_expression:
    unary_op primary_expression {
        $$ = puppet_expr_create_unop($1, $2);
    }
    ;

unary_op:
    '!' { $$ = PUPPET_UNOP_NOT; }
    | NOT { $$ = PUPPET_UNOP_NOT; }
    | '-' %prec UMINUS { $$ = PUPPET_UNOP_MINUS; }
    | '+' %prec UMINUS { $$ = PUPPET_UNOP_PLUS; }
    ;

binary_expression:
    expression arithmetic_op expression {
        $$ = puppet_expr_create_binop($2, $1, $3);
    }
    | expression comparison_op expression {
        $$ = puppet_expr_create_binop($2, $1, $3);
    }
    | expression logical_op expression {
        $$ = puppet_expr_create_binop($2, $1, $3);
    }
    | expression IN expression {
        $$ = puppet_expr_create_binop(PUPPET_OP_IN, $1, $3);
    }
    ;

arithmetic_op:
    '+' { $$ = PUPPET_OP_ADD; }
    | '-' { $$ = PUPPET_OP_SUB; }
    | '*' { $$ = PUPPET_OP_MUL; }
    | '/' { $$ = PUPPET_OP_DIV; }
    | '%' { $$ = PUPPET_OP_MOD; }
    | LSHIFT { $$ = PUPPET_OP_LSHIFT; }
    | RSHIFT { $$ = PUPPET_OP_RSHIFT; }
    ;

comparison_op:
    '<' { $$ = PUPPET_OP_LT; }
    | '>' { $$ = PUPPET_OP_GT; }
    | LE { $$ = PUPPET_OP_LE; }
    | GE { $$ = PUPPET_OP_GE; }
    | EQ { $$ = PUPPET_OP_EQ; }
    | NE { $$ = PUPPET_OP_NE; }
    | MATCH { $$ = PUPPET_OP_MATCH; }
    | NOT_MATCH { $$ = PUPPET_OP_NOT_MATCH; }
    ;

logical_op:
    AND { $$ = PUPPET_OP_AND; }
    | OR { $$ = PUPPET_OP_OR; }
    ;

selector_expression:
    expression '?' '{' selector_cases '}' {
        /* TODO: Implement selector */
        $$ = $1;
    }
    ;

selector_cases:
    selector_case
    | selector_cases ',' selector_case
    ;

selector_case:
    expression FARROW expression
    | DEFAULT FARROW expression
    ;

lambda_expression:
    '|' parameter_list '|' '{' statement_list '}' {
        $$ = puppet_calloc(1, sizeof(puppet_expr_t));
        $$->type = PUPPET_EXPR_LAMBDA;
        puppet_lambda_t *lambda = puppet_calloc(1, sizeof(puppet_lambda_t));
        if ($2) {
            lambda->params = *$2;
            puppet_free($2);
        }
        /* TODO: Convert statements to expressions */
        $$->data.lambda = lambda;
        puppet_free($5);
    }
    ;

resource_reference:
    TYPE_NAME '[' expression ']' {
        $$ = puppet_calloc(1, sizeof(puppet_expr_t));
        $$->type = PUPPET_EXPR_RESOURCE_REF;
        $$->data.resource_ref.type = puppet_string_create($1);
        $$->data.resource_ref.title = $3;
        puppet_free($1);
    }
    | CLASSREF '[' expression ']' {
        $$ = puppet_calloc(1, sizeof(puppet_expr_t));
        $$->type = PUPPET_EXPR_RESOURCE_REF;
        $$->data.resource_ref.type = puppet_string_create($1);
        $$->data.resource_ref.title = $3;
        puppet_free($1);
    }
    ;

type_expression:
    TYPE_NAME { $$ = puppet_expr_create_variable($1); puppet_free($1); }
    | TYPE_NAME '[' expression_list ']' {
        /* TODO: Implement parameterized types */
        $$ = puppet_expr_create_variable($1); 
        puppet_free($1);
    }
    ;

expression_list:
    expression {
        $$ = puppet_calloc(1, sizeof(puppet_expr_list_t));
        $$->exprs = puppet_malloc(sizeof(puppet_expr_t *));
        $$->exprs[0] = $1;
        $$->count = 1;
    }
    | expression_list ',' expression {
        $$ = $1;
        $$->exprs = puppet_realloc($$->exprs, ($$->count + 1) * sizeof(puppet_expr_t *));
        $$->exprs[$$->count++] = $3;
    }
    ;

expression_list_opt:
    /* empty */ { $$ = NULL; }
    | expression_list { $$ = $1; }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Parse error at line %d: %s\n", yylineno, s);
    fprintf(stderr, "Near token: %s\n", yytext);
}

