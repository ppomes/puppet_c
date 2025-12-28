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
    puppet/template_test.pp \
    puppet/interpolation_test.pp \
    puppet/resource_title_interpolation.pp \
    puppet/resource_title_facts.pp \
    puppet/duplicate_resource_test.pp \
    puppet/duplicate_multinode_test.pp \
    puppet/single_line_template_test.pp \
    puppet/erb_template_test.pp \
    puppet/apache_vhost_template.erb \
    facts/sample_facts.json \
    facts/multi_node_facts.json \
    facts/test_facts_usage.pp \
    facts/test_single_quotes.pp \
    facts/simple_test.pp \
    puppet/hiera_lookup_test.pp \
    puppet/hiera_complex_test.pp \
    puppet/hiera_access_test.pp \
    puppet/split_test.pp \
    puppet/join_test.pp \
    puppet/string_functions_test.pp \
    puppet/inspection_functions_test.pp \
    puppet/iterator_test.pp \
    puppet/collector_test.pp \
    data/common.yaml \
    test_iterators.sh \
    test_collectors.sh

# Test output directory
check-local: check-parser check-facts check-core-functions check-hiera check-iterators check-collectors
	@echo "All tests completed."

check-parser:
	@echo "Running parser tests..."
	@$(MKDIR_P) output
	@total=0; passed=0; \
	for file in $(srcdir)/puppet/*.pp; do \
		printf "Testing %s... " "$$(basename $$file)"; \
		if $(top_builddir)/compiler/puppetc-compile $$file > output/$$(basename $$file).out 2>&1; then \
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
	echo "Parser Test Results: $$passed/$$total tests passed"; \
	if [ $$passed -eq $$total ]; then \
		echo "✅ All parser tests passed!"; \
	else \
		echo "❌ Some parser tests failed. Check output/ for details."; \
		exit 1; \
	fi

check-facts:
	@echo "Running facts system tests..."
	@$(MKDIR_P) output
	@total=0; passed=0; \
	echo "  Testing single-node facts..."; \
	if $(top_builddir)/compiler/puppetc-compile --eval --facts $(srcdir)/facts/sample_facts.json \
	   $(srcdir)/facts/test_facts_usage.pp > output/facts_single.out 2>&1; then \
		echo "    ✓ Single-node facts test passed"; \
		passed=$$((passed + 1)); \
	else \
		echo "    ✗ Single-node facts test failed"; \
		sed 's/^/      /' output/facts_single.out; \
	fi; \
	total=$$((total + 1)); \
	echo "  Testing multi-node facts..."; \
	if $(top_builddir)/compiler/puppetc-compile --eval --all-nodes --facts $(srcdir)/facts/multi_node_facts.json \
	   $(srcdir)/facts/test_single_quotes.pp > output/facts_multi.out 2>&1; then \
		echo "    ✓ Multi-node facts test passed"; \
		passed=$$((passed + 1)); \
	else \
		echo "    ✗ Multi-node facts test failed"; \
		sed 's/^/      /' output/facts_multi.out; \
	fi; \
	total=$$((total + 1)); \
	echo "  Testing facts with default node..."; \
	if $(top_builddir)/compiler/puppetc-compile --eval --facts $(srcdir)/facts/multi_node_facts.json \
	   $(srcdir)/facts/simple_test.pp > output/facts_default.out 2>&1; then \
		echo "    ✓ Default node facts test passed"; \
		passed=$$((passed + 1)); \
	else \
		echo "    ✗ Default node facts test failed"; \
		sed 's/^/      /' output/facts_default.out; \
	fi; \
	total=$$((total + 1)); \
	echo "  Testing string interpolation with facts..."; \
	if $(top_builddir)/compiler/puppetc-compile --eval --facts $(srcdir)/facts/sample_facts.json \
	   $(srcdir)/puppet/interpolation_test.pp > output/interpolation.out 2>&1; then \
		echo "    ✓ String interpolation test passed"; \
		passed=$$((passed + 1)); \
	else \
		echo "    ✗ String interpolation test failed"; \
		sed 's/^/      /' output/interpolation.out; \
	fi; \
	total=$$((total + 1)); \
	echo; \
	echo "Facts Test Results: $$passed/$$total tests passed"; \
	if [ $$passed -eq $$total ]; then \
		echo "✅ All facts tests passed!"; \
	else \
		echo "❌ Some facts tests failed. Check output/ for details."; \
		exit 1; \
	fi

check-core-functions:
	@echo "Running core function tests..."
	@$(MKDIR_P) output
	@echo "  Testing logging functions (notice, info, warning, debug)..."
	@if $(top_builddir)/compiler/puppetc-compile --eval $(srcdir)/puppet/simple_core_test.pp > output/core_functions.out 2>&1; then \
		if grep -q "Notice: Test message" output/core_functions.out && \
		   grep -q "Info: Info test" output/core_functions.out && \
		   grep -q "Warning: Warning test" output/core_functions.out && \
		   grep -q "Debug: Debug test" output/core_functions.out; then \
			echo "    ✓ Logging functions test passed"; \
		else \
			echo "    ✗ Logging functions test failed - missing expected output"; \
			sed 's/^/      /' output/core_functions.out; \
			exit 1; \
		fi; \
	else \
		echo "    ✗ Core functions test failed - execution error"; \
		sed 's/^/      /' output/core_functions.out; \
		exit 1; \
	fi
	@echo "  Testing fail() function..."
	@echo 'fail("Test failure message")' > output/fail_test.pp
	@if $(top_builddir)/compiler/puppetc-compile --eval output/fail_test.pp > output/fail_test.out 2>&1; then \
		echo "    ✗ fail() function should have caused failure but didn't"; \
		cat output/fail_test.out; \
		exit 1; \
	else \
		if grep -q "Critical: Test failure message" output/fail_test.out; then \
			echo "    ✓ fail() function test passed"; \
		else \
			echo "    ✗ fail() function test failed - no critical message"; \
			sed 's/^/      /' output/fail_test.out; \
			exit 1; \
		fi; \
	fi
	@echo "  Testing function call statements..."
	@if $(top_builddir)/compiler/puppetc-compile --eval $(srcdir)/puppet/notice_statement_test.pp > output/statement_test.out 2>&1; then \
		if grep -q "Notice: This should appear during execution" output/statement_test.out; then \
			echo "    ✓ Function call statements test passed"; \
		else \
			echo "    ✗ Function call statements test failed"; \
			sed 's/^/      /' output/statement_test.out; \
			exit 1; \
		fi; \
	else \
		echo "    ✗ Function call statements test failed"; \
		sed 's/^/      /' output/statement_test.out; \
		exit 1; \
	fi
	@echo "  Testing split() function..."
	@if $(top_builddir)/compiler/puppetc-compile --eval --verbose $(srcdir)/puppet/split_test.pp > output/split_test.out 2>&1; then \
		if grep -q "split() function tests completed" output/split_test.out && \
		   grep -q 'Set \$$parts = (complex value)' output/split_test.out && \
		   grep -q 'Set \$$path = (complex value)' output/split_test.out && \
		   grep -q 'Set \$$words = (complex value)' output/split_test.out; then \
			echo "    ✓ split() function test passed"; \
		else \
			echo "    ✗ split() function test failed - missing expected output"; \
			sed 's/^/      /' output/split_test.out; \
			exit 1; \
		fi; \
	else \
		echo "    ✗ split() function test failed - execution error"; \
		sed 's/^/      /' output/split_test.out; \
		exit 1; \
	fi
	@echo "  Testing join() function..."
	@if $(top_builddir)/compiler/puppetc-compile --eval --verbose $(srcdir)/puppet/join_test.pp > output/join_test.out 2>&1; then \
		if grep -q "join() function tests completed" output/join_test.out && \
		   grep -q 'Joined with colon: a:b:c' output/join_test.out && \
		   grep -q 'Joined with dash: one - two - three' output/join_test.out && \
		   grep -q 'Round-trip: x:y:z' output/join_test.out; then \
			echo "    ✓ join() function test passed"; \
		else \
			echo "    ✗ join() function test failed - missing expected output"; \
			sed 's/^/      /' output/join_test.out; \
			exit 1; \
		fi; \
	else \
		echo "    ✗ join() function test failed - execution error"; \
		sed 's/^/      /' output/join_test.out; \
		exit 1; \
	fi
	@echo "✅ All core function tests passed!"

check-hiera:
	@echo "Running Hiera evaluation tests..."
	@$(MKDIR_P) output
	@total=0; passed=0; \
	echo "  Testing basic Hiera lookup functionality..."; \
	if $(top_builddir)/compiler/puppetc-compile --eval --hiera-data $(srcdir)/data \
	   $(srcdir)/puppet/hiera_lookup_test.pp > output/hiera_lookup.out 2>&1; then \
		if grep -q "Hiera message:" output/hiera_lookup.out && \
		   grep -q "Unknown key with default: default_value" output/hiera_lookup.out && \
		   grep -q "Hiera number: 42" output/hiera_lookup.out && \
		   grep -q "Hiera boolean: true" output/hiera_lookup.out; then \
			echo "    ✓ Basic Hiera lookup test passed"; \
			passed=$$((passed + 1)); \
		else \
			echo "    ✗ Basic Hiera lookup test failed - missing expected output"; \
			sed 's/^/      /' output/hiera_lookup.out; \
		fi; \
	else \
		echo "    ✗ Basic Hiera lookup test failed - execution error"; \
		sed 's/^/      /' output/hiera_lookup.out; \
	fi; \
	total=$$((total + 1)); \
	echo "  Testing complex Hiera data structures..."; \
	if $(top_builddir)/compiler/puppetc-compile --eval --hiera-data $(srcdir)/data \
	   $(srcdir)/puppet/hiera_complex_test.pp > output/hiera_complex.out 2>&1; then \
		if grep -q "Array from Hiera:" output/hiera_complex.out && \
		   grep -q "Hash from Hiera:" output/hiera_complex.out && \
		   grep -q "Apache config from Hiera:" output/hiera_complex.out; then \
			echo "    ✓ Complex Hiera data test passed"; \
			passed=$$((passed + 1)); \
		else \
			echo "    ✗ Complex Hiera data test failed - missing expected output"; \
			sed 's/^/      /' output/hiera_complex.out; \
		fi; \
	else \
		echo "    ✗ Complex Hiera data test failed - execution error"; \
		sed 's/^/      /' output/hiera_complex.out; \
	fi; \
	total=$$((total + 1)); \
	echo "  Testing Hiera data access..."; \
	if $(top_builddir)/compiler/puppetc-compile --eval --hiera-data $(srcdir)/data \
	   $(srcdir)/puppet/hiera_access_test.pp > output/hiera_access.out 2>&1; then \
		if grep -q "Apache data loaded successfully" output/hiera_access.out && \
		   grep -q "Simple value still works:" output/hiera_access.out; then \
			echo "    ✓ Hiera data access test passed"; \
			passed=$$((passed + 1)); \
		else \
			echo "    ✗ Hiera data access test failed - missing expected output"; \
			sed 's/^/      /' output/hiera_access.out; \
		fi; \
	else \
		echo "    ✗ Hiera data access test failed - execution error"; \
		sed 's/^/      /' output/hiera_access.out; \
	fi; \
	total=$$((total + 1)); \
	echo; \
	echo "Hiera Test Results: $$passed/$$total tests passed"; \
	if [ $$passed -eq $$total ]; then \
		echo "✅ All Hiera tests passed!"; \
	else \
		echo "❌ Some Hiera tests failed. Check output/ for details."; \
		exit 1; \
	fi

# Memory leak testing with Valgrind
check-memory: $(top_builddir)/compiler/puppetc-compile
	@echo "Running memory leak tests with Valgrind..."
	@$(MKDIR_P) output
	@if command -v valgrind >/dev/null 2>&1; then \
		echo "Valgrind found, running memory tests..."; \
		total=0; passed=0; \
		for file in $(srcdir)/puppet/basic.pp $(srcdir)/puppet/class.pp $(srcdir)/puppet/simple.pp; do \
			printf "Memory testing %s... " "$$(basename $$file)"; \
			if valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all \
			   --error-exitcode=1 --quiet \
			   $(top_builddir)/compiler/puppetc-compile $$file > output/$$(basename $$file).valgrind 2>&1; then \
				echo "✓ PASS"; \
				passed=$$((passed + 1)); \
			else \
				echo "✗ FAIL"; \
				echo "  Valgrind output:"; \
				sed 's/^/    /' output/$$(basename $$file).valgrind; \
			fi; \
			total=$$((total + 1)); \
		done; \
		echo; \
		echo "Memory Test Results: $$passed/$$total tests passed"; \
	else \
		echo "Valgrind not found, skipping memory tests"; \
		echo "Install valgrind with: apt-get install valgrind"; \
	fi

# Test with address sanitizer
check-asan: 
	@echo "To run with AddressSanitizer, rebuild with:"
	@echo "  ./configure CFLAGS='-fsanitize=address -g'"
	@echo "  make clean && make"
	@echo "  make check"

# Clean test outputs
clean-local:
	-rm -rf output

# Create test output directory
$(builddir)/output:
	$(MKDIR_P) $@

check-iterators:
	@echo "Running iterator function tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_iterators.sh > output/iterators.out 2>&1; then \
		echo "✅ All iterator tests passed!"; \
	else \
		echo "❌ Some iterator tests failed:"; \
		sed 's/^/  /' output/iterators.out; \
		exit 1; \
	fi

check-collectors:
	@echo "Running virtual resource collector tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_collectors.sh > output/collectors.out 2>&1; then \
		echo "✅ All collector tests passed!"; \
	else \
		echo "❌ Some collector tests failed:"; \
		sed 's/^/  /' output/collectors.out; \
		exit 1; \
	fi

.PHONY: check-parser check-facts check-core-functions check-hiera check-iterators check-collectors check-memory check-asan