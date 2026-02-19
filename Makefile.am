SUBDIRS = common facter compiler
if BUILD_SERVER
SUBDIRS += server
endif
if BUILD_AGENT
SUBDIRS += agent
endif
SUBDIRS += tests

# Man pages
dist_man1_MANS = \
    man/man1/puppetc-compile.1 \
    man/man1/puppetc-server.1 \
    man/man1/puppetc-agent.1 \
    man/man1/puppetc-ca.1 \
    man/man1/facter_c.1

dist_man5_MANS = \
    man/man5/puppet.conf.5

# Additional files to distribute
EXTRA_DIST = README.md scripts/dump_puppetdb_facts.py

# Clean up generated files
MAINTAINERCLEANFILES = \
    aclocal.m4 \
    configure \
    Makefile.in \
    compiler/Makefile.in \
    tests/Makefile.in \
    install-sh \
    missing \
    compile \
    depcomp

.PHONY: check-syntax
check-syntax:
	@echo "Running syntax checks..."
	@for file in compiler/*.c compiler/*.h; do \
		echo "Checking $$file..."; \
		$(CC) $(CFLAGS) $(CPPFLAGS) -fsyntax-only $$file || exit 1; \
	done
	@echo "All syntax checks passed."

.PHONY: integration-test
integration-test:
	@echo "Running integration tests..."
	@./tests/integration/run_tests.sh