# puppetc-server - HTTP API for catalog compilation
# puppetc-ca - Certificate Authority management CLI
bin_PROGRAMS = puppetc-server puppetc-ca

puppetc_server_SOURCES = puppetc_server.c \
                         puppet_ca.c \
                         puppet_autosign.c

puppetc_ca_SOURCES = puppetc_ca_cli.c \
                     puppet_ca.c \
                     puppet_autosign.c

# Link against libpuppetc and libpuppetc_common
puppetc_server_LDADD = $(top_builddir)/compiler/libpuppetc.la $(top_builddir)/common/libpuppetc_common.la $(MHD_LIBS) $(YAML_LIBS) $(OPENSSL_LIBS) $(SQLITE_LIBS) $(RUBY_LIBS)
puppetc_ca_LDADD = $(top_builddir)/compiler/libpuppetc.la $(top_builddir)/common/libpuppetc_common.la $(YAML_LIBS) $(OPENSSL_LIBS) $(RUBY_LIBS)

# Include directories
AM_CPPFLAGS = -I$(top_srcdir)/include $(MHD_CFLAGS) $(SQLITE_CFLAGS)

# Compiler flags
AM_CFLAGS = -Wall -Wextra -g $(YAML_CFLAGS) $(OPENSSL_CFLAGS) $(RUBY_CFLAGS)
