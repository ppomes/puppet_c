# libpuppetc_common - Shared utilities
lib_LTLIBRARIES = libpuppetc_common.la

libpuppetc_common_la_SOURCES = \
	puppet_memory.c \
	puppet_json.c \
	exec_utils.c \
	file_utils.c \
	string_utils.c \
	color_output.c \
	config_parser.c

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
	$(top_srcdir)/include/puppet_json_common.h \
	$(top_srcdir)/include/exec_utils.h \
	$(top_srcdir)/include/file_utils.h \
	$(top_srcdir)/include/string_utils.h \
	$(top_srcdir)/include/color_output.h \
	$(top_srcdir)/include/config_parser.h
