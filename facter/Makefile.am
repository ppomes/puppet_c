# libfacter_c - Native fact collection library
lib_LTLIBRARIES = libfacter_c.la

libfacter_c_la_SOURCES = facter.c

# Include directories
AM_CPPFLAGS = -I$(top_srcdir)/include

# Compiler flags
AM_CFLAGS = -Wall -Wextra -g

# Library versioning (current:revision:age)
libfacter_c_la_LDFLAGS = -version-info 0:0:0

# Library dependencies
libfacter_c_la_LIBADD = $(top_builddir)/common/libpuppetc_common.la

# Header files to install
puppetcincludedir = $(includedir)/puppetc
puppetcinclude_HEADERS = $(top_srcdir)/include/facter.h

# Also build a standalone facter command
bin_PROGRAMS = facter_c

facter_c_SOURCES = facter_main.c
facter_c_LDADD = libfacter_c.la
