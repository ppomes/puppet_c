AC_INIT([puppet-c], [0.0.1], [pierre.pomes@gmail.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_PROG_CC
AC_PROG_LEX
AC_PROG_YACC

# Check for required programs
AC_CHECK_PROG([FLEX], [flex], [flex])
if test -z "$FLEX"; then
    AC_MSG_ERROR([flex is required but not found])
fi

AC_CHECK_PROG([BISON], [bison], [bison])
if test -z "$BISON"; then
    AC_MSG_ERROR([bison is required but not found])
fi

# Check for Ruby support (optional)
AC_ARG_ENABLE([ruby],
    [AS_HELP_STRING([--enable-ruby], [Enable Ruby/ERB support])],
    [enable_ruby=$enableval],
    [enable_ruby=auto])

AC_ARG_WITH([ruby],
    [AS_HELP_STRING([--with-ruby=PATH], [Path to Ruby installation])],
    [ruby_prefix=$withval],
    [ruby_prefix=])

if test "x$enable_ruby" != "xno"; then
    if test -n "$ruby_prefix"; then
        # User specified Ruby path
        AC_MSG_CHECKING([for Ruby in $ruby_prefix])
        RUBY="$ruby_prefix/bin/ruby"
        if test -x "$RUBY"; then
            ruby_version=`$RUBY -e 'puts RUBY_VERSION'`
            ruby_arch=`$RUBY -e 'puts RUBY_PLATFORM'`
            # Try to find the actual Ruby include directory
            ruby_include_base="$ruby_prefix/include"
            if test -d "$ruby_include_base/ruby-$ruby_version"; then
                ruby_include_dir="$ruby_include_base/ruby-$ruby_version"
            else
                # Find any ruby-* directory
                ruby_include_dir=`find $ruby_include_base -name "ruby-*" -type d 2>/dev/null | head -1`
            fi
            if test -n "$ruby_include_dir" -a -d "$ruby_include_dir"; then
                RUBY_CFLAGS="-I$ruby_include_dir -I$ruby_include_dir/$ruby_arch"
                RUBY_LIBS="-L$ruby_prefix/lib -lruby"
                AC_MSG_RESULT([yes (found $ruby_include_dir)])
                ruby_support=yes
            else
                AC_MSG_RESULT([no])
                AC_MSG_ERROR([Ruby include directory not found in $ruby_prefix/include])
            fi
        else
            AC_MSG_RESULT([no])
            AC_MSG_ERROR([Ruby not found in $ruby_prefix])
        fi
    else
        # Try pkg-config first
        PKG_CHECK_MODULES([RUBY], [ruby], [
            ruby_support=yes
        ], [
            # Fallback to finding Ruby executable
            AC_PATH_PROG([RUBY], [ruby])
            if test -n "$RUBY"; then
                AC_MSG_CHECKING([for Ruby configuration])
                ruby_version=`$RUBY -e 'puts RUBY_VERSION'`
                ruby_archdir=`$RUBY -e 'puts RbConfig::CONFIG[["archdir"]]'`
                ruby_includedir=`$RUBY -e 'puts RbConfig::CONFIG[["rubyhdrdir"]]'`
                ruby_libdir=`$RUBY -e 'puts RbConfig::CONFIG[["libdir"]]'`
                
                if test -d "$ruby_includedir"; then
                    RUBY_CFLAGS="-I$ruby_includedir -I$ruby_archdir"
                    RUBY_LIBS="-L$ruby_libdir -lruby"
                    AC_MSG_RESULT([yes])
                    ruby_support=yes
                else
                    AC_MSG_RESULT([no])
                    if test "x$enable_ruby" = "xyes"; then
                        AC_MSG_ERROR([Ruby development headers not found. Try --with-ruby=PATH])
                    fi
                    ruby_support=no
                fi
            else
                if test "x$enable_ruby" = "xyes"; then
                    AC_MSG_ERROR([Ruby not found. Try --with-ruby=PATH])
                fi
                ruby_support=no
            fi
        ])
    fi
    
    if test "x$ruby_support" = "xyes"; then
        AC_DEFINE([HAVE_RUBY], [1], [Define if Ruby support is available])
        AC_SUBST([RUBY_CFLAGS])
        AC_SUBST([RUBY_LIBS])
    fi
else
    ruby_support=no
fi

AM_CONDITIONAL([HAVE_RUBY], [test "x$ruby_support" = "xyes"])

# Check for standard library functions
AC_CHECK_FUNCS([malloc realloc free strdup strlen strcpy snprintf])
AC_CHECK_HEADERS([stdio.h stdlib.h string.h])

# Debug mode
AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug], [Enable debug mode])],
    [enable_debug=$enableval],
    [enable_debug=no])

if test "x$enable_debug" = "xyes"; then
    AC_DEFINE([DEBUG], [1], [Define if debug mode is enabled])
    CFLAGS="$CFLAGS -g -DYYDEBUG=1"
fi

AC_CONFIG_FILES([
    Makefile
    src/Makefile
    tests/Makefile
])

AC_OUTPUT

echo ""
echo "Configuration summary:"
echo "  Ruby support:     $ruby_support"
echo "  Debug mode:       $enable_debug"
echo "  CC:               $CC"
echo "  CFLAGS:           $CFLAGS"
echo ""