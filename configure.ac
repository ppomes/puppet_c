AC_INIT([puppet-c], [0.0.1], [pierre.pomes@gmail.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

# Libtool support for shared libraries
AM_PROG_AR
LT_INIT

AC_PROG_CC

# Check for tree-sitter library (required for parsing)
AC_ARG_WITH([treesitter],
    [AS_HELP_STRING([--with-treesitter=PATH], [Path to tree-sitter installation])],
    [treesitter_prefix=$withval],
    [treesitter_prefix=])

if test -n "$treesitter_prefix"; then
    AC_MSG_CHECKING([for tree-sitter in $treesitter_prefix])
    if test -f "$treesitter_prefix/include/tree_sitter/api.h"; then
        TREESITTER_CFLAGS="-I$treesitter_prefix/include"
        TREESITTER_LIBS="-L$treesitter_prefix/lib -ltree-sitter"
        AC_MSG_RESULT([yes])
    else
        AC_MSG_RESULT([no])
        AC_MSG_ERROR([tree-sitter headers not found in $treesitter_prefix/include])
    fi
else
    PKG_CHECK_MODULES([TREESITTER], [tree-sitter], [], [
        AC_MSG_ERROR([tree-sitter not found. Install libtree-sitter-dev package or use --with-treesitter=PATH])
    ])
fi
AC_DEFINE([HAVE_TREESITTER], [1], [Define if tree-sitter is available])
AC_SUBST([TREESITTER_CFLAGS])
AC_SUBST([TREESITTER_LIBS])

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

        # Check Ruby version - require Ruby 3.0-3.3
        AC_MSG_CHECKING([if Ruby version $ruby_version is supported])
        ruby_major=`echo $ruby_version | cut -d. -f1`
        ruby_minor=`echo $ruby_version | cut -d. -f2`

        if test "$ruby_major" -lt 3; then
            AC_MSG_RESULT([no])
            AC_MSG_ERROR([Ruby $ruby_version is not supported. Ruby 3.0 or later is required. Please use Ruby 3.0-3.3 (Ruby 3.4+ has ERB embedding incompatibilities)])
        fi

        if test "$ruby_major" -ge 4; then
            AC_MSG_RESULT([no])
            AC_MSG_ERROR([Ruby $ruby_version is not supported. Ruby 4.0+ has breaking changes incompatible with ERB embedding. Please use Ruby 3.0-3.3])
        fi

        if test "$ruby_major" -eq 3 -a "$ruby_minor" -ge 4; then
            AC_MSG_RESULT([no])
            AC_MSG_ERROR([Ruby $ruby_version is not supported. Ruby 3.4+ has ERB embedding incompatibilities. Please use Ruby 3.0-3.3 (recommended: Ruby 3.2 or 3.3)])
        fi

        AC_MSG_RESULT([yes (Ruby $ruby_version)])

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

            # Check Ruby version - require Ruby 3.0-3.3
            AC_MSG_CHECKING([if Ruby version $ruby_version is supported])
            ruby_major=`echo $ruby_version | cut -d. -f1`
            ruby_minor=`echo $ruby_version | cut -d. -f2`

            if test "$ruby_major" -lt 3; then
                AC_MSG_RESULT([no])
                AC_MSG_ERROR([Ruby $ruby_version is not supported. Ruby 3.0 or later is required. Please use Ruby 3.0-3.3 (Ruby 3.4+ has ERB embedding incompatibilities)])
            fi

            if test "$ruby_major" -ge 4; then
                AC_MSG_RESULT([no])
                AC_MSG_ERROR([Ruby $ruby_version is not supported. Ruby 4.0+ has breaking changes incompatible with ERB embedding. Please use Ruby 3.0-3.3])
            fi

            if test "$ruby_major" -eq 3 -a "$ruby_minor" -ge 4; then
                AC_MSG_RESULT([no])
                AC_MSG_ERROR([Ruby $ruby_version is not supported. Ruby 3.4+ has ERB embedding incompatibilities. Please use Ruby 3.0-3.3 (recommended: Ruby 3.2 or 3.3)])
            fi

            AC_MSG_RESULT([yes])

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
AC_ARG_WITH([yaml],
    [AS_HELP_STRING([--with-yaml=PATH], [Path to libyaml installation])],
    [yaml_prefix=$withval],
    [yaml_prefix=])

if test -n "$yaml_prefix"; then
    AC_MSG_CHECKING([for libyaml in $yaml_prefix])
    if test -f "$yaml_prefix/include/yaml.h"; then
        YAML_CFLAGS="-I$yaml_prefix/include"
        YAML_LIBS="-L$yaml_prefix/lib -lyaml"
        AC_MSG_RESULT([yes])
    else
        AC_MSG_RESULT([no])
        AC_MSG_ERROR([libyaml headers not found in $yaml_prefix/include])
    fi
else
    PKG_CHECK_MODULES([YAML], [yaml-0.1], [], [
        AC_MSG_ERROR([libyaml not found. Install libyaml-dev package or use --with-yaml=PATH])
    ])
fi
AC_SUBST([YAML_CFLAGS])
AC_SUBST([YAML_LIBS])

# Check for OpenSSL (required for crypto functions)
AC_ARG_WITH([openssl],
    [AS_HELP_STRING([--with-openssl=PATH], [Path to OpenSSL installation])],
    [openssl_prefix=$withval],
    [openssl_prefix=])

if test -n "$openssl_prefix"; then
    AC_MSG_CHECKING([for OpenSSL in $openssl_prefix])
    if test -f "$openssl_prefix/include/openssl/ssl.h"; then
        OPENSSL_CFLAGS="-I$openssl_prefix/include"
        OPENSSL_LIBS="-L$openssl_prefix/lib -lssl -lcrypto"
        AC_MSG_RESULT([yes])
    else
        AC_MSG_RESULT([no])
        AC_MSG_ERROR([OpenSSL headers not found in $openssl_prefix/include])
    fi
else
    PKG_CHECK_MODULES([OPENSSL], [openssl], [], [
        AC_MSG_ERROR([OpenSSL not found. Install libssl-dev package or use --with-openssl=PATH])
    ])
fi
AC_DEFINE([HAVE_OPENSSL], [1], [Define if OpenSSL is available])
AC_SUBST([OPENSSL_CFLAGS])
AC_SUBST([OPENSSL_LIBS])

# Check for libmicrohttpd (required for puppetc-server)
AC_ARG_WITH([microhttpd],
    [AS_HELP_STRING([--with-microhttpd=PATH], [Path to libmicrohttpd installation])],
    [microhttpd_prefix=$withval],
    [microhttpd_prefix=])

if test -n "$microhttpd_prefix"; then
    AC_MSG_CHECKING([for libmicrohttpd in $microhttpd_prefix])
    if test -f "$microhttpd_prefix/include/microhttpd.h"; then
        MHD_CFLAGS="-I$microhttpd_prefix/include"
        MHD_LIBS="-L$microhttpd_prefix/lib -lmicrohttpd"
        AC_MSG_RESULT([yes])
    else
        AC_MSG_RESULT([no])
        AC_MSG_ERROR([libmicrohttpd headers not found in $microhttpd_prefix/include])
    fi
else
    PKG_CHECK_MODULES([MHD], [libmicrohttpd], [], [
        AC_MSG_ERROR([libmicrohttpd not found. Install libmicrohttpd-dev package or use --with-microhttpd=PATH])
    ])
fi
AC_DEFINE([HAVE_MICROHTTPD], [1], [Define if libmicrohttpd is available])
AC_SUBST([MHD_CFLAGS])
AC_SUBST([MHD_LIBS])

# Check for libcurl (required for puppetc-agent)
AC_ARG_WITH([curl],
    [AS_HELP_STRING([--with-curl=PATH], [Path to libcurl installation])],
    [curl_prefix=$withval],
    [curl_prefix=])

if test -n "$curl_prefix"; then
    AC_MSG_CHECKING([for libcurl in $curl_prefix])
    if test -f "$curl_prefix/include/curl/curl.h"; then
        CURL_CFLAGS="-I$curl_prefix/include"
        CURL_LIBS="-L$curl_prefix/lib -lcurl"
        AC_MSG_RESULT([yes])
    else
        AC_MSG_RESULT([no])
        AC_MSG_ERROR([libcurl headers not found in $curl_prefix/include])
    fi
else
    PKG_CHECK_MODULES([CURL], [libcurl], [], [
        AC_MSG_ERROR([libcurl not found. Install libcurl4-openssl-dev package or use --with-curl=PATH])
    ])
fi
AC_DEFINE([HAVE_CURL], [1], [Define if libcurl is available])
AC_SUBST([CURL_CFLAGS])
AC_SUBST([CURL_LIBS])

# Check for SQLite3 (required for PuppetDB)
AC_ARG_WITH([sqlite],
    [AS_HELP_STRING([--with-sqlite=PATH], [Path to SQLite3 installation])],
    [sqlite_prefix=$withval],
    [sqlite_prefix=])

if test -n "$sqlite_prefix"; then
    AC_MSG_CHECKING([for SQLite3 in $sqlite_prefix])
    if test -f "$sqlite_prefix/include/sqlite3.h"; then
        SQLITE_CFLAGS="-I$sqlite_prefix/include"
        SQLITE_LIBS="-L$sqlite_prefix/lib -lsqlite3"
        AC_MSG_RESULT([yes])
    else
        AC_MSG_RESULT([no])
        AC_MSG_ERROR([SQLite3 headers not found in $sqlite_prefix/include])
    fi
else
    PKG_CHECK_MODULES([SQLITE], [sqlite3], [], [
        AC_MSG_ERROR([SQLite3 not found. Install libsqlite3-dev package or use --with-sqlite=PATH])
    ])
fi
AC_DEFINE([HAVE_SQLITE], [1], [Define if SQLite3 is available])
AC_SUBST([SQLITE_CFLAGS])
AC_SUBST([SQLITE_LIBS])

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
