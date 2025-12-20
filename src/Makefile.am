bin_PROGRAMS = puppetc

# Generated parser files
BUILT_SOURCES = puppet.tab.c puppet.tab.h lex.yy.c
CLEANFILES = $(BUILT_SOURCES)

# Source files
puppetc_SOURCES = \
    main.c \
    puppet_ast.c \
    puppet_interpreter.c \
    puppet_json.c \
    puppet_json_parser.c \
    puppet_loader.c \
    puppet_memory.c \
    puppet_stdlib.c \
    $(BUILT_SOURCES)

# Conditionally add ERB support
if HAVE_RUBY
puppetc_SOURCES += puppet_erb.c
endif

# Conditionally add Hiera support
if HAVE_HIERA
puppetc_SOURCES += puppet_hiera.c
endif

# Include directories
AM_CPPFLAGS = -I$(top_srcdir)/include

# Compiler flags
AM_CFLAGS = -Wall -Wextra -g

# Conditional Ruby flags
if HAVE_RUBY
AM_CFLAGS += $(RUBY_CFLAGS) -DHAVE_RUBY
puppetc_LDADD = $(RUBY_LIBS) -lm -lpthread
else
AM_CFLAGS += -DDISABLE_RUBY
puppetc_LDADD = -lpthread
endif

# Conditional Hiera/YAML flags
if HAVE_HIERA
AM_CFLAGS += $(YAML_CFLAGS) -DHAVE_YAML
puppetc_LDADD += $(YAML_LIBS)
endif

# Header files to install
include_HEADERS = \
    $(top_srcdir)/include/puppet_ast.h \
    $(top_srcdir)/include/puppet_interpreter.h \
    $(top_srcdir)/include/puppet_json.h \
    $(top_srcdir)/include/puppet_json_parser.h \
    $(top_srcdir)/include/puppet_loader.h \
    $(top_srcdir)/include/puppet_memory.h \
    $(top_srcdir)/include/puppet_stdlib.h

if HAVE_RUBY
include_HEADERS += $(top_srcdir)/include/puppet_erb.h
endif

if HAVE_HIERA
include_HEADERS += $(top_srcdir)/include/puppet_hiera.h
endif

# Flex/Bison rules
lex.yy.c: puppet.l puppet.tab.h
	$(LEX) -o $@ $<

puppet.tab.c puppet.tab.h: puppet.y
	$(YACC) -d -o puppet.tab.c $<
	cp puppet.tab.h $(top_srcdir)/include/

# Ensure proper build order
puppet_ast.$(OBJEXT): puppet.tab.h
puppet_interpreter.$(OBJEXT): puppet.tab.h
puppet_json.$(OBJEXT): puppet.tab.h
puppet_loader.$(OBJEXT): puppet.tab.h
puppet_stdlib.$(OBJEXT): puppet.tab.h
main.$(OBJEXT): puppet.tab.h
if HAVE_RUBY
puppet_erb.$(OBJEXT): puppet.tab.h
endif
if HAVE_HIERA
puppet_hiera.$(OBJEXT): puppet.tab.h
endif

# Install additional files
install-data-hook:
	$(MKDIR_P) $(DESTDIR)$(includedir)
	cp $(top_srcdir)/include/puppet.tab.h $(DESTDIR)$(includedir)/ || true