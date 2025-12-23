SUBDIRS = common src facter server agent tests

# Additional files to distribute
EXTRA_DIST = README.md

# Clean up generated files
MAINTAINERCLEANFILES = \
    aclocal.m4 \
    configure \
    Makefile.in \
    src/Makefile.in \
    tests/Makefile.in \
    install-sh \
    missing \
    compile \
    depcomp

.PHONY: check-syntax
check-syntax:
	@echo "Running syntax checks..."
	@for file in src/*.c src/*.h; do \
		echo "Checking $$file..."; \
		$(CC) $(CFLAGS) $(CPPFLAGS) -fsyntax-only $$file || exit 1; \
	done
	@echo "All syntax checks passed."