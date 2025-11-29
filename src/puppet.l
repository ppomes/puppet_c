/**
 * @file puppet.l
 * @brief Flex lexer for the Puppet configuration language
 *
 * This file defines the lexical analyzer (scanner) for the Puppet
 * language using Flex. The lexer tokenizes Puppet source code into
 * tokens that the parser (puppet.y) can process.
 *
 * Token Categories:
 * - Keywords: class, define, if, unless, case, etc.
 * - Literals: strings, numbers, booleans, regex patterns
 * - Identifiers: variable names, class names, resource types
 * - Operators: arithmetic, comparison, logical operators
 * - Delimiters: braces, brackets, parentheses
 * - Comments: single-line (#) and multi-line (C)
 *
 * Special Features:
 * - String interpolation support with nested expressions
 * - Multiple string literal formats (single/double quoted)
 * - Regular expression literals with proper escaping
 * - Context-sensitive lexing for different string types
 * - Line number tracking for error reporting
 */

%{
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "puppet_ast.h"
#include "puppet.tab.h"

static char *string_buf;
static int string_buf_size;
static int string_buf_len;

static void string_buf_init(void);
static void string_buf_append(const char *str, int len);
static void string_buf_append_char(char c);
static char *string_buf_finish(void);

%}

%option noyywrap
%option yylineno
%x STRING DQSTRING COMMENT

DIGIT       [0-9]
HEXDIGIT    [0-9a-fA-F]
OCTALDIGIT  [0-7]
LETTER      [a-zA-Z]
LOWERCASE   [a-z]
UPPERCASE   [A-Z]
WORD        [a-zA-Z0-9_]
NON_WORD    [^a-zA-Z0-9_]

%%

"/*"                    { BEGIN(COMMENT); }
<COMMENT>[^*\n]*        { }
<COMMENT>"*"+[^*/\n]*   { }
<COMMENT>\n             { }
<COMMENT>"*"+"/"        { BEGIN(INITIAL); }

"#".*$                  { }

[ \t]+                  { }
\r                      { }
\n                      { }

"{"                     { return '{'; }
"}"                     { return '}'; }
"["                     { return '['; }
"]"                     { return ']'; }
"("                     { return '('; }
")"                     { return ')'; }
";"                     { return ';'; }
","                     { return ','; }
":"                     { return ':'; }
"."                     { return '.'; }

"+"                     { return '+'; }
"-"                     { return '-'; }
"*"                     { return '*'; }
"/"                     { return '/'; }
"%"                     { return '%'; }
"<<"                    { return LSHIFT; }
">>"                    { return RSHIFT; }

"="                     { return '='; }
"+="                    { return APPEND; }
"=="                    { return EQ; }
"!="                    { return NE; }
"<"                     { return '<'; }
"<="                    { return LE; }
">"                     { return '>'; }
">="                    { return GE; }
"=~"                    { return MATCH; }
"!~"                    { return NOT_MATCH; }
"in"                    { return IN; }

"and"                   { return AND; }
"or"                    { return OR; }
"!"                     { return '!'; }
"not"                   { return NOT; }

"->"                    { return ARROW; }
"~>"                    { return NOTIFY; }
"<-"                    { return BEFORE; }
"<~"                    { return REQUIRE; }

"=>"                    { return FARROW; }
"+>"                    { return PARROW; }
"*"                     { return '*'; }

"?"                     { return '?'; }
"|"                     { return '|'; }
"@"                     { return '@'; }
"@@"                    { return AT2; }
"<|"                    { return LCOLLECT; }
"|>"                    { return RCOLLECT; }

"$"{WORD}+              { yylval.string = strdup(yytext + 1); return VARIABLE; }
"$""::"?({WORD}+"::")*{WORD}+ { yylval.string = strdup(yytext + 1); return VARIABLE; }

"if"                    { return IF; }
"elsif"                 { return ELSIF; }
"else"                  { return ELSE; }
"unless"                { return UNLESS; }
"case"                  { return CASE; }
"default"               { return DEFAULT; }

"class"                 { return CLASS; }
"define"                { return DEFINE; }
"node"                  { return NODE; }
"inherits"              { return INHERITS; }

"include"               { return INCLUDE; }
"require"               { return REQUIRE_KEYWORD; }
"contain"               { return CONTAIN; }
"tag"                   { return TAG; }
"import"                { return IMPORT; }

"true"                  { yylval.boolean = true; return BOOLEAN; }
"false"                 { yylval.boolean = false; return BOOLEAN; }
"undef"                 { return UNDEF; }

"attr"                  { return ATTR; }
"audit"                 { return AUDIT; }
"before"                { return BEFORE_KEYWORD; }
"noop"                  { return NOOP; }
"notify"                { return NOTIFY_KEYWORD; }
"require"               { return REQUIRE_KEYWORD; }
"schedule"              { return SCHEDULE; }
"stage"                 { return STAGE; }
"subscribe"             { return SUBSCRIBE; }
"tag"                   { return TAG; }


{DIGIT}+                { yylval.number = atof(yytext); return NUMBER; }
{DIGIT}+"."{DIGIT}+     { yylval.number = atof(yytext); return NUMBER; }
{DIGIT}+[eE][+-]?{DIGIT}+ { yylval.number = atof(yytext); return NUMBER; }
{DIGIT}+"."{DIGIT}+[eE][+-]?{DIGIT}+ { yylval.number = atof(yytext); return NUMBER; }

"0x"{HEXDIGIT}+         { yylval.number = strtol(yytext + 2, NULL, 16); return NUMBER; }
"0"{OCTALDIGIT}+        { yylval.number = strtol(yytext + 1, NULL, 8); return NUMBER; }

{LOWERCASE}{WORD}*      { yylval.string = strdup(yytext); return NAME; }
{UPPERCASE}{WORD}*      { yylval.string = strdup(yytext); return CLASSREF; }
"::"                    { return COLONCOLON; }

\'                      { string_buf_init(); BEGIN(STRING); }
<STRING>\'              { 
    yylval.string = string_buf_finish(); 
    BEGIN(INITIAL); 
    return STRING_LITERAL; 
}
<STRING>\\\'            { string_buf_append_char('\''); }
<STRING>\\\\            { string_buf_append_char('\\'); }
<STRING>\\n             { string_buf_append_char('\n'); }
<STRING>\\r             { string_buf_append_char('\r'); }
<STRING>\\t             { string_buf_append_char('\t'); }
<STRING>[^\\\'\n]+      { string_buf_append(yytext, yyleng); }
<STRING>\n              { 
    fprintf(stderr, "Unterminated string\n"); 
    exit(1); 
}

\"                      { string_buf_init(); BEGIN(DQSTRING); }
<DQSTRING>\"            { 
    yylval.string = string_buf_finish(); 
    BEGIN(INITIAL); 
    return DQSTRING_LITERAL; 
}
<DQSTRING>\\\"          { string_buf_append_char('"'); }
<DQSTRING>\\\\          { string_buf_append_char('\\'); }
<DQSTRING>\\n           { string_buf_append_char('\n'); }
<DQSTRING>\\r           { string_buf_append_char('\r'); }
<DQSTRING>\\t           { string_buf_append_char('\t'); }
<DQSTRING>\\[0-7]{1,3} {
    int result;
    sscanf(yytext + 1, "%o", &result);
    string_buf_append_char((char)result);
}
<DQSTRING>\\x{HEXDIGIT}{1,2} {
    int result;
    sscanf(yytext + 2, "%x", &result);
    string_buf_append_char((char)result);
}
<DQSTRING>"$"           { string_buf_append("${", 2); return DQSTRING_INTERP_START; }
<DQSTRING>[^\\\"\n$]+   { string_buf_append(yytext, yyleng); }
<DQSTRING>\n            { 
    fprintf(stderr, "Unterminated string\n"); 
    exit(1); 
}

"/"[^/\n\* \t]([^/\n]*[^/\n\\])?"/"{NON_WORD} {
    yyless(yyleng - 1);
    yylval.string = strndup(yytext + 1, yyleng - 2);
    return REGEX;
}

.                       { fprintf(stderr, "Invalid character: %c\n", *yytext); }

%%

static void string_buf_init(void) {
    string_buf_size = 256;
    string_buf = malloc(string_buf_size);
    string_buf_len = 0;
}

static void string_buf_append(const char *str, int len) {
    if (string_buf_len + len + 1 > string_buf_size) {
        while (string_buf_len + len + 1 > string_buf_size)
            string_buf_size *= 2;
        string_buf = realloc(string_buf, string_buf_size);
    }
    memcpy(string_buf + string_buf_len, str, len);
    string_buf_len += len;
}

static void string_buf_append_char(char c) {
    string_buf_append(&c, 1);
}

static char *string_buf_finish(void) {
    string_buf[string_buf_len] = '\0';
    return string_buf;
}
