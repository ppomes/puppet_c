# Library and binary
lib_LTLIBRARIES = libpuppetc.la
bin_PROGRAMS = puppetc-compile

# Library sources
libpuppetc_la_SOURCES = \
    puppet_ast.c \
    puppet_catalog.c \
    puppet_interpreter.c \
    puppet_json.c \
    puppet_json_parser.c \
    puppet_loader.c \
    puppet_stdlib.c \
    puppet_hiera.c \
    puppet_ts_parser.c \
    puppet_erb.c \
    puppet_lint.c \
    puppet_deadcode.c \
    $(top_srcdir)/tree-sitter-puppet/src/parser.c \
    $(top_srcdir)/tree-sitter-puppet/src/scanner.c

# Library version info (current:revision:age)
# See: https://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html
libpuppetc_la_LDFLAGS = -version-info 0:0:0

# Library dependencies
libpuppetc_la_LIBADD = $(top_builddir)/common/libpuppetc_common.la $(top_builddir)/facter/libfacter_c.la $(RUBY_LIBS) -lm -lpthread $(YAML_LIBS) $(OPENSSL_LIBS) $(TREESITTER_LIBS)

# Binary sources (just main.c, links against library)
puppetc_compile_SOURCES = main.c
puppetc_compile_LDADD = libpuppetc.la $(top_builddir)/common/libpuppetc_common.la $(top_builddir)/facter/libfacter_c.la

# Include directories
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/tree-sitter-puppet/src

# Compiler flags
AM_CFLAGS = -Wall -Wextra -g $(TREESITTER_CFLAGS) $(RUBY_CFLAGS) $(YAML_CFLAGS) $(OPENSSL_CFLAGS)

# Header files to install
puppetcincludedir = $(includedir)/puppetc
puppetcinclude_HEADERS = \
    $(top_srcdir)/include/puppet_ast.h \
    $(top_srcdir)/include/puppet_catalog.h \
    $(top_srcdir)/include/puppet_interpreter.h \
    $(top_srcdir)/include/puppet_json.h \
    $(top_srcdir)/include/puppet_json_parser.h \
    $(top_srcdir)/include/puppet_loader.h \
    $(top_srcdir)/include/puppet_stdlib.h \
    $(top_srcdir)/include/puppet_hiera.h \
    $(top_srcdir)/include/puppet_erb.h \
    $(top_srcdir)/include/puppet_lint.h \
    $(top_srcdir)/include/puppet_deadcode.h \
    puppet_ts_parser.h
