AC_INIT([puppet-c], [0.0.1], [pierre.pomes@gmail.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

# Libtool support for shared libraries
AM_PROG_AR
LT_INIT

AC_PROG_CC

# Check for tree-sitter library (required for parsing)
PKG_CHECK_MODULES([TREESITTER], [tree-sitter], [
    AC_DEFINE([HAVE_TREESITTER], [1], [Define if tree-sitter is available])
    AC_SUBST([TREESITTER_CFLAGS])
    AC_SUBST([TREESITTER_LIBS])
], [
    AC_MSG_ERROR([tree-sitter not found. Install libtree-sitter-dev package.])
])

# Check for Ruby (required for ERB templates)
AC_ARG_WITH([ruby],
    [AS_HELP_STRING([--with-ruby=PATH], [Path to Ruby installation])],
    [ruby_prefix=$withval],
    [ruby_prefix=])

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
    PKG_CHECK_MODULES([RUBY], [ruby], [], [
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
            else
                AC_MSG_RESULT([no])
                AC_MSG_ERROR([Ruby development headers not found. Install ruby-dev package or use --with-ruby=PATH])
            fi
        else
            AC_MSG_ERROR([Ruby not found. Install ruby package or use --with-ruby=PATH])
        fi
    ])
fi

AC_SUBST([RUBY_CFLAGS])
AC_SUBST([RUBY_LIBS])

# Check for libyaml (required for Hiera support)
PKG_CHECK_MODULES([YAML], [yaml-0.1], [
    AC_SUBST([YAML_CFLAGS])
    AC_SUBST([YAML_LIBS])
], [
    AC_MSG_ERROR([libyaml not found. Install libyaml-dev package.])
])

# Check for OpenSSL (required for crypto functions)
PKG_CHECK_MODULES([OPENSSL], [openssl], [
    AC_DEFINE([HAVE_OPENSSL], [1], [Define if OpenSSL is available])
    AC_SUBST([OPENSSL_CFLAGS])
    AC_SUBST([OPENSSL_LIBS])
], [
    AC_MSG_ERROR([OpenSSL not found. Install libssl-dev package.])
])

# Check for libmicrohttpd (required for puppetc-server)
PKG_CHECK_MODULES([MHD], [libmicrohttpd], [
    AC_DEFINE([HAVE_MICROHTTPD], [1], [Define if libmicrohttpd is available])
    AC_SUBST([MHD_CFLAGS])
    AC_SUBST([MHD_LIBS])
], [
    AC_MSG_ERROR([libmicrohttpd not found. Install libmicrohttpd-dev package.])
])

# Check for libcurl (required for puppetc-agent)
PKG_CHECK_MODULES([CURL], [libcurl], [
    AC_DEFINE([HAVE_CURL], [1], [Define if libcurl is available])
    AC_SUBST([CURL_CFLAGS])
    AC_SUBST([CURL_LIBS])
], [
    AC_MSG_ERROR([libcurl not found. Install libcurl4-openssl-dev package.])
])

# Check for SQLite3 (required for PuppetDB)
PKG_CHECK_MODULES([SQLITE], [sqlite3], [
    AC_DEFINE([HAVE_SQLITE], [1], [Define if SQLite3 is available])
    AC_SUBST([SQLITE_CFLAGS])
    AC_SUBST([SQLITE_LIBS])
], [
    AC_MSG_ERROR([SQLite3 not found. Install libsqlite3-dev package.])
])

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
    common/Makefile
    compiler/Makefile
    facter/Makefile
    server/Makefile
    agent/Makefile
    tests/Makefile
])

AC_OUTPUT

echo ""
echo "Configuration summary:"
echo "  Debug mode:       $enable_debug"
echo "  CC:               $CC"
echo "  CFLAGS:           $CFLAGS"
echo ""
