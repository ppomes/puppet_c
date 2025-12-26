# puppetc-agent - Puppet agent client
bin_PROGRAMS = puppetc-agent

puppetc_agent_SOURCES = \
	puppetc_agent.c \
	puppet_provider.c \
	provider_file.c \
	provider_package.c \
	provider_service.c \
	provider_notify.c \
	provider_exec.c \
	provider_cron.c \
	provider_host.c \
	provider_group.c \
	provider_user.c \
	provider_sysctl.c

# Link against libfacter_c, libpuppetc_common, and libcurl
puppetc_agent_LDADD = $(top_builddir)/facter/libfacter_c.la $(top_builddir)/common/libpuppetc_common.la $(CURL_LIBS)

# Include directories
AM_CPPFLAGS = -I$(top_srcdir)/include $(CURL_CFLAGS)

# Compiler flags
AM_CFLAGS = -Wall -Wextra -g
