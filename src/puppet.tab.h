/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_SRC_PUPPET_TAB_H_INCLUDED
# define YY_YY_SRC_PUPPET_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    NAME = 258,                    /* NAME  */
    CLASSREF = 259,                /* CLASSREF  */
    TYPE_NAME = 260,               /* TYPE_NAME  */
    VARIABLE = 261,                /* VARIABLE  */
    STRING_LITERAL = 262,          /* STRING_LITERAL  */
    DQSTRING_LITERAL = 263,        /* DQSTRING_LITERAL  */
    REGEX = 264,                   /* REGEX  */
    NUMBER = 265,                  /* NUMBER  */
    BOOLEAN = 266,                 /* BOOLEAN  */
    UNDEF = 267,                   /* UNDEF  */
    IF = 268,                      /* IF  */
    ELSIF = 269,                   /* ELSIF  */
    ELSE = 270,                    /* ELSE  */
    UNLESS = 271,                  /* UNLESS  */
    CASE = 272,                    /* CASE  */
    DEFAULT = 273,                 /* DEFAULT  */
    CLASS = 274,                   /* CLASS  */
    DEFINE = 275,                  /* DEFINE  */
    NODE = 276,                    /* NODE  */
    INHERITS = 277,                /* INHERITS  */
    INCLUDE = 278,                 /* INCLUDE  */
    REQUIRE_KEYWORD = 279,         /* REQUIRE_KEYWORD  */
    CONTAIN = 280,                 /* CONTAIN  */
    TAG = 281,                     /* TAG  */
    IMPORT = 282,                  /* IMPORT  */
    ATTR = 283,                    /* ATTR  */
    AUDIT = 284,                   /* AUDIT  */
    BEFORE_KEYWORD = 285,          /* BEFORE_KEYWORD  */
    NOOP = 286,                    /* NOOP  */
    NOTIFY_KEYWORD = 287,          /* NOTIFY_KEYWORD  */
    SCHEDULE = 288,                /* SCHEDULE  */
    STAGE = 289,                   /* STAGE  */
    SUBSCRIBE = 290,               /* SUBSCRIBE  */
    ARROW = 291,                   /* ARROW  */
    NOTIFY = 292,                  /* NOTIFY  */
    BEFORE = 293,                  /* BEFORE  */
    REQUIRE = 294,                 /* REQUIRE  */
    FARROW = 295,                  /* FARROW  */
    PARROW = 296,                  /* PARROW  */
    APPEND = 297,                  /* APPEND  */
    EQ = 298,                      /* EQ  */
    NE = 299,                      /* NE  */
    LE = 300,                      /* LE  */
    GE = 301,                      /* GE  */
    MATCH = 302,                   /* MATCH  */
    NOT_MATCH = 303,               /* NOT_MATCH  */
    IN = 304,                      /* IN  */
    LSHIFT = 305,                  /* LSHIFT  */
    RSHIFT = 306,                  /* RSHIFT  */
    AND = 307,                     /* AND  */
    OR = 308,                      /* OR  */
    NOT = 309,                     /* NOT  */
    AT2 = 310,                     /* AT2  */
    LCOLLECT = 311,                /* LCOLLECT  */
    RCOLLECT = 312,                /* RCOLLECT  */
    COLONCOLON = 313,              /* COLONCOLON  */
    DQSTRING_INTERP_START = 314,   /* DQSTRING_INTERP_START  */
    UMINUS = 315                   /* UMINUS  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 41 "src/puppet.y"

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
    puppet_resource_instance_t *resource_instance;
    puppet_resource_decl_t *resource_decl;
    puppet_case_when_t *case_when;
    puppet_if_branch_t *if_branch;
    puppet_binop_t binop;
    puppet_unop_t unop;

#line 144 "src/puppet.tab.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_SRC_PUPPET_TAB_H_INCLUDED  */
