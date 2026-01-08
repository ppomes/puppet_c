# puppetc-agent - Puppet agent client
bin_PROGRAMS = puppetc-agent

puppetc_agent_SOURCES = \
	puppetc_agent.c \
	puppet_provider.c \
	puppet_agent_ruby.c \
	puppet_pluginsync.c \
	provider_file.c \
	provider_package.c \
	provider_service.c \
	provider_notify.c \
	provider_exec.c \
	provider_cron.c \
	provider_host.c \
	provider_group.c \
	provider_user.c \
	provider_sysctl.c \
	provider_mount.c \
	provider_anchor.c

# Link against libfacter_c, libpuppetc_common, libcurl, OpenSSL and Ruby
puppetc_agent_LDADD = $(top_builddir)/facter/libfacter_c.la $(top_builddir)/common/libpuppetc_common.la $(CURL_LIBS) $(RUBY_LIBS) -lssl -lcrypto -lpthread

# Include directories (includes Ruby headers)
AM_CPPFLAGS = -I$(top_srcdir)/include $(CURL_CFLAGS) $(RUBY_CFLAGS)

# Compiler flags
AM_CFLAGS = -Wall -Wextra -g
