# Test data files
EXTRA_DIST = \
    puppet/apache_basic.pp \
    puppet/apache_demo.pp \
    puppet/apache_simple.pp \
    puppet/apache_vhost.erb \
    puppet/basic.pp \
    puppet/class_with_resource.pp \
    puppet/class.pp \
    puppet/complex.pp \
    puppet/erb_test.pp \
    puppet/eval_test.pp \
    puppet/expr_test.pp \
    puppet/package_management.pp \
    puppet/resource.pp \
    puppet/simple_no_attrs.pp \
    puppet/simple_resource.pp \
    puppet/simple.pp \
    puppet/single_attr.pp \
    puppet/site.pp \
    puppet/system_resources.pp \
    puppet/template_test.pp

# Test output directory
check-local:
	@echo "Running parser tests..."
	@$(MKDIR_P) output
	@total=0; passed=0; \
	for file in $(srcdir)/puppet/*.pp; do \
		printf "Testing %s... " "$$(basename $$file)"; \
		if $(top_builddir)/src/puppetc $$file > output/$$(basename $$file).out 2>&1; then \
			echo "✓ PASS"; \
			passed=$$((passed + 1)); \
		else \
			echo "✗ FAIL"; \
			echo "  Error output:"; \
			sed 's/^/    /' output/$$(basename $$file).out; \
		fi; \
		total=$$((total + 1)); \
	done; \
	echo; \
	echo "Test Results: $$passed/$$total tests passed"; \
	if [ $$passed -eq $$total ]; then \
		echo "🎉 All tests passed!"; \
		exit 0; \
	else \
		echo "❌ Some tests failed. Check output/ for details."; \
		exit 1; \
	fi

# Clean test outputs
clean-local:
	-rm -rf output

# Create test output directory
$(builddir)/output:
	$(MKDIR_P) $@