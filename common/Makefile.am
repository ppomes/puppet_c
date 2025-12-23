# libpuppetc_common - Shared utilities (memory management, JSON)
lib_LTLIBRARIES = libpuppetc_common.la

libpuppetc_common_la_SOURCES = \
	puppet_memory.c \
	puppet_json.c

# Include directories
AM_CPPFLAGS = -I$(top_srcdir)/include

# Compiler flags
AM_CFLAGS = -Wall -Wextra -g

# Library versioning
libpuppetc_common_la_LDFLAGS = -version-info 0:0:0

# Install headers
puppetcincludedir = $(includedir)/puppetc
puppetcinclude_HEADERS = \
	$(top_srcdir)/include/puppet_memory.h \
	$(top_srcdir)/include/puppet_json_common.h
