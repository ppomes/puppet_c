# Library and binary
lib_LTLIBRARIES = libpuppetc.la
bin_PROGRAMS = puppetc

# Generated parser files
BUILT_SOURCES = puppet.tab.c puppet.tab.h lex.yy.c
CLEANFILES = $(BUILT_SOURCES)

# Library sources (everything except main.c)
libpuppetc_la_SOURCES = \
    puppet_ast.c \
    puppet_catalog.c \
    puppet_interpreter.c \
    puppet_json.c \
    puppet_json_parser.c \
    puppet_loader.c \
    puppet_memory.c \
    puppet_stdlib.c \
    puppet_hiera.c \
    $(BUILT_SOURCES)

# Conditionally add ERB support to library
if HAVE_RUBY
libpuppetc_la_SOURCES += puppet_erb.c
endif

# Library version info (current:revision:age)
# See: https://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html
libpuppetc_la_LDFLAGS = -version-info 0:0:0

# Library dependencies
if HAVE_RUBY
libpuppetc_la_LIBADD = $(RUBY_LIBS) -lm -lpthread $(YAML_LIBS) $(OPENSSL_LIBS)
else
libpuppetc_la_LIBADD = -lpthread $(YAML_LIBS) $(OPENSSL_LIBS)
endif

# Binary sources (just main.c, links against library)
puppetc_SOURCES = main.c
puppetc_LDADD = libpuppetc.la

# Include directories
AM_CPPFLAGS = -I$(top_srcdir)/include

# Compiler flags
AM_CFLAGS = -Wall -Wextra -g

# Conditional Ruby flags
if HAVE_RUBY
AM_CFLAGS += $(RUBY_CFLAGS) -DHAVE_RUBY
else
AM_CFLAGS += -DDISABLE_RUBY
endif

# YAML/Hiera flags
AM_CFLAGS += $(YAML_CFLAGS)

# OpenSSL flags
AM_CFLAGS += $(OPENSSL_CFLAGS)

# Header files to install
puppetcincludedir = $(includedir)/puppetc
puppetcinclude_HEADERS = \
    $(top_srcdir)/include/puppet_ast.h \
    $(top_srcdir)/include/puppet_catalog.h \
    $(top_srcdir)/include/puppet_interpreter.h \
    $(top_srcdir)/include/puppet_json.h \
    $(top_srcdir)/include/puppet_json_parser.h \
    $(top_srcdir)/include/puppet_loader.h \
    $(top_srcdir)/include/puppet_memory.h \
    $(top_srcdir)/include/puppet_stdlib.h \
    $(top_srcdir)/include/puppet_hiera.h

if HAVE_RUBY
puppetcinclude_HEADERS += $(top_srcdir)/include/puppet_erb.h
endif

# Flex/Bison rules
lex.yy.c: puppet.l puppet.tab.h
	$(LEX) -o $@ $<

puppet.tab.c puppet.tab.h: puppet.y
	$(YACC) -d -o puppet.tab.c $<
	cp puppet.tab.h $(top_srcdir)/include/

# Ensure proper build order
puppet_ast.lo: puppet.tab.h
puppet_catalog.lo: puppet.tab.h
puppet_interpreter.lo: puppet.tab.h
puppet_json.lo: puppet.tab.h
puppet_loader.lo: puppet.tab.h
puppet_stdlib.lo: puppet.tab.h
puppet_hiera.lo: puppet.tab.h
main.$(OBJEXT): puppet.tab.h
if HAVE_RUBY
puppet_erb.lo: puppet.tab.h
endif

# Install parser header
install-data-hook:
	$(MKDIR_P) $(DESTDIR)$(puppetcincludedir)
	cp $(top_srcdir)/include/puppet.tab.h $(DESTDIR)$(puppetcincludedir)/ || true
