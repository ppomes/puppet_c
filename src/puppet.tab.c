/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

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

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 26 "src/puppet.y"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "puppet_ast.h"

extern int yylex(void);
extern int yylineno;
extern char *yytext;

void yyerror(const char *s);
puppet_program_t *parsed_program = NULL;


#line 86 "src/puppet.tab.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "puppet.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_NAME = 3,                       /* NAME  */
  YYSYMBOL_CLASSREF = 4,                   /* CLASSREF  */
  YYSYMBOL_TYPE_NAME = 5,                  /* TYPE_NAME  */
  YYSYMBOL_VARIABLE = 6,                   /* VARIABLE  */
  YYSYMBOL_STRING_LITERAL = 7,             /* STRING_LITERAL  */
  YYSYMBOL_DQSTRING_LITERAL = 8,           /* DQSTRING_LITERAL  */
  YYSYMBOL_REGEX = 9,                      /* REGEX  */
  YYSYMBOL_NUMBER = 10,                    /* NUMBER  */
  YYSYMBOL_BOOLEAN = 11,                   /* BOOLEAN  */
  YYSYMBOL_UNDEF = 12,                     /* UNDEF  */
  YYSYMBOL_IF = 13,                        /* IF  */
  YYSYMBOL_ELSIF = 14,                     /* ELSIF  */
  YYSYMBOL_ELSE = 15,                      /* ELSE  */
  YYSYMBOL_UNLESS = 16,                    /* UNLESS  */
  YYSYMBOL_CASE = 17,                      /* CASE  */
  YYSYMBOL_DEFAULT = 18,                   /* DEFAULT  */
  YYSYMBOL_CLASS = 19,                     /* CLASS  */
  YYSYMBOL_DEFINE = 20,                    /* DEFINE  */
  YYSYMBOL_NODE = 21,                      /* NODE  */
  YYSYMBOL_INHERITS = 22,                  /* INHERITS  */
  YYSYMBOL_INCLUDE = 23,                   /* INCLUDE  */
  YYSYMBOL_REQUIRE_KEYWORD = 24,           /* REQUIRE_KEYWORD  */
  YYSYMBOL_CONTAIN = 25,                   /* CONTAIN  */
  YYSYMBOL_TAG = 26,                       /* TAG  */
  YYSYMBOL_IMPORT = 27,                    /* IMPORT  */
  YYSYMBOL_ATTR = 28,                      /* ATTR  */
  YYSYMBOL_AUDIT = 29,                     /* AUDIT  */
  YYSYMBOL_BEFORE_KEYWORD = 30,            /* BEFORE_KEYWORD  */
  YYSYMBOL_NOOP = 31,                      /* NOOP  */
  YYSYMBOL_NOTIFY_KEYWORD = 32,            /* NOTIFY_KEYWORD  */
  YYSYMBOL_SCHEDULE = 33,                  /* SCHEDULE  */
  YYSYMBOL_STAGE = 34,                     /* STAGE  */
  YYSYMBOL_SUBSCRIBE = 35,                 /* SUBSCRIBE  */
  YYSYMBOL_ARROW = 36,                     /* ARROW  */
  YYSYMBOL_NOTIFY = 37,                    /* NOTIFY  */
  YYSYMBOL_BEFORE = 38,                    /* BEFORE  */
  YYSYMBOL_REQUIRE = 39,                   /* REQUIRE  */
  YYSYMBOL_FARROW = 40,                    /* FARROW  */
  YYSYMBOL_PARROW = 41,                    /* PARROW  */
  YYSYMBOL_APPEND = 42,                    /* APPEND  */
  YYSYMBOL_EQ = 43,                        /* EQ  */
  YYSYMBOL_NE = 44,                        /* NE  */
  YYSYMBOL_LE = 45,                        /* LE  */
  YYSYMBOL_GE = 46,                        /* GE  */
  YYSYMBOL_MATCH = 47,                     /* MATCH  */
  YYSYMBOL_NOT_MATCH = 48,                 /* NOT_MATCH  */
  YYSYMBOL_IN = 49,                        /* IN  */
  YYSYMBOL_LSHIFT = 50,                    /* LSHIFT  */
  YYSYMBOL_RSHIFT = 51,                    /* RSHIFT  */
  YYSYMBOL_AND = 52,                       /* AND  */
  YYSYMBOL_OR = 53,                        /* OR  */
  YYSYMBOL_NOT = 54,                       /* NOT  */
  YYSYMBOL_AT2 = 55,                       /* AT2  */
  YYSYMBOL_LCOLLECT = 56,                  /* LCOLLECT  */
  YYSYMBOL_RCOLLECT = 57,                  /* RCOLLECT  */
  YYSYMBOL_COLONCOLON = 58,                /* COLONCOLON  */
  YYSYMBOL_DQSTRING_INTERP_START = 59,     /* DQSTRING_INTERP_START  */
  YYSYMBOL_60_ = 60,                       /* '?'  */
  YYSYMBOL_61_ = 61,                       /* '<'  */
  YYSYMBOL_62_ = 62,                       /* '>'  */
  YYSYMBOL_63_ = 63,                       /* '+'  */
  YYSYMBOL_64_ = 64,                       /* '-'  */
  YYSYMBOL_65_ = 65,                       /* '*'  */
  YYSYMBOL_66_ = 66,                       /* '/'  */
  YYSYMBOL_67_ = 67,                       /* '%'  */
  YYSYMBOL_68_ = 68,                       /* '!'  */
  YYSYMBOL_UMINUS = 69,                    /* UMINUS  */
  YYSYMBOL_70_ = 70,                       /* '.'  */
  YYSYMBOL_71_ = 71,                       /* '['  */
  YYSYMBOL_72_ = 72,                       /* ']'  */
  YYSYMBOL_73_ = 73,                       /* '@'  */
  YYSYMBOL_74_ = 74,                       /* '{'  */
  YYSYMBOL_75_ = 75,                       /* '}'  */
  YYSYMBOL_76_ = 76,                       /* ','  */
  YYSYMBOL_77_ = 77,                       /* ':'  */
  YYSYMBOL_78_ = 78,                       /* '('  */
  YYSYMBOL_79_ = 79,                       /* ')'  */
  YYSYMBOL_80_ = 80,                       /* '='  */
  YYSYMBOL_81_ = 81,                       /* '|'  */
  YYSYMBOL_YYACCEPT = 82,                  /* $accept  */
  YYSYMBOL_program = 83,                   /* program  */
  YYSYMBOL_qualified_name = 84,            /* qualified_name  */
  YYSYMBOL_statement_list = 85,            /* statement_list  */
  YYSYMBOL_statement = 86,                 /* statement  */
  YYSYMBOL_resource_declaration = 87,      /* resource_declaration  */
  YYSYMBOL_resource_type = 88,             /* resource_type  */
  YYSYMBOL_resource_body = 89,             /* resource_body  */
  YYSYMBOL_resource_instance_list = 90,    /* resource_instance_list  */
  YYSYMBOL_resource_instance = 91,         /* resource_instance  */
  YYSYMBOL_attribute_list_opt = 92,        /* attribute_list_opt  */
  YYSYMBOL_attribute_list = 93,            /* attribute_list  */
  YYSYMBOL_attribute = 94,                 /* attribute  */
  YYSYMBOL_resource_default = 95,          /* resource_default  */
  YYSYMBOL_resource_override = 96,         /* resource_override  */
  YYSYMBOL_resource_collector = 97,        /* resource_collector  */
  YYSYMBOL_class_definition = 98,          /* class_definition  */
  YYSYMBOL_class_parent_opt = 99,          /* class_parent_opt  */
  YYSYMBOL_parameter_list_opt = 100,       /* parameter_list_opt  */
  YYSYMBOL_parameter_list = 101,           /* parameter_list  */
  YYSYMBOL_parameter = 102,                /* parameter  */
  YYSYMBOL_define_definition = 103,        /* define_definition  */
  YYSYMBOL_node_definition = 104,          /* node_definition  */
  YYSYMBOL_if_statement = 105,             /* if_statement  */
  YYSYMBOL_elsif_clauses = 106,            /* elsif_clauses  */
  YYSYMBOL_unless_statement = 107,         /* unless_statement  */
  YYSYMBOL_case_statement = 108,           /* case_statement  */
  YYSYMBOL_case_when_list = 109,           /* case_when_list  */
  YYSYMBOL_case_when = 110,                /* case_when  */
  YYSYMBOL_assignment_statement = 111,     /* assignment_statement  */
  YYSYMBOL_append_statement = 112,         /* append_statement  */
  YYSYMBOL_function_statement = 113,       /* function_statement  */
  YYSYMBOL_resource_chain = 114,           /* resource_chain  */
  YYSYMBOL_include_statement = 115,        /* include_statement  */
  YYSYMBOL_require_statement = 116,        /* require_statement  */
  YYSYMBOL_contain_statement = 117,        /* contain_statement  */
  YYSYMBOL_expression = 118,               /* expression  */
  YYSYMBOL_primary_expression = 119,       /* primary_expression  */
  YYSYMBOL_literal_expression = 120,       /* literal_expression  */
  YYSYMBOL_value = 121,                    /* value  */
  YYSYMBOL_array_value = 122,              /* array_value  */
  YYSYMBOL_hash_value = 123,               /* hash_value  */
  YYSYMBOL_hash_pairs = 124,               /* hash_pairs  */
  YYSYMBOL_hash_pair = 125,                /* hash_pair  */
  YYSYMBOL_variable_expression = 126,      /* variable_expression  */
  YYSYMBOL_funcall_expression = 127,       /* funcall_expression  */
  YYSYMBOL_funcall_args = 128,             /* funcall_args  */
  YYSYMBOL_index_expression = 129,         /* index_expression  */
  YYSYMBOL_dot_expression = 130,           /* dot_expression  */
  YYSYMBOL_unary_expression = 131,         /* unary_expression  */
  YYSYMBOL_unary_op = 132,                 /* unary_op  */
  YYSYMBOL_binary_expression = 133,        /* binary_expression  */
  YYSYMBOL_arithmetic_op = 134,            /* arithmetic_op  */
  YYSYMBOL_comparison_op = 135,            /* comparison_op  */
  YYSYMBOL_logical_op = 136,               /* logical_op  */
  YYSYMBOL_selector_expression = 137,      /* selector_expression  */
  YYSYMBOL_selector_cases = 138,           /* selector_cases  */
  YYSYMBOL_selector_case = 139,            /* selector_case  */
  YYSYMBOL_lambda_expression = 140,        /* lambda_expression  */
  YYSYMBOL_resource_reference = 141,       /* resource_reference  */
  YYSYMBOL_type_expression = 142,          /* type_expression  */
  YYSYMBOL_expression_list = 143           /* expression_list  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int16 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  3
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   1304

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  82
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  62
/* YYNRULES -- Number of rules.  */
#define YYNRULES  156
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  291

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   315


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    68,     2,     2,     2,    67,     2,     2,
      78,    79,    65,    63,    76,    64,    70,    66,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    77,     2,
      61,    80,    62,    60,    73,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    71,     2,    72,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    74,    81,    75,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    69
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,   135,   135,   143,   144,   157,   160,   173,   174,   175,
     176,   177,   178,   179,   180,   181,   182,   183,   184,   185,
     186,   187,   188,   189,   193,   199,   206,   216,   217,   218,
     222,   229,   238,   241,   247,   262,   265,   271,   278,   285,
     291,   297,   302,   307,   315,   324,   332,   340,   351,   371,
     372,   376,   377,   378,   379,   383,   390,   400,   407,   414,
     422,   433,   448,   456,   464,   474,   485,   499,   500,   519,
     529,   538,   539,   543,   547,   554,   561,   571,   581,   594,
     601,   611,   624,   637,   650,   651,   652,   653,   654,   658,
     659,   660,   661,   662,   663,   664,   668,   672,   673,   674,
     675,   676,   677,   678,   679,   680,   689,   690,   697,   698,
     705,   706,   710,   714,   718,   743,   749,   757,   766,   776,
     782,   783,   784,   785,   789,   792,   795,   798,   804,   805,
     806,   807,   808,   809,   810,   814,   815,   816,   817,   818,
     819,   820,   821,   825,   826,   830,   837,   838,   842,   843,
     847,   862,   869,   879,   880,   888,   894
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "NAME", "CLASSREF",
  "TYPE_NAME", "VARIABLE", "STRING_LITERAL", "DQSTRING_LITERAL", "REGEX",
  "NUMBER", "BOOLEAN", "UNDEF", "IF", "ELSIF", "ELSE", "UNLESS", "CASE",
  "DEFAULT", "CLASS", "DEFINE", "NODE", "INHERITS", "INCLUDE",
  "REQUIRE_KEYWORD", "CONTAIN", "TAG", "IMPORT", "ATTR", "AUDIT",
  "BEFORE_KEYWORD", "NOOP", "NOTIFY_KEYWORD", "SCHEDULE", "STAGE",
  "SUBSCRIBE", "ARROW", "NOTIFY", "BEFORE", "REQUIRE", "FARROW", "PARROW",
  "APPEND", "EQ", "NE", "LE", "GE", "MATCH", "NOT_MATCH", "IN", "LSHIFT",
  "RSHIFT", "AND", "OR", "NOT", "AT2", "LCOLLECT", "RCOLLECT",
  "COLONCOLON", "DQSTRING_INTERP_START", "'?'", "'<'", "'>'", "'+'", "'-'",
  "'*'", "'/'", "'%'", "'!'", "UMINUS", "'.'", "'['", "']'", "'@'", "'{'",
  "'}'", "','", "':'", "'('", "')'", "'='", "'|'", "$accept", "program",
  "qualified_name", "statement_list", "statement", "resource_declaration",
  "resource_type", "resource_body", "resource_instance_list",
  "resource_instance", "attribute_list_opt", "attribute_list", "attribute",
  "resource_default", "resource_override", "resource_collector",
  "class_definition", "class_parent_opt", "parameter_list_opt",
  "parameter_list", "parameter", "define_definition", "node_definition",
  "if_statement", "elsif_clauses", "unless_statement", "case_statement",
  "case_when_list", "case_when", "assignment_statement",
  "append_statement", "function_statement", "resource_chain",
  "include_statement", "require_statement", "contain_statement",
  "expression", "primary_expression", "literal_expression", "value",
  "array_value", "hash_value", "hash_pairs", "hash_pair",
  "variable_expression", "funcall_expression", "funcall_args",
  "index_expression", "dot_expression", "unary_expression", "unary_op",
  "binary_expression", "arithmetic_op", "comparison_op", "logical_op",
  "selector_expression", "selector_cases", "selector_case",
  "lambda_expression", "resource_reference", "type_expression",
  "expression_list", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-191)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -191,    13,   873,  -191,   -59,   -29,    -5,   -34,   442,   442,
     442,    60,    67,    43,    67,    67,    67,     3,     3,    90,
      -7,  -191,    32,  -191,  -191,  -191,  -191,  -191,  -191,  -191,
    -191,  -191,  -191,  -191,  -191,  -191,  -191,  -191,  -191,    35,
      29,   442,   154,   442,    25,   442,   442,    40,   -29,    78,
    -191,  -191,  -191,  -191,  -191,  -191,  -191,  -191,  -191,  -191,
    -191,   236,   315,   442,    84,   941,    46,  -191,  -191,  -191,
    -191,  -191,  -191,  -191,  -191,  -191,   453,  -191,  -191,  -191,
    -191,   973,  1005,    73,  -191,   -53,    93,    98,    99,    90,
      90,    90,  -191,  -191,    32,    32,   171,   873,   873,   331,
    -191,    25,  -191,  1237,   -17,  1069,  -191,  1212,  1099,   135,
     137,   138,   139,   -22,  -191,  1237,  1237,   442,  -191,   -49,
    -191,  1159,    49,  -191,   869,   109,   105,   -50,  -191,   180,
    -191,  -191,  -191,  -191,  -191,  -191,   442,  -191,  -191,  -191,
    -191,   113,  -191,  -191,  -191,  -191,  -191,  -191,  -191,  -191,
     442,   442,   442,   185,   442,    46,  -191,   347,    12,   167,
     117,  -191,  -191,  -191,  -191,  -191,  -191,    -7,    -7,  -191,
      51,  -191,   906,    53,   442,  -191,  -191,  -191,  -191,   442,
     442,   442,   442,  -191,    25,  1237,    -8,  -191,   442,  -191,
     442,  -191,   442,   442,    84,   119,   114,    48,   426,   529,
    1237,  1237,  1237,  -191,  1129,   552,   121,    69,  -191,    54,
    -191,    26,   193,   125,  -191,   575,   615,   638,  -191,   442,
      25,  -191,  1237,  1237,  1237,  1237,  1237,  -191,   442,  -191,
    1237,  -191,   -16,  1237,  -191,  -191,   442,   161,  1187,    59,
    -191,  -191,  -191,  -191,   130,  -191,  -191,   131,    15,  -191,
    -191,  -191,   661,  -191,  -191,  -191,  -191,  -191,   126,  1237,
    -191,   701,  1237,   442,   442,  -191,   426,   127,  -191,  -191,
    -191,   724,  -191,  -191,  1237,  1237,  -191,   442,   132,   747,
     787,  -191,  1037,  -191,  -191,  -191,  -191,   810,   833,  -191,
    -191
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       5,     0,     2,     1,     3,    28,     0,     0,     0,     0,
       0,    29,     0,     0,     0,     0,     0,     0,     0,    27,
       6,     7,     0,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,     0,
       0,     0,     0,     0,     0,     0,     0,   101,     0,     0,
     113,    99,   100,   105,    98,    97,   102,   121,   123,   122,
     120,     0,     0,     0,     0,     0,    84,    89,    96,   103,
     104,    90,    91,    92,    93,    85,     0,    86,    87,    88,
      94,     0,     0,    51,     3,    51,     0,     0,     0,    81,
      82,    83,    28,    29,     0,     0,     0,     0,     0,     0,
      24,     0,    77,   155,     0,     0,    47,     0,     0,     0,
       0,     0,     0,     0,    37,    76,    75,     0,   106,     0,
     108,     0,     0,   110,     0,   153,    57,     0,    55,     0,
     139,   140,   137,   138,   141,   142,     0,   133,   134,   143,
     144,     0,   135,   136,   128,   129,   130,   131,   132,     5,
       0,     0,     0,     0,     0,   119,     5,     0,     0,    49,
       0,     5,     5,     5,    26,    25,     4,    79,    80,    31,
       0,    32,     0,     0,     0,    78,   152,    46,   151,     0,
       0,     0,     0,    44,    39,   115,     0,   107,     0,   109,
       0,    95,     0,     0,     0,     0,    59,   127,     0,     0,
     124,   125,   126,   118,     0,     0,     0,     0,    71,     0,
      52,     0,     0,     0,     5,     0,     0,     0,    30,     0,
      35,    45,   156,    40,    41,    43,    42,    38,     0,   114,
     112,   111,     0,    58,    56,     5,     0,     0,     0,     0,
     146,    67,   117,    69,     0,    70,    72,     0,     0,    53,
      50,     5,     0,    62,    63,    64,    33,    34,    36,   116,
     154,     0,    60,     0,     0,   145,     0,    65,     5,     5,
      54,     0,    61,   150,   149,   148,   147,     0,     0,     0,
       0,    48,     0,     5,    74,    73,     5,     0,     0,    66,
      68
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -191,  -191,    70,  -147,    71,  -191,   128,    76,  -191,   -12,
    -191,  -100,    28,  -191,  -191,  -191,  -191,  -191,   124,    52,
    -190,  -191,  -191,  -191,  -191,  -191,  -191,  -191,     9,  -191,
    -191,  -191,  -191,  -191,  -191,  -191,     2,   143,  -191,  -191,
    -191,  -191,  -191,    30,  -191,  -191,  -191,  -191,  -191,  -191,
    -191,  -191,  -191,  -191,  -191,  -191,  -191,   -43,  -191,    -2,
    -191,   -37
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
       0,     1,    19,     2,    20,    21,    22,   100,   170,   171,
     257,   113,   114,    23,    24,    25,    26,   213,   159,   127,
     128,    27,    28,    29,   267,    30,    31,   207,   208,    32,
      33,    34,    35,    36,    37,    38,   103,    66,    67,    68,
      69,    70,   122,   123,    71,    72,   186,    73,    74,    75,
      76,    77,   150,   151,   152,    78,   239,   240,    79,    80,
     129,   209
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      39,   173,   199,   104,   234,    96,    84,    92,    45,   205,
      65,    81,    82,     3,   215,   216,   217,   125,   126,    40,
     125,   126,    93,   187,   119,   158,   194,   174,   109,    97,
      98,   195,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    41,   105,   107,   108,    46,   115,   116,   110,
      86,    42,    87,   183,   184,   111,   260,   112,   234,   174,
     174,    88,   175,    83,   121,   124,    43,   252,   228,    44,
      84,   229,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    85,    57,    89,    90,    91,   206,   261,   125,
     126,   210,    58,    59,   270,    39,    39,    60,   137,   138,
      61,   172,   248,    62,   271,   249,    99,    63,   102,   101,
      64,   144,   145,   146,   147,   148,   153,   154,   117,   185,
     258,   279,   280,    57,   189,   190,   218,   219,   221,   184,
     174,   247,    58,    59,   265,   266,   287,    60,   197,   288,
      61,   277,   278,    62,   245,    94,    95,    63,    96,    43,
      64,   158,   200,   201,   202,   232,   204,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,   161,   167,   168,
     164,   165,   162,   163,   166,   179,   222,   180,   181,   182,
     192,   223,   224,   225,   226,   193,   196,   198,   203,   212,
     230,   214,   121,   235,   236,   233,   250,    39,   244,   251,
     238,   263,   184,    39,   268,   269,   283,   256,    57,   160,
     211,   106,   227,    39,    39,    39,   246,    58,    59,   155,
     231,   172,    60,   276,     0,    61,     0,     0,    62,     0,
     259,     0,    63,     0,     0,    64,     0,     0,   262,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,     0,
      39,     0,     0,     0,     0,     0,     0,     0,     0,    39,
       0,     0,     0,     0,     0,   274,   275,     0,   238,    39,
       0,     0,     0,     0,     0,     0,     0,    39,    39,   282,
       0,     0,     0,     0,     0,    39,    39,     0,     0,     0,
      57,     0,     0,     0,     0,     0,     0,     0,     0,    58,
      59,     0,     0,     0,    60,     0,     0,    61,   118,     0,
      62,     0,     0,     0,    63,     0,     0,    64,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,     0,     0,
       0,     0,     0,     0,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,     0,     0,     0,     0,     0,     0,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
       0,     0,     0,     0,     0,   206,     0,     0,     0,    57,
       0,     0,     0,     0,     0,     0,     0,     0,    58,    59,
       0,     0,     0,    60,     0,    57,    61,     0,     0,    62,
     120,     0,     0,    63,    58,    59,    64,     0,     0,    60,
       0,    57,    61,     0,     0,    62,   169,     0,     0,    63,
      58,    59,    64,     0,     0,    60,     0,     0,    61,     0,
       0,    62,     0,     0,     0,    63,     0,     0,    64,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,     0,
       0,     0,     0,     0,   237,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,     0,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      57,     0,     0,     0,     0,     0,     0,     0,     0,    58,
      59,     0,     0,     0,    60,     0,    57,    61,     0,     0,
      62,     0,     0,     0,    63,    58,    59,    64,     0,     0,
      60,     0,     0,    61,     0,     0,    62,     0,     0,     0,
      63,     0,     0,    64,    61,     0,     0,    62,     0,     0,
       0,    63,     4,     5,     6,     7,     0,     0,     0,     0,
       0,     0,     8,     0,     0,     9,    10,     0,    11,    12,
      13,     0,    14,    15,    16,     4,     5,     6,     7,     0,
       0,     0,     0,     0,     0,     8,     0,     0,     9,    10,
       0,    11,    12,    13,     0,    14,    15,    16,     4,     5,
       6,     7,     0,     0,    17,     0,     0,     0,     8,     0,
       0,     9,    10,     0,    11,    12,    13,     0,    14,    15,
      16,     0,    18,     0,   241,     0,     0,    17,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     4,     5,
       6,     7,     0,     0,     0,    18,     0,   243,     8,     0,
      17,     9,    10,     0,    11,    12,    13,     0,    14,    15,
      16,     4,     5,     6,     7,     0,     0,     0,    18,     0,
     253,     8,     0,     0,     9,    10,     0,    11,    12,    13,
       0,    14,    15,    16,     4,     5,     6,     7,     0,     0,
      17,     0,     0,     0,     8,     0,     0,     9,    10,     0,
      11,    12,    13,     0,    14,    15,    16,     0,    18,     0,
     254,     0,     0,    17,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     4,     5,     6,     7,     0,     0,
       0,    18,     0,   255,     8,     0,    17,     9,    10,     0,
      11,    12,    13,     0,    14,    15,    16,     4,     5,     6,
       7,     0,     0,     0,    18,     0,   272,     8,     0,     0,
       9,    10,     0,    11,    12,    13,     0,    14,    15,    16,
       4,     5,     6,     7,     0,     0,    17,     0,     0,     0,
       8,     0,     0,     9,    10,     0,    11,    12,    13,     0,
      14,    15,    16,     0,    18,     0,   273,     0,     0,    17,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       4,     5,     6,     7,     0,     0,     0,    18,     0,   281,
       8,     0,    17,     9,    10,     0,    11,    12,    13,     0,
      14,    15,    16,     4,     5,     6,     7,     0,     0,     0,
      18,     0,   284,     8,     0,     0,     9,    10,     0,    11,
      12,    13,     0,    14,    15,    16,     4,     5,     6,     7,
       0,     0,    17,     0,     0,     0,     8,     0,     0,     9,
      10,     0,    11,    12,    13,     0,    14,    15,    16,     0,
      18,     0,   285,     0,     0,    17,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     4,     5,     6,     7,
       0,     0,     0,    18,     0,   289,     8,     0,    17,     9,
      10,     0,    11,    12,    13,     0,    14,    15,    16,     0,
       0,     0,     0,     0,     0,     0,    18,     0,   290,     0,
       0,     0,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,     0,     0,     0,     0,     0,    17,   141,
     142,   143,   144,   145,   146,   147,   148,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    18,     0,   191,   130,
     131,   132,   133,   134,   135,   136,   137,   138,   139,   140,
       0,     0,     0,     0,     0,     0,   141,   142,   143,   144,
     145,   146,   147,   148,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   220,   130,   131,   132,   133,   134,   135,
     136,   137,   138,   139,   140,     0,     0,     0,     0,     0,
       0,   141,   142,   143,   144,   145,   146,   147,   148,     0,
       0,     0,     0,     0,     0,   149,   130,   131,   132,   133,
     134,   135,   136,   137,   138,   139,   140,     0,     0,     0,
       0,     0,     0,   141,   142,   143,   144,   145,   146,   147,
     148,     0,     0,     0,     0,     0,     0,   156,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,     0,
       0,     0,     0,     0,     0,   141,   142,   143,   144,   145,
     146,   147,   148,     0,     0,     0,     0,     0,     0,   157,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,     0,     0,     0,     0,     0,     0,   141,   142,   143,
     144,   145,   146,   147,   148,     0,     0,     0,     0,     0,
       0,   286,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,     0,     0,     0,     0,     0,     0,   141,
     142,   143,   144,   145,   146,   147,   148,     0,     0,     0,
       0,   176,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,     0,     0,     0,     0,     0,     0,   141,
     142,   143,   144,   145,   146,   147,   148,     0,     0,     0,
       0,   178,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,     0,     0,     0,     0,     0,     0,   141,
     142,   143,   144,   145,   146,   147,   148,     0,     0,   188,
       0,   242,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,     0,     0,     0,     0,     0,     0,   141,
     142,   143,   144,   145,   146,   147,   148,   264,     0,     0,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,     0,     0,     0,     0,     0,     0,   141,   142,   143,
     144,   145,   146,   147,   148,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,     0,     0,     0,   177,
       0,     0,   141,   142,   143,   144,   145,   146,   147,   148,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,     0,     0,     0,     0,     0,     0,   141,   142,   143,
     144,   145,   146,   147,   148
};

static const yytype_int16 yycheck[] =
{
       2,   101,   149,    40,   194,    58,     3,     4,    42,   156,
       8,     9,    10,     0,   161,   162,   163,     5,     6,    78,
       5,     6,    19,    72,    61,    78,    76,    76,     3,    36,
      37,    81,     3,     4,     5,     6,     7,     8,     9,    10,
      11,    12,    71,    41,    42,    43,    80,    45,    46,    24,
       7,    56,     9,    75,    76,    30,    72,    32,   248,    76,
      76,    18,    79,     3,    62,    63,    71,   214,    76,    74,
       3,    79,     3,     4,     5,     6,     7,     8,     9,    10,
      11,    12,    12,    54,    14,    15,    16,    18,   235,     5,
       6,    79,    63,    64,    79,    97,    98,    68,    50,    51,
      71,    99,    76,    74,   251,    79,    74,    78,    79,    74,
      81,    63,    64,    65,    66,    67,    70,    71,    78,   117,
     220,   268,   269,    54,    75,    76,    75,    76,    75,    76,
      76,    77,    63,    64,    75,    76,   283,    68,   136,   286,
      71,    14,    15,    74,    75,    17,    18,    78,    58,    71,
      81,    78,   150,   151,   152,   192,   154,     3,     4,     5,
       6,     7,     8,     9,    10,    11,    12,    74,    97,    98,
      94,    95,    74,    74,     3,    40,   174,    40,    40,    40,
      71,   179,   180,   181,   182,    80,     6,    74,     3,    22,
     188,    74,   190,    74,    80,   193,     3,   199,    77,    74,
     198,    40,    76,   205,    74,    74,    74,   219,    54,    85,
     158,    57,   184,   215,   216,   217,   207,    63,    64,    76,
     190,   219,    68,   266,    -1,    71,    -1,    -1,    74,    -1,
     228,    -1,    78,    -1,    -1,    81,    -1,    -1,   236,     3,
       4,     5,     6,     7,     8,     9,    10,    11,    12,    -1,
     252,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   261,
      -1,    -1,    -1,    -1,    -1,   263,   264,    -1,   266,   271,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   279,   280,   277,
      -1,    -1,    -1,    -1,    -1,   287,   288,    -1,    -1,    -1,
      54,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    63,
      64,    -1,    -1,    -1,    68,    -1,    -1,    71,    72,    -1,
      74,    -1,    -1,    -1,    78,    -1,    -1,    81,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    -1,    -1,
      -1,    -1,    -1,    -1,     3,     4,     5,     6,     7,     8,
       9,    10,    11,    12,    -1,    -1,    -1,    -1,    -1,    -1,
       3,     4,     5,     6,     7,     8,     9,    10,    11,    12,
      -1,    -1,    -1,    -1,    -1,    18,    -1,    -1,    -1,    54,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    63,    64,
      -1,    -1,    -1,    68,    -1,    54,    71,    -1,    -1,    74,
      75,    -1,    -1,    78,    63,    64,    81,    -1,    -1,    68,
      -1,    54,    71,    -1,    -1,    74,    75,    -1,    -1,    78,
      63,    64,    81,    -1,    -1,    68,    -1,    -1,    71,    -1,
      -1,    74,    -1,    -1,    -1,    78,    -1,    -1,    81,     3,
       4,     5,     6,     7,     8,     9,    10,    11,    12,    -1,
      -1,    -1,    -1,    -1,    18,     3,     4,     5,     6,     7,
       8,     9,    10,    11,    12,    -1,     3,     4,     5,     6,
       7,     8,     9,    10,    11,    12,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      54,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    63,
      64,    -1,    -1,    -1,    68,    -1,    54,    71,    -1,    -1,
      74,    -1,    -1,    -1,    78,    63,    64,    81,    -1,    -1,
      68,    -1,    -1,    71,    -1,    -1,    74,    -1,    -1,    -1,
      78,    -1,    -1,    81,    71,    -1,    -1,    74,    -1,    -1,
      -1,    78,     3,     4,     5,     6,    -1,    -1,    -1,    -1,
      -1,    -1,    13,    -1,    -1,    16,    17,    -1,    19,    20,
      21,    -1,    23,    24,    25,     3,     4,     5,     6,    -1,
      -1,    -1,    -1,    -1,    -1,    13,    -1,    -1,    16,    17,
      -1,    19,    20,    21,    -1,    23,    24,    25,     3,     4,
       5,     6,    -1,    -1,    55,    -1,    -1,    -1,    13,    -1,
      -1,    16,    17,    -1,    19,    20,    21,    -1,    23,    24,
      25,    -1,    73,    -1,    75,    -1,    -1,    55,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,     3,     4,
       5,     6,    -1,    -1,    -1,    73,    -1,    75,    13,    -1,
      55,    16,    17,    -1,    19,    20,    21,    -1,    23,    24,
      25,     3,     4,     5,     6,    -1,    -1,    -1,    73,    -1,
      75,    13,    -1,    -1,    16,    17,    -1,    19,    20,    21,
      -1,    23,    24,    25,     3,     4,     5,     6,    -1,    -1,
      55,    -1,    -1,    -1,    13,    -1,    -1,    16,    17,    -1,
      19,    20,    21,    -1,    23,    24,    25,    -1,    73,    -1,
      75,    -1,    -1,    55,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,     3,     4,     5,     6,    -1,    -1,
      -1,    73,    -1,    75,    13,    -1,    55,    16,    17,    -1,
      19,    20,    21,    -1,    23,    24,    25,     3,     4,     5,
       6,    -1,    -1,    -1,    73,    -1,    75,    13,    -1,    -1,
      16,    17,    -1,    19,    20,    21,    -1,    23,    24,    25,
       3,     4,     5,     6,    -1,    -1,    55,    -1,    -1,    -1,
      13,    -1,    -1,    16,    17,    -1,    19,    20,    21,    -1,
      23,    24,    25,    -1,    73,    -1,    75,    -1,    -1,    55,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
       3,     4,     5,     6,    -1,    -1,    -1,    73,    -1,    75,
      13,    -1,    55,    16,    17,    -1,    19,    20,    21,    -1,
      23,    24,    25,     3,     4,     5,     6,    -1,    -1,    -1,
      73,    -1,    75,    13,    -1,    -1,    16,    17,    -1,    19,
      20,    21,    -1,    23,    24,    25,     3,     4,     5,     6,
      -1,    -1,    55,    -1,    -1,    -1,    13,    -1,    -1,    16,
      17,    -1,    19,    20,    21,    -1,    23,    24,    25,    -1,
      73,    -1,    75,    -1,    -1,    55,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,     3,     4,     5,     6,
      -1,    -1,    -1,    73,    -1,    75,    13,    -1,    55,    16,
      17,    -1,    19,    20,    21,    -1,    23,    24,    25,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    73,    -1,    75,    -1,
      -1,    -1,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    -1,    -1,    -1,    -1,    -1,    55,    60,
      61,    62,    63,    64,    65,    66,    67,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    73,    -1,    79,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      -1,    -1,    -1,    -1,    -1,    -1,    60,    61,    62,    63,
      64,    65,    66,    67,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    77,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    -1,    -1,    -1,    -1,    -1,
      -1,    60,    61,    62,    63,    64,    65,    66,    67,    -1,
      -1,    -1,    -1,    -1,    -1,    74,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    -1,    -1,    -1,
      -1,    -1,    -1,    60,    61,    62,    63,    64,    65,    66,
      67,    -1,    -1,    -1,    -1,    -1,    -1,    74,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    -1,
      -1,    -1,    -1,    -1,    -1,    60,    61,    62,    63,    64,
      65,    66,    67,    -1,    -1,    -1,    -1,    -1,    -1,    74,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    -1,    -1,    -1,    -1,    -1,    -1,    60,    61,    62,
      63,    64,    65,    66,    67,    -1,    -1,    -1,    -1,    -1,
      -1,    74,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    -1,    -1,    -1,    -1,    -1,    -1,    60,
      61,    62,    63,    64,    65,    66,    67,    -1,    -1,    -1,
      -1,    72,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    -1,    -1,    -1,    -1,    -1,    -1,    60,
      61,    62,    63,    64,    65,    66,    67,    -1,    -1,    -1,
      -1,    72,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    -1,    -1,    -1,    -1,    -1,    -1,    60,
      61,    62,    63,    64,    65,    66,    67,    -1,    -1,    40,
      -1,    72,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    -1,    -1,    -1,    -1,    -1,    -1,    60,
      61,    62,    63,    64,    65,    66,    67,    40,    -1,    -1,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    -1,    -1,    -1,    -1,    -1,    -1,    60,    61,    62,
      63,    64,    65,    66,    67,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    -1,    -1,    -1,    57,
      -1,    -1,    60,    61,    62,    63,    64,    65,    66,    67,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    -1,    -1,    -1,    -1,    -1,    -1,    60,    61,    62,
      63,    64,    65,    66,    67
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    83,    85,     0,     3,     4,     5,     6,    13,    16,
      17,    19,    20,    21,    23,    24,    25,    55,    73,    84,
      86,    87,    88,    95,    96,    97,    98,   103,   104,   105,
     107,   108,   111,   112,   113,   114,   115,   116,   117,   141,
      78,    71,    56,    71,    74,    42,    80,     3,     4,     5,
       6,     7,     8,     9,    10,    11,    12,    54,    63,    64,
      68,    71,    74,    78,    81,   118,   119,   120,   121,   122,
     123,   126,   127,   129,   130,   131,   132,   133,   137,   140,
     141,   118,   118,     3,     3,    84,     7,     9,    18,    84,
      84,    84,     4,    19,    88,    88,    58,    36,    37,    74,
      89,    74,    79,   118,   143,   118,    57,   118,   118,     3,
      24,    30,    32,    93,    94,   118,   118,    78,    72,   143,
      75,   118,   124,   125,   118,     5,     6,   101,   102,   142,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    60,    61,    62,    63,    64,    65,    66,    67,    74,
     134,   135,   136,    70,    71,   119,    74,    74,    78,   100,
     100,    74,    74,    74,    89,    89,     3,    86,    86,    75,
      90,    91,   118,    93,    76,    79,    72,    57,    72,    40,
      40,    40,    40,    75,    76,   118,   128,    72,    40,    75,
      76,    79,    71,    80,    76,    81,     6,   118,    74,    85,
     118,   118,   118,     3,   118,    85,    18,   109,   110,   143,
      79,   101,    22,    99,    74,    85,    85,    85,    75,    76,
      77,    75,   118,   118,   118,   118,   118,    94,    76,    79,
     118,   125,   143,   118,   102,    74,    80,    18,   118,   138,
     139,    75,    72,    75,    77,    75,   110,    77,    76,    79,
       3,    74,    85,    75,    75,    75,    91,    92,    93,   118,
      72,    85,   118,    40,    40,    75,    76,   106,    74,    74,
      79,    85,    75,    75,   118,   118,   139,    14,    15,    85,
      85,    75,   118,    74,    75,    75,    74,    85,    85,    75,
      75
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_uint8 yyr1[] =
{
       0,    82,    83,    84,    84,    85,    85,    86,    86,    86,
      86,    86,    86,    86,    86,    86,    86,    86,    86,    86,
      86,    86,    86,    86,    87,    87,    87,    88,    88,    88,
      89,    89,    90,    90,    91,    92,    92,    93,    93,    93,
      94,    94,    94,    94,    95,    96,    97,    97,    98,    99,
      99,   100,   100,   100,   100,   101,   101,   102,   102,   102,
     102,   103,   104,   104,   104,   105,   105,   106,   106,   107,
     108,   109,   109,   110,   110,   111,   112,   113,   113,   114,
     114,   115,   116,   117,   118,   118,   118,   118,   118,   119,
     119,   119,   119,   119,   119,   119,   120,   121,   121,   121,
     121,   121,   121,   121,   121,   121,   122,   122,   123,   123,
     124,   124,   125,   126,   127,   128,   128,   129,   130,   131,
     132,   132,   132,   132,   133,   133,   133,   133,   134,   134,
     134,   134,   134,   134,   134,   135,   135,   135,   135,   135,
     135,   135,   135,   136,   136,   137,   138,   138,   139,   139,
     140,   141,   141,   142,   142,   143,   143
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     1,     3,     0,     2,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     2,     3,     3,     1,     1,     1,
       3,     2,     1,     3,     3,     0,     1,     1,     3,     2,
       3,     3,     3,     3,     4,     4,     4,     3,     7,     0,
       2,     0,     2,     3,     4,     1,     3,     1,     3,     2,
       4,     6,     5,     5,     5,     6,    10,     0,     6,     5,
       5,     1,     2,     5,     5,     3,     3,     3,     4,     3,
       3,     2,     2,     2,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     3,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     2,     3,     2,     3,
       1,     3,     3,     1,     4,     1,     3,     4,     3,     2,
       1,     1,     1,     1,     3,     3,     3,     3,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     5,     1,     3,     3,     3,
       6,     4,     4,     1,     4,     1,     3
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* program: statement_list  */
#line 135 "src/puppet.y"
                   {
        parsed_program = calloc(1, sizeof(puppet_program_t));
        parsed_program->statements = *(yyvsp[0].stmt_list);
        free((yyvsp[0].stmt_list));
    }
#line 1644 "src/puppet.tab.c"
    break;

  case 3: /* qualified_name: NAME  */
#line 143 "src/puppet.y"
         { (yyval.string) = (yyvsp[0].string); }
#line 1650 "src/puppet.tab.c"
    break;

  case 4: /* qualified_name: qualified_name COLONCOLON NAME  */
#line 144 "src/puppet.y"
                                     {
        size_t len1 = strlen((yyvsp[-2].string));
        size_t len3 = strlen((yyvsp[0].string));
        (yyval.string) = malloc(len1 + 2 + len3 + 1);
        strcpy((yyval.string), (yyvsp[-2].string));
        strcat((yyval.string), "::");
        strcat((yyval.string), (yyvsp[0].string));
        free((yyvsp[-2].string));
        free((yyvsp[0].string));
    }
#line 1665 "src/puppet.tab.c"
    break;

  case 5: /* statement_list: %empty  */
#line 157 "src/puppet.y"
                {
        (yyval.stmt_list) = calloc(1, sizeof(puppet_stmt_list_t));
    }
#line 1673 "src/puppet.tab.c"
    break;

  case 6: /* statement_list: statement_list statement  */
#line 160 "src/puppet.y"
                               {
        (yyval.stmt_list) = (yyvsp[-1].stmt_list);
        (yyval.stmt_list)->stmts = realloc((yyval.stmt_list)->stmts, ((yyval.stmt_list)->count + 1) * sizeof(puppet_stmt_t *));
        (yyval.stmt_list)->stmts[(yyval.stmt_list)->count++] = (yyvsp[0].stmt);
    }
#line 1683 "src/puppet.tab.c"
    break;

  case 24: /* resource_declaration: resource_type resource_body  */
#line 193 "src/puppet.y"
                                {
        (yyval.stmt) = puppet_stmt_create_resource(*(yyvsp[0].resource_decl));
        (yyval.stmt)->data.resource.type = puppet_string_create((yyvsp[-1].string));
        free((yyvsp[-1].string));
        free((yyvsp[0].resource_decl));
    }
#line 1694 "src/puppet.tab.c"
    break;

  case 25: /* resource_declaration: '@' resource_type resource_body  */
#line 199 "src/puppet.y"
                                      {
        (yyval.stmt) = puppet_stmt_create_resource(*(yyvsp[0].resource_decl));
        (yyval.stmt)->data.resource.type = puppet_string_create((yyvsp[-1].string));
        (yyval.stmt)->data.resource.style = PUPPET_RES_VIRTUAL;
        free((yyvsp[-1].string));
        free((yyvsp[0].resource_decl));
    }
#line 1706 "src/puppet.tab.c"
    break;

  case 26: /* resource_declaration: AT2 resource_type resource_body  */
#line 206 "src/puppet.y"
                                      {
        (yyval.stmt) = puppet_stmt_create_resource(*(yyvsp[0].resource_decl));
        (yyval.stmt)->data.resource.type = puppet_string_create((yyvsp[-1].string));
        (yyval.stmt)->data.resource.style = PUPPET_RES_EXPORTED;
        free((yyvsp[-1].string));
        free((yyvsp[0].resource_decl));
    }
#line 1718 "src/puppet.tab.c"
    break;

  case 27: /* resource_type: qualified_name  */
#line 216 "src/puppet.y"
                   { (yyval.string) = (yyvsp[0].string); }
#line 1724 "src/puppet.tab.c"
    break;

  case 28: /* resource_type: CLASSREF  */
#line 217 "src/puppet.y"
               { (yyval.string) = (yyvsp[0].string); }
#line 1730 "src/puppet.tab.c"
    break;

  case 29: /* resource_type: CLASS  */
#line 218 "src/puppet.y"
            { (yyval.string) = strdup("class"); }
#line 1736 "src/puppet.tab.c"
    break;

  case 30: /* resource_body: '{' resource_instance_list '}'  */
#line 222 "src/puppet.y"
                                   {
        (yyval.resource_decl) = calloc(1, sizeof(puppet_resource_decl_t));
        (yyval.resource_decl)->style = PUPPET_RES_NORMAL;
        (yyval.resource_decl)->instance_count = 1;  // For now, assume single instance
        (yyval.resource_decl)->instances = calloc(1, sizeof(puppet_resource_instance_t));
        (yyval.resource_decl)->instances[0] = *(yyvsp[-1].resource_instance);  // Copy the first instance
    }
#line 1748 "src/puppet.tab.c"
    break;

  case 31: /* resource_body: '{' '}'  */
#line 229 "src/puppet.y"
              {
        (yyval.resource_decl) = calloc(1, sizeof(puppet_resource_decl_t));
        (yyval.resource_decl)->style = PUPPET_RES_NORMAL;
        (yyval.resource_decl)->instance_count = 0;
        (yyval.resource_decl)->instances = NULL;
    }
#line 1759 "src/puppet.tab.c"
    break;

  case 32: /* resource_instance_list: resource_instance  */
#line 238 "src/puppet.y"
                      {
        (yyval.resource_instance) = (yyvsp[0].resource_instance);
    }
#line 1767 "src/puppet.tab.c"
    break;

  case 33: /* resource_instance_list: resource_instance_list ',' resource_instance  */
#line 241 "src/puppet.y"
                                                   {
        (yyval.resource_instance) = (yyvsp[-2].resource_instance);  // For now, just return the first one
    }
#line 1775 "src/puppet.tab.c"
    break;

  case 34: /* resource_instance: expression ':' attribute_list_opt  */
#line 247 "src/puppet.y"
                                      {
        (yyval.resource_instance) = calloc(1, sizeof(puppet_resource_instance_t));
        (yyval.resource_instance)->title = (yyvsp[-2].expr);
        if ((yyvsp[0].attribute_list)) {
            (yyval.resource_instance)->attr_count = (yyvsp[0].attribute_list)->count;
            (yyval.resource_instance)->attributes = (yyvsp[0].attribute_list)->attributes;
            free((yyvsp[0].attribute_list));
        } else {
            (yyval.resource_instance)->attr_count = 0;
            (yyval.resource_instance)->attributes = NULL;
        }
    }
#line 1792 "src/puppet.tab.c"
    break;

  case 35: /* attribute_list_opt: %empty  */
#line 262 "src/puppet.y"
                {
        (yyval.attribute_list) = NULL;
    }
#line 1800 "src/puppet.tab.c"
    break;

  case 36: /* attribute_list_opt: attribute_list  */
#line 265 "src/puppet.y"
                     {
        (yyval.attribute_list) = (yyvsp[0].attribute_list);
    }
#line 1808 "src/puppet.tab.c"
    break;

  case 37: /* attribute_list: attribute  */
#line 271 "src/puppet.y"
              {
        (yyval.attribute_list) = calloc(1, sizeof(puppet_attribute_list_t));
        (yyval.attribute_list)->attributes = malloc(sizeof(puppet_attribute_t));
        (yyval.attribute_list)->attributes[0] = *(yyvsp[0].attribute);
        (yyval.attribute_list)->count = 1;
        free((yyvsp[0].attribute));
    }
#line 1820 "src/puppet.tab.c"
    break;

  case 38: /* attribute_list: attribute_list ',' attribute  */
#line 278 "src/puppet.y"
                                   {
        (yyval.attribute_list) = (yyvsp[-2].attribute_list);
        (yyval.attribute_list)->attributes = realloc((yyval.attribute_list)->attributes, ((yyval.attribute_list)->count + 1) * sizeof(puppet_attribute_t));
        (yyval.attribute_list)->attributes[(yyval.attribute_list)->count] = *(yyvsp[0].attribute);
        (yyval.attribute_list)->count++;
        free((yyvsp[0].attribute));
    }
#line 1832 "src/puppet.tab.c"
    break;

  case 39: /* attribute_list: attribute_list ','  */
#line 285 "src/puppet.y"
                         {
        (yyval.attribute_list) = (yyvsp[-1].attribute_list);
    }
#line 1840 "src/puppet.tab.c"
    break;

  case 40: /* attribute: NAME FARROW expression  */
#line 291 "src/puppet.y"
                           {
        (yyval.attribute) = calloc(1, sizeof(puppet_attribute_t));
        (yyval.attribute)->name = puppet_string_create((yyvsp[-2].string));
        (yyval.attribute)->value = (yyvsp[0].expr);
        free((yyvsp[-2].string));
    }
#line 1851 "src/puppet.tab.c"
    break;

  case 41: /* attribute: REQUIRE_KEYWORD FARROW expression  */
#line 297 "src/puppet.y"
                                        {
        (yyval.attribute) = calloc(1, sizeof(puppet_attribute_t));
        (yyval.attribute)->name = puppet_string_create("require");
        (yyval.attribute)->value = (yyvsp[0].expr);
    }
#line 1861 "src/puppet.tab.c"
    break;

  case 42: /* attribute: NOTIFY_KEYWORD FARROW expression  */
#line 302 "src/puppet.y"
                                       {
        (yyval.attribute) = calloc(1, sizeof(puppet_attribute_t));
        (yyval.attribute)->name = puppet_string_create("notify");
        (yyval.attribute)->value = (yyvsp[0].expr);
    }
#line 1871 "src/puppet.tab.c"
    break;

  case 43: /* attribute: BEFORE_KEYWORD FARROW expression  */
#line 307 "src/puppet.y"
                                       {
        (yyval.attribute) = calloc(1, sizeof(puppet_attribute_t));
        (yyval.attribute)->name = puppet_string_create("before");
        (yyval.attribute)->value = (yyvsp[0].expr);
    }
#line 1881 "src/puppet.tab.c"
    break;

  case 44: /* resource_default: TYPE_NAME '{' attribute_list '}'  */
#line 315 "src/puppet.y"
                                     {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_RESOURCE_DEFAULT;
        (yyval.stmt)->data.resource_default.type = puppet_string_create((yyvsp[-3].string));
        free((yyvsp[-3].string));
    }
#line 1892 "src/puppet.tab.c"
    break;

  case 45: /* resource_override: resource_reference '{' attribute_list '}'  */
#line 324 "src/puppet.y"
                                              {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_RESOURCE_OVERRIDE;
        (yyval.stmt)->data.resource_override.reference = (yyvsp[-3].expr);
    }
#line 1902 "src/puppet.tab.c"
    break;

  case 46: /* resource_collector: TYPE_NAME LCOLLECT expression RCOLLECT  */
#line 332 "src/puppet.y"
                                           {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_RESOURCE_COLLECTOR;
        (yyval.stmt)->data.collector.style = PUPPET_RES_NORMAL;
        (yyval.stmt)->data.collector.type = puppet_string_create((yyvsp[-3].string));
        (yyval.stmt)->data.collector.search_expr = (yyvsp[-1].expr);
        free((yyvsp[-3].string));
    }
#line 1915 "src/puppet.tab.c"
    break;

  case 47: /* resource_collector: TYPE_NAME LCOLLECT RCOLLECT  */
#line 340 "src/puppet.y"
                                  {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_RESOURCE_COLLECTOR;
        (yyval.stmt)->data.collector.style = PUPPET_RES_NORMAL;
        (yyval.stmt)->data.collector.type = puppet_string_create((yyvsp[-2].string));
        (yyval.stmt)->data.collector.search_expr = NULL;
        free((yyvsp[-2].string));
    }
#line 1928 "src/puppet.tab.c"
    break;

  case 48: /* class_definition: CLASS NAME parameter_list_opt class_parent_opt '{' statement_list '}'  */
#line 351 "src/puppet.y"
                                                                          {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_CLASS_DEF;
        (yyval.stmt)->data.class_def.name = puppet_string_create((yyvsp[-5].string));
        if ((yyvsp[-4].param_list)) {
            (yyval.stmt)->data.class_def.params = *(yyvsp[-4].param_list);
            free((yyvsp[-4].param_list));
        }
        if ((yyvsp[-3].string)) {
            (yyval.stmt)->data.class_def.inherits = calloc(1, sizeof(puppet_string_t));
            *(yyval.stmt)->data.class_def.inherits = puppet_string_create((yyvsp[-3].string));
            free((yyvsp[-3].string));
        }
        (yyval.stmt)->data.class_def.body = *(yyvsp[-1].stmt_list);
        free((yyvsp[-5].string));
        free((yyvsp[-1].stmt_list));
    }
#line 1950 "src/puppet.tab.c"
    break;

  case 49: /* class_parent_opt: %empty  */
#line 371 "src/puppet.y"
                { (yyval.string) = NULL; }
#line 1956 "src/puppet.tab.c"
    break;

  case 50: /* class_parent_opt: INHERITS NAME  */
#line 372 "src/puppet.y"
                    { (yyval.string) = (yyvsp[0].string); }
#line 1962 "src/puppet.tab.c"
    break;

  case 51: /* parameter_list_opt: %empty  */
#line 376 "src/puppet.y"
                { (yyval.param_list) = NULL; }
#line 1968 "src/puppet.tab.c"
    break;

  case 52: /* parameter_list_opt: '(' ')'  */
#line 377 "src/puppet.y"
              { (yyval.param_list) = calloc(1, sizeof(puppet_param_list_t)); }
#line 1974 "src/puppet.tab.c"
    break;

  case 53: /* parameter_list_opt: '(' parameter_list ')'  */
#line 378 "src/puppet.y"
                             { (yyval.param_list) = (yyvsp[-1].param_list); }
#line 1980 "src/puppet.tab.c"
    break;

  case 54: /* parameter_list_opt: '(' parameter_list ',' ')'  */
#line 379 "src/puppet.y"
                                 { (yyval.param_list) = (yyvsp[-2].param_list); }
#line 1986 "src/puppet.tab.c"
    break;

  case 55: /* parameter_list: parameter  */
#line 383 "src/puppet.y"
              {
        (yyval.param_list) = calloc(1, sizeof(puppet_param_list_t));
        (yyval.param_list)->params = calloc(1, sizeof(puppet_param_t));
        (yyval.param_list)->params[0] = *(yyvsp[0].param);
        (yyval.param_list)->count = 1;
        free((yyvsp[0].param));
    }
#line 1998 "src/puppet.tab.c"
    break;

  case 56: /* parameter_list: parameter_list ',' parameter  */
#line 390 "src/puppet.y"
                                   {
        (yyval.param_list) = (yyvsp[-2].param_list);
        (yyval.param_list)->params = realloc((yyval.param_list)->params, ((yyval.param_list)->count + 1) * sizeof(puppet_param_t));
        (yyval.param_list)->params[(yyval.param_list)->count] = *(yyvsp[0].param);
        (yyval.param_list)->count++;
        free((yyvsp[0].param));
    }
#line 2010 "src/puppet.tab.c"
    break;

  case 57: /* parameter: VARIABLE  */
#line 400 "src/puppet.y"
             {
        (yyval.param) = calloc(1, sizeof(puppet_param_t));
        (yyval.param)->name = puppet_string_create((yyvsp[0].string));
        (yyval.param)->type_constraint = NULL;
        (yyval.param)->default_value = NULL;
        free((yyvsp[0].string));
    }
#line 2022 "src/puppet.tab.c"
    break;

  case 58: /* parameter: VARIABLE '=' expression  */
#line 407 "src/puppet.y"
                              {
        (yyval.param) = calloc(1, sizeof(puppet_param_t));
        (yyval.param)->name = puppet_string_create((yyvsp[-2].string));
        (yyval.param)->type_constraint = NULL;
        (yyval.param)->default_value = (yyvsp[0].expr);
        free((yyvsp[-2].string));
    }
#line 2034 "src/puppet.tab.c"
    break;

  case 59: /* parameter: type_expression VARIABLE  */
#line 414 "src/puppet.y"
                               {
        (yyval.param) = calloc(1, sizeof(puppet_param_t));
        (yyval.param)->name = puppet_string_create((yyvsp[0].string));
        (yyval.param)->type_constraint = puppet_value_create_string((yyvsp[-1].expr)->data.variable.data, (yyvsp[-1].expr)->data.variable.len);
        (yyval.param)->default_value = NULL;
        puppet_expr_destroy((yyvsp[-1].expr));
        free((yyvsp[0].string));
    }
#line 2047 "src/puppet.tab.c"
    break;

  case 60: /* parameter: type_expression VARIABLE '=' expression  */
#line 422 "src/puppet.y"
                                              {
        (yyval.param) = calloc(1, sizeof(puppet_param_t));
        (yyval.param)->name = puppet_string_create((yyvsp[-2].string));
        (yyval.param)->type_constraint = puppet_value_create_string((yyvsp[-3].expr)->data.variable.data, (yyvsp[-3].expr)->data.variable.len);
        (yyval.param)->default_value = (yyvsp[0].expr);
        puppet_expr_destroy((yyvsp[-3].expr));
        free((yyvsp[-2].string));
    }
#line 2060 "src/puppet.tab.c"
    break;

  case 61: /* define_definition: DEFINE qualified_name parameter_list_opt '{' statement_list '}'  */
#line 433 "src/puppet.y"
                                                                    {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_DEFINE;
        (yyval.stmt)->data.define.name = puppet_string_create((yyvsp[-4].string));
        if ((yyvsp[-3].param_list)) {
            (yyval.stmt)->data.define.params = *(yyvsp[-3].param_list);
            free((yyvsp[-3].param_list));
        }
        (yyval.stmt)->data.define.body = *(yyvsp[-1].stmt_list);
        free((yyvsp[-4].string));
        free((yyvsp[-1].stmt_list));
    }
#line 2077 "src/puppet.tab.c"
    break;

  case 62: /* node_definition: NODE STRING_LITERAL '{' statement_list '}'  */
#line 448 "src/puppet.y"
                                               {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_NODE;
        (yyval.stmt)->data.node.name = puppet_string_create((yyvsp[-3].string));
        (yyval.stmt)->data.node.body = *(yyvsp[-1].stmt_list);
        free((yyvsp[-3].string));
        free((yyvsp[-1].stmt_list));
    }
#line 2090 "src/puppet.tab.c"
    break;

  case 63: /* node_definition: NODE REGEX '{' statement_list '}'  */
#line 456 "src/puppet.y"
                                        {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_NODE;
        (yyval.stmt)->data.node.name = puppet_string_create((yyvsp[-3].string));
        (yyval.stmt)->data.node.body = *(yyvsp[-1].stmt_list);
        free((yyvsp[-3].string));
        free((yyvsp[-1].stmt_list));
    }
#line 2103 "src/puppet.tab.c"
    break;

  case 64: /* node_definition: NODE DEFAULT '{' statement_list '}'  */
#line 464 "src/puppet.y"
                                          {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_NODE;
        (yyval.stmt)->data.node.name = puppet_string_create("default");
        (yyval.stmt)->data.node.body = *(yyvsp[-1].stmt_list);
        free((yyvsp[-1].stmt_list));
    }
#line 2115 "src/puppet.tab.c"
    break;

  case 65: /* if_statement: IF expression '{' statement_list '}' elsif_clauses  */
#line 474 "src/puppet.y"
                                                       {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_IF;
        puppet_if_branch_t *first = calloc(1, sizeof(puppet_if_branch_t));
        first->condition = (yyvsp[-4].expr);
        first->body = *(yyvsp[-2].stmt_list);
        first->next = (yyvsp[0].if_branch);
        (yyval.stmt)->data.if_stmt.branches = first;
        (yyval.stmt)->data.if_stmt.else_body = NULL;
        free((yyvsp[-2].stmt_list));
    }
#line 2131 "src/puppet.tab.c"
    break;

  case 66: /* if_statement: IF expression '{' statement_list '}' elsif_clauses ELSE '{' statement_list '}'  */
#line 485 "src/puppet.y"
                                                                                     {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_IF;
        puppet_if_branch_t *first = calloc(1, sizeof(puppet_if_branch_t));
        first->condition = (yyvsp[-8].expr);
        first->body = *(yyvsp[-6].stmt_list);
        first->next = (yyvsp[-4].if_branch);
        (yyval.stmt)->data.if_stmt.branches = first;
        (yyval.stmt)->data.if_stmt.else_body = (yyvsp[-1].stmt_list);
        free((yyvsp[-6].stmt_list));
    }
#line 2147 "src/puppet.tab.c"
    break;

  case 67: /* elsif_clauses: %empty  */
#line 499 "src/puppet.y"
                { (yyval.if_branch) = NULL; }
#line 2153 "src/puppet.tab.c"
    break;

  case 68: /* elsif_clauses: elsif_clauses ELSIF expression '{' statement_list '}'  */
#line 500 "src/puppet.y"
                                                            {
        puppet_if_branch_t *branch = calloc(1, sizeof(puppet_if_branch_t));
        branch->condition = (yyvsp[-3].expr);
        branch->body = *(yyvsp[-1].stmt_list);
        branch->next = NULL;
        free((yyvsp[-1].stmt_list));
        
        if ((yyvsp[-5].if_branch) == NULL) {
            (yyval.if_branch) = branch;
        } else {
            puppet_if_branch_t *last = (yyvsp[-5].if_branch);
            while (last->next) last = last->next;
            last->next = branch;
            (yyval.if_branch) = (yyvsp[-5].if_branch);
        }
    }
#line 2174 "src/puppet.tab.c"
    break;

  case 69: /* unless_statement: UNLESS expression '{' statement_list '}'  */
#line 519 "src/puppet.y"
                                             {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_UNLESS;
        (yyval.stmt)->data.unless_stmt.condition = (yyvsp[-3].expr);
        (yyval.stmt)->data.unless_stmt.body = *(yyvsp[-1].stmt_list);
        free((yyvsp[-1].stmt_list));
    }
#line 2186 "src/puppet.tab.c"
    break;

  case 70: /* case_statement: CASE expression '{' case_when_list '}'  */
#line 529 "src/puppet.y"
                                           {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_CASE;
        (yyval.stmt)->data.case_stmt.expr = (yyvsp[-3].expr);
        /* TODO: Implement case when list */
    }
#line 2197 "src/puppet.tab.c"
    break;

  case 73: /* case_when: expression_list ':' '{' statement_list '}'  */
#line 543 "src/puppet.y"
                                               {
        (yyval.case_when) = calloc(1, sizeof(puppet_case_when_t));
        /* TODO: Implement case when */
    }
#line 2206 "src/puppet.tab.c"
    break;

  case 74: /* case_when: DEFAULT ':' '{' statement_list '}'  */
#line 547 "src/puppet.y"
                                         {
        (yyval.case_when) = calloc(1, sizeof(puppet_case_when_t));
        /* TODO: Implement default case */
    }
#line 2215 "src/puppet.tab.c"
    break;

  case 75: /* assignment_statement: VARIABLE '=' expression  */
#line 554 "src/puppet.y"
                            {
        (yyval.stmt) = puppet_stmt_create_assignment((yyvsp[-2].string), (yyvsp[0].expr));
        free((yyvsp[-2].string));
    }
#line 2224 "src/puppet.tab.c"
    break;

  case 76: /* append_statement: VARIABLE APPEND expression  */
#line 561 "src/puppet.y"
                               {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_APPEND;
        (yyval.stmt)->data.append.variable = puppet_string_create((yyvsp[-2].string));
        (yyval.stmt)->data.append.value = (yyvsp[0].expr);
        free((yyvsp[-2].string));
    }
#line 2236 "src/puppet.tab.c"
    break;

  case 77: /* function_statement: NAME '(' ')'  */
#line 571 "src/puppet.y"
                 {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_FUNCTION_CALL;
        (yyval.stmt)->data.expr = calloc(1, sizeof(puppet_expr_t));
        (yyval.stmt)->data.expr->type = PUPPET_EXPR_FUNCALL;
        (yyval.stmt)->data.expr->data.funcall.name = puppet_string_create((yyvsp[-2].string));
        (yyval.stmt)->data.expr->data.funcall.args.count = 0;
        (yyval.stmt)->data.expr->data.funcall.args.exprs = NULL;
        free((yyvsp[-2].string));
    }
#line 2251 "src/puppet.tab.c"
    break;

  case 78: /* function_statement: NAME '(' expression_list ')'  */
#line 581 "src/puppet.y"
                                   {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_FUNCTION_CALL;
        (yyval.stmt)->data.expr = calloc(1, sizeof(puppet_expr_t));
        (yyval.stmt)->data.expr->type = PUPPET_EXPR_FUNCALL;
        (yyval.stmt)->data.expr->data.funcall.name = puppet_string_create((yyvsp[-3].string));
        (yyval.stmt)->data.expr->data.funcall.args = *(yyvsp[-1].expr_list);
        free((yyvsp[-3].string));
        free((yyvsp[-1].expr_list));
    }
#line 2266 "src/puppet.tab.c"
    break;

  case 79: /* resource_chain: statement ARROW statement  */
#line 594 "src/puppet.y"
                              {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_RESOURCE_CHAIN;
        (yyval.stmt)->data.chain.left = (yyvsp[-2].stmt);
        (yyval.stmt)->data.chain.right = (yyvsp[0].stmt);
        (yyval.stmt)->data.chain.type = CHAIN_BEFORE;
    }
#line 2278 "src/puppet.tab.c"
    break;

  case 80: /* resource_chain: statement NOTIFY statement  */
#line 601 "src/puppet.y"
                                 {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_RESOURCE_CHAIN;
        (yyval.stmt)->data.chain.left = (yyvsp[-2].stmt);
        (yyval.stmt)->data.chain.right = (yyvsp[0].stmt);
        (yyval.stmt)->data.chain.type = CHAIN_NOTIFY;
    }
#line 2290 "src/puppet.tab.c"
    break;

  case 81: /* include_statement: INCLUDE qualified_name  */
#line 611 "src/puppet.y"
                           {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_INCLUDE;
        (yyval.stmt)->data.names.exprs = calloc(1, sizeof(puppet_expr_t*));
        (yyval.stmt)->data.names.exprs[0] = calloc(1, sizeof(puppet_expr_t));
        (yyval.stmt)->data.names.exprs[0]->type = PUPPET_EXPR_VALUE;
        (yyval.stmt)->data.names.exprs[0]->data.value = puppet_value_create_string((yyvsp[0].string), strlen((yyvsp[0].string)));
        (yyval.stmt)->data.names.count = 1;
        free((yyvsp[0].string));
    }
#line 2305 "src/puppet.tab.c"
    break;

  case 82: /* require_statement: REQUIRE_KEYWORD qualified_name  */
#line 624 "src/puppet.y"
                                   {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_REQUIRE;
        (yyval.stmt)->data.names.exprs = calloc(1, sizeof(puppet_expr_t*));
        (yyval.stmt)->data.names.exprs[0] = calloc(1, sizeof(puppet_expr_t));
        (yyval.stmt)->data.names.exprs[0]->type = PUPPET_EXPR_VALUE;
        (yyval.stmt)->data.names.exprs[0]->data.value = puppet_value_create_string((yyvsp[0].string), strlen((yyvsp[0].string)));
        (yyval.stmt)->data.names.count = 1;
        free((yyvsp[0].string));
    }
#line 2320 "src/puppet.tab.c"
    break;

  case 83: /* contain_statement: CONTAIN qualified_name  */
#line 637 "src/puppet.y"
                           {
        (yyval.stmt) = calloc(1, sizeof(puppet_stmt_t));
        (yyval.stmt)->type = PUPPET_STMT_CONTAIN;
        (yyval.stmt)->data.names.exprs = calloc(1, sizeof(puppet_expr_t*));
        (yyval.stmt)->data.names.exprs[0] = calloc(1, sizeof(puppet_expr_t));
        (yyval.stmt)->data.names.exprs[0]->type = PUPPET_EXPR_VALUE;
        (yyval.stmt)->data.names.exprs[0]->data.value = puppet_value_create_string((yyvsp[0].string), strlen((yyvsp[0].string)));
        (yyval.stmt)->data.names.count = 1;
        free((yyvsp[0].string));
    }
#line 2335 "src/puppet.tab.c"
    break;

  case 95: /* primary_expression: '(' expression ')'  */
#line 664 "src/puppet.y"
                         { (yyval.expr) = (yyvsp[-1].expr); }
#line 2341 "src/puppet.tab.c"
    break;

  case 96: /* literal_expression: value  */
#line 668 "src/puppet.y"
          { (yyval.expr) = puppet_expr_create_value((yyvsp[0].value)); }
#line 2347 "src/puppet.tab.c"
    break;

  case 97: /* value: BOOLEAN  */
#line 672 "src/puppet.y"
            { (yyval.value) = puppet_value_create_bool((yyvsp[0].boolean)); }
#line 2353 "src/puppet.tab.c"
    break;

  case 98: /* value: NUMBER  */
#line 673 "src/puppet.y"
             { (yyval.value) = puppet_value_create_number((yyvsp[0].number)); }
#line 2359 "src/puppet.tab.c"
    break;

  case 99: /* value: STRING_LITERAL  */
#line 674 "src/puppet.y"
                     { (yyval.value) = puppet_value_create_string((yyvsp[0].string), strlen((yyvsp[0].string))); free((yyvsp[0].string)); }
#line 2365 "src/puppet.tab.c"
    break;

  case 100: /* value: DQSTRING_LITERAL  */
#line 675 "src/puppet.y"
                       { (yyval.value) = puppet_value_create_string((yyvsp[0].string), strlen((yyvsp[0].string))); free((yyvsp[0].string)); }
#line 2371 "src/puppet.tab.c"
    break;

  case 101: /* value: NAME  */
#line 676 "src/puppet.y"
           { (yyval.value) = puppet_value_create_string((yyvsp[0].string), strlen((yyvsp[0].string))); free((yyvsp[0].string)); }
#line 2377 "src/puppet.tab.c"
    break;

  case 102: /* value: UNDEF  */
#line 677 "src/puppet.y"
            { (yyval.value) = puppet_value_create_undef(); }
#line 2383 "src/puppet.tab.c"
    break;

  case 103: /* value: array_value  */
#line 678 "src/puppet.y"
                  { (yyval.value) = (yyvsp[0].value); }
#line 2389 "src/puppet.tab.c"
    break;

  case 104: /* value: hash_value  */
#line 679 "src/puppet.y"
                 { (yyval.value) = (yyvsp[0].value); }
#line 2395 "src/puppet.tab.c"
    break;

  case 105: /* value: REGEX  */
#line 680 "src/puppet.y"
            { 
        (yyval.value) = calloc(1, sizeof(puppet_value_t));
        (yyval.value)->type = PUPPET_VALUE_REGEXP;
        (yyval.value)->data.regexp = puppet_string_create((yyvsp[0].string));
        free((yyvsp[0].string));
    }
#line 2406 "src/puppet.tab.c"
    break;

  case 106: /* array_value: '[' ']'  */
#line 689 "src/puppet.y"
            { (yyval.value) = puppet_value_create_array(); }
#line 2412 "src/puppet.tab.c"
    break;

  case 107: /* array_value: '[' expression_list ']'  */
#line 690 "src/puppet.y"
                              { 
        (yyval.value) = puppet_value_create_array();
        /* TODO: Add expressions to array */
    }
#line 2421 "src/puppet.tab.c"
    break;

  case 108: /* hash_value: '{' '}'  */
#line 697 "src/puppet.y"
            { (yyval.value) = puppet_value_create_hash(); }
#line 2427 "src/puppet.tab.c"
    break;

  case 109: /* hash_value: '{' hash_pairs '}'  */
#line 698 "src/puppet.y"
                         {
        (yyval.value) = puppet_value_create_hash();
        /* TODO: Add pairs to hash */
    }
#line 2436 "src/puppet.tab.c"
    break;

  case 113: /* variable_expression: VARIABLE  */
#line 714 "src/puppet.y"
             { (yyval.expr) = puppet_expr_create_variable((yyvsp[0].string)); free((yyvsp[0].string)); }
#line 2442 "src/puppet.tab.c"
    break;

  case 114: /* funcall_expression: NAME '(' funcall_args ')'  */
#line 718 "src/puppet.y"
                              {
        (yyval.expr) = calloc(1, sizeof(puppet_expr_t));
        (yyval.expr)->type = PUPPET_EXPR_FUNCALL;
        (yyval.expr)->data.funcall.name = puppet_string_create((yyvsp[-3].string));
        if ((yyvsp[-1].expr_list)) {
            (yyval.expr)->data.funcall.args = *(yyvsp[-1].expr_list);
            free((yyvsp[-1].expr_list));
        }
        free((yyvsp[-3].string));
    }
#line 2457 "src/puppet.tab.c"
    break;

  case 115: /* funcall_args: expression  */
#line 743 "src/puppet.y"
               {
        (yyval.expr_list) = calloc(1, sizeof(puppet_expr_list_t));
        (yyval.expr_list)->exprs = malloc(sizeof(puppet_expr_t *));
        (yyval.expr_list)->exprs[0] = (yyvsp[0].expr);
        (yyval.expr_list)->count = 1;
    }
#line 2468 "src/puppet.tab.c"
    break;

  case 116: /* funcall_args: funcall_args ',' expression  */
#line 749 "src/puppet.y"
                                  {
        (yyval.expr_list) = (yyvsp[-2].expr_list);
        (yyval.expr_list)->exprs = realloc((yyval.expr_list)->exprs, ((yyval.expr_list)->count + 1) * sizeof(puppet_expr_t *));
        (yyval.expr_list)->exprs[(yyval.expr_list)->count++] = (yyvsp[0].expr);
    }
#line 2478 "src/puppet.tab.c"
    break;

  case 117: /* index_expression: primary_expression '[' expression ']'  */
#line 757 "src/puppet.y"
                                          {
        (yyval.expr) = calloc(1, sizeof(puppet_expr_t));
        (yyval.expr)->type = PUPPET_EXPR_INDEX;
        (yyval.expr)->data.index.object = (yyvsp[-3].expr);
        (yyval.expr)->data.index.index = (yyvsp[-1].expr);
    }
#line 2489 "src/puppet.tab.c"
    break;

  case 118: /* dot_expression: primary_expression '.' NAME  */
#line 766 "src/puppet.y"
                                {
        (yyval.expr) = calloc(1, sizeof(puppet_expr_t));
        (yyval.expr)->type = PUPPET_EXPR_DOT;
        (yyval.expr)->data.dot.object = (yyvsp[-2].expr);
        (yyval.expr)->data.dot.field = puppet_string_create((yyvsp[0].string));
        free((yyvsp[0].string));
    }
#line 2501 "src/puppet.tab.c"
    break;

  case 119: /* unary_expression: unary_op primary_expression  */
#line 776 "src/puppet.y"
                                {
        (yyval.expr) = puppet_expr_create_unop((yyvsp[-1].unop), (yyvsp[0].expr));
    }
#line 2509 "src/puppet.tab.c"
    break;

  case 120: /* unary_op: '!'  */
#line 782 "src/puppet.y"
        { (yyval.unop) = PUPPET_UNOP_NOT; }
#line 2515 "src/puppet.tab.c"
    break;

  case 121: /* unary_op: NOT  */
#line 783 "src/puppet.y"
          { (yyval.unop) = PUPPET_UNOP_NOT; }
#line 2521 "src/puppet.tab.c"
    break;

  case 122: /* unary_op: '-'  */
#line 784 "src/puppet.y"
                       { (yyval.unop) = PUPPET_UNOP_MINUS; }
#line 2527 "src/puppet.tab.c"
    break;

  case 123: /* unary_op: '+'  */
#line 785 "src/puppet.y"
                       { (yyval.unop) = PUPPET_UNOP_PLUS; }
#line 2533 "src/puppet.tab.c"
    break;

  case 124: /* binary_expression: expression arithmetic_op expression  */
#line 789 "src/puppet.y"
                                        {
        (yyval.expr) = puppet_expr_create_binop((yyvsp[-1].binop), (yyvsp[-2].expr), (yyvsp[0].expr));
    }
#line 2541 "src/puppet.tab.c"
    break;

  case 125: /* binary_expression: expression comparison_op expression  */
#line 792 "src/puppet.y"
                                          {
        (yyval.expr) = puppet_expr_create_binop((yyvsp[-1].binop), (yyvsp[-2].expr), (yyvsp[0].expr));
    }
#line 2549 "src/puppet.tab.c"
    break;

  case 126: /* binary_expression: expression logical_op expression  */
#line 795 "src/puppet.y"
                                       {
        (yyval.expr) = puppet_expr_create_binop((yyvsp[-1].binop), (yyvsp[-2].expr), (yyvsp[0].expr));
    }
#line 2557 "src/puppet.tab.c"
    break;

  case 127: /* binary_expression: expression IN expression  */
#line 798 "src/puppet.y"
                               {
        (yyval.expr) = puppet_expr_create_binop(PUPPET_OP_IN, (yyvsp[-2].expr), (yyvsp[0].expr));
    }
#line 2565 "src/puppet.tab.c"
    break;

  case 128: /* arithmetic_op: '+'  */
#line 804 "src/puppet.y"
        { (yyval.binop) = PUPPET_OP_ADD; }
#line 2571 "src/puppet.tab.c"
    break;

  case 129: /* arithmetic_op: '-'  */
#line 805 "src/puppet.y"
          { (yyval.binop) = PUPPET_OP_SUB; }
#line 2577 "src/puppet.tab.c"
    break;

  case 130: /* arithmetic_op: '*'  */
#line 806 "src/puppet.y"
          { (yyval.binop) = PUPPET_OP_MUL; }
#line 2583 "src/puppet.tab.c"
    break;

  case 131: /* arithmetic_op: '/'  */
#line 807 "src/puppet.y"
          { (yyval.binop) = PUPPET_OP_DIV; }
#line 2589 "src/puppet.tab.c"
    break;

  case 132: /* arithmetic_op: '%'  */
#line 808 "src/puppet.y"
          { (yyval.binop) = PUPPET_OP_MOD; }
#line 2595 "src/puppet.tab.c"
    break;

  case 133: /* arithmetic_op: LSHIFT  */
#line 809 "src/puppet.y"
             { (yyval.binop) = PUPPET_OP_LSHIFT; }
#line 2601 "src/puppet.tab.c"
    break;

  case 134: /* arithmetic_op: RSHIFT  */
#line 810 "src/puppet.y"
             { (yyval.binop) = PUPPET_OP_RSHIFT; }
#line 2607 "src/puppet.tab.c"
    break;

  case 135: /* comparison_op: '<'  */
#line 814 "src/puppet.y"
        { (yyval.binop) = PUPPET_OP_LT; }
#line 2613 "src/puppet.tab.c"
    break;

  case 136: /* comparison_op: '>'  */
#line 815 "src/puppet.y"
          { (yyval.binop) = PUPPET_OP_GT; }
#line 2619 "src/puppet.tab.c"
    break;

  case 137: /* comparison_op: LE  */
#line 816 "src/puppet.y"
         { (yyval.binop) = PUPPET_OP_LE; }
#line 2625 "src/puppet.tab.c"
    break;

  case 138: /* comparison_op: GE  */
#line 817 "src/puppet.y"
         { (yyval.binop) = PUPPET_OP_GE; }
#line 2631 "src/puppet.tab.c"
    break;

  case 139: /* comparison_op: EQ  */
#line 818 "src/puppet.y"
         { (yyval.binop) = PUPPET_OP_EQ; }
#line 2637 "src/puppet.tab.c"
    break;

  case 140: /* comparison_op: NE  */
#line 819 "src/puppet.y"
         { (yyval.binop) = PUPPET_OP_NE; }
#line 2643 "src/puppet.tab.c"
    break;

  case 141: /* comparison_op: MATCH  */
#line 820 "src/puppet.y"
            { (yyval.binop) = PUPPET_OP_MATCH; }
#line 2649 "src/puppet.tab.c"
    break;

  case 142: /* comparison_op: NOT_MATCH  */
#line 821 "src/puppet.y"
                { (yyval.binop) = PUPPET_OP_NOT_MATCH; }
#line 2655 "src/puppet.tab.c"
    break;

  case 143: /* logical_op: AND  */
#line 825 "src/puppet.y"
        { (yyval.binop) = PUPPET_OP_AND; }
#line 2661 "src/puppet.tab.c"
    break;

  case 144: /* logical_op: OR  */
#line 826 "src/puppet.y"
         { (yyval.binop) = PUPPET_OP_OR; }
#line 2667 "src/puppet.tab.c"
    break;

  case 145: /* selector_expression: expression '?' '{' selector_cases '}'  */
#line 830 "src/puppet.y"
                                          {
        /* TODO: Implement selector */
        (yyval.expr) = (yyvsp[-4].expr);
    }
#line 2676 "src/puppet.tab.c"
    break;

  case 150: /* lambda_expression: '|' parameter_list '|' '{' statement_list '}'  */
#line 847 "src/puppet.y"
                                                  {
        (yyval.expr) = calloc(1, sizeof(puppet_expr_t));
        (yyval.expr)->type = PUPPET_EXPR_LAMBDA;
        puppet_lambda_t *lambda = calloc(1, sizeof(puppet_lambda_t));
        if ((yyvsp[-4].param_list)) {
            lambda->params = *(yyvsp[-4].param_list);
            free((yyvsp[-4].param_list));
        }
        /* TODO: Convert statements to expressions */
        (yyval.expr)->data.lambda = lambda;
        free((yyvsp[-1].stmt_list));
    }
#line 2693 "src/puppet.tab.c"
    break;

  case 151: /* resource_reference: TYPE_NAME '[' expression ']'  */
#line 862 "src/puppet.y"
                                 {
        (yyval.expr) = calloc(1, sizeof(puppet_expr_t));
        (yyval.expr)->type = PUPPET_EXPR_RESOURCE_REF;
        (yyval.expr)->data.resource_ref.type = puppet_string_create((yyvsp[-3].string));
        (yyval.expr)->data.resource_ref.title = (yyvsp[-1].expr);
        free((yyvsp[-3].string));
    }
#line 2705 "src/puppet.tab.c"
    break;

  case 152: /* resource_reference: CLASSREF '[' expression ']'  */
#line 869 "src/puppet.y"
                                  {
        (yyval.expr) = calloc(1, sizeof(puppet_expr_t));
        (yyval.expr)->type = PUPPET_EXPR_RESOURCE_REF;
        (yyval.expr)->data.resource_ref.type = puppet_string_create((yyvsp[-3].string));
        (yyval.expr)->data.resource_ref.title = (yyvsp[-1].expr);
        free((yyvsp[-3].string));
    }
#line 2717 "src/puppet.tab.c"
    break;

  case 153: /* type_expression: TYPE_NAME  */
#line 879 "src/puppet.y"
              { (yyval.expr) = puppet_expr_create_variable((yyvsp[0].string)); free((yyvsp[0].string)); }
#line 2723 "src/puppet.tab.c"
    break;

  case 154: /* type_expression: TYPE_NAME '[' expression_list ']'  */
#line 880 "src/puppet.y"
                                        {
        /* TODO: Implement parameterized types */
        (yyval.expr) = puppet_expr_create_variable((yyvsp[-3].string)); 
        free((yyvsp[-3].string));
    }
#line 2733 "src/puppet.tab.c"
    break;

  case 155: /* expression_list: expression  */
#line 888 "src/puppet.y"
               {
        (yyval.expr_list) = calloc(1, sizeof(puppet_expr_list_t));
        (yyval.expr_list)->exprs = malloc(sizeof(puppet_expr_t *));
        (yyval.expr_list)->exprs[0] = (yyvsp[0].expr);
        (yyval.expr_list)->count = 1;
    }
#line 2744 "src/puppet.tab.c"
    break;

  case 156: /* expression_list: expression_list ',' expression  */
#line 894 "src/puppet.y"
                                     {
        (yyval.expr_list) = (yyvsp[-2].expr_list);
        (yyval.expr_list)->exprs = realloc((yyval.expr_list)->exprs, ((yyval.expr_list)->count + 1) * sizeof(puppet_expr_t *));
        (yyval.expr_list)->exprs[(yyval.expr_list)->count++] = (yyvsp[0].expr);
    }
#line 2754 "src/puppet.tab.c"
    break;


#line 2758 "src/puppet.tab.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 906 "src/puppet.y"


void yyerror(const char *s) {
    fprintf(stderr, "Parse error at line %d: %s\n", yylineno, s);
    fprintf(stderr, "Near token: %s\n", yytext);
}

