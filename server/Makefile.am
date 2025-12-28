# puppetc-server - HTTP API for catalog compilation
bin_PROGRAMS = puppetc-server

puppetc_server_SOURCES = puppetc_server.c

# Link against libpuppetc and libpuppetc_common
puppetc_server_LDADD = $(top_builddir)/compiler/libpuppetc.la $(top_builddir)/common/libpuppetc_common.la $(MHD_LIBS) $(YAML_LIBS) $(OPENSSL_LIBS)

# Include directories
AM_CPPFLAGS = -I$(top_srcdir)/include $(MHD_CFLAGS)

# Compiler flags
AM_CFLAGS = -Wall -Wextra -g $(YAML_CFLAGS) $(OPENSSL_CFLAGS)

# Conditional Ruby flags (needed if libpuppetc uses Ruby)
if HAVE_RUBY
AM_CFLAGS += $(RUBY_CFLAGS) -DHAVE_RUBY
puppetc_server_LDADD += $(RUBY_LIBS)
endif
