# Test data files
EXTRA_DIST = \
    puppet/apache_basic.pp \
    puppet/apache_demo.pp \
    puppet/apache_simple.pp \
    puppet/apache_vhost.erb \
    puppet/apache_vhost_template.erb \
    puppet/array_functions2_test.pp \
    puppet/array_functions_test.pp \
    puppet/basic.pp \
    puppet/class.pp \
    puppet/class_with_resource.pp \
    puppet/collector_test.pp \
    puppet/comment_in_expr_test.pp \
    puppet/complex.pp \
    puppet/conditionals_test.pp \
    puppet/core_functions_test.pp \
    puppet/crypto_functions_test.pp \
    puppet/duplicate_multinode_test.pp \
    puppet/duplicate_resource_test.pp \
    puppet/erb_template_test.pp \
    puppet/erb_test.pp \
    puppet/eval_test.pp \
    puppet/exec_test.pp \
    puppet/expr_test.pp \
    puppet/hash_functions_test.pp \
    puppet/hiera_access_test.pp \
    puppet/hiera_complex_test.pp \
    puppet/hiera_lookup_test.pp \
    puppet/inspection_functions_test.pp \
    puppet/interpolation_facts_test.pp \
    puppet/interpolation_test.pp \
    puppet/iterator_test.pp \
    puppet/join_test.pp \
    puppet/lookup_functions_test.pp \
    puppet/math_functions2_test.pp \
    puppet/math_functions_test.pp \
    puppet/nested_define_test.pp \
    puppet/node_test.pp \
    puppet/notice_statement_test.pp \
    puppet/package_management.pp \
    puppet/path_functions_test.pp \
    puppet/range_test.pp \
    puppet/regex_functions_test.pp \
    puppet/resource.pp \
    puppet/resource_title_facts.pp \
    puppet/resource_title_interpolation.pp \
    puppet/simple.pp \
    puppet/simple_core_test.pp \
    puppet/simple_erb_test.pp \
    puppet/simple_no_attrs.pp \
    puppet/simple_resource.pp \
    puppet/single_attr.pp \
    puppet/single_line_template_test.pp \
    puppet/site.pp \
    puppet/split_test.pp \
    puppet/string_functions2_test.pp \
    puppet/string_functions_test.pp \
    puppet/system_resources.pp \
    puppet/template_test.pp \
    puppet/test_example_key.pp \
    puppet/type_checking_test.pp \
    puppet/virtual_nested_define_test.pp \
    puppet/virtual_test.pp \
    puppet/epp_test.pp \
    puppet/simple.epp \
    puppet/merge_operations_test.pp \
    puppet/regex_case_test.pp \
    puppet/sensitive_test.pp \
    puppet/splat_test.pp \
    puppet/chained_access_test.pp \
    puppet/binary_expr_test.pp \
    puppet/chain_test.pp \
    puppet/append_test.pp \
    puppet/tag_test.pp \
    puppet/virtual_override_test.pp \
    facts/sample_facts.json \
    facts/multi_node_facts.json \
    facts/test_facts_usage.pp \
    facts/test_single_quotes.pp \
    facts/simple_test.pp \
    data/common.yaml \
    test_iterators.sh \
    test_collectors.sh \
    test_virtual.sh \
    test_simple_core.sh \
    test_erb.sh \
    test_epp.sh \
    test_features.sh \
    test_chains.sh \
    test_append.sh \
    test_tags.sh \
    test_virtual_override.sh \
    test_parallel_consistency.sh \
    test_multinode_isolation.sh \
    test_class_loader.sh \
    test_hiera_advanced.sh \
    test_catalog_refs.sh \
    test_source_url.sh \
    test_unknown_type.sh \
    test_template_fidelity.sh \
    test_puppet8_lint.sh \
    test_user_functions.sh \
    test_method_dispatch.sh \
    test_multi_title_refs.sh \
    test_strict_undef_index.sh \
    test_function_argtypes.sh \
    test_strict_erb.sh \
    test_ruby_kwarg_shorthand.sh \
    facts/multinode_topvar_facts.yaml \
    facts/multinode_isolation_facts.yaml \
    puppet/multinode_topvar.pp \
    puppet/multinode_isolation.pp \
    puppet/loader_includes.pp \
    puppet/refs_ok.pp \
    puppet/refs_missing_resource.pp \
    puppet/refs_missing_class.pp \
    puppet/source_ok.pp \
    puppet/source_missing.pp \
    puppet/erbtest_site.pp \
    data/erbtest.cfg.expected \
    hieratest/hiera.yaml \
    hieratest/allfacts.yaml \
    hieratest/manifests/site.pp \
    hieratest/hieralocal/global.yaml \
    hieratest/hieralocal/mymod/global.yaml \
    hieratest/hieralocal/mymod/prod.yaml \
    hieratest/modules/mymod/manifests/init.pp \
    modules/loadertest/manifests/init.pp \
    modules/loadertest/manifests/sub.pp \
    modules/loadertest/lib/puppet/type/loadertest_type.rb \
    modules/srctest/files/data.txt \
    modules/srctest/files/sub/inner.txt \
    modules/erbtest/manifests/init.pp \
    modules/erbtest/templates/config.cfg.erb \
    valgrind.supp \
    modules/epp_test/templates/trim_basic.epp \
    modules/epp_test/templates/trim_newlines.epp \
    modules/epp_test/templates/if_elsif_else.epp \
    modules/epp_test/templates/conditional_each.epp \
    modules/epp_test/templates/comments.epp \
    modules/epp_test/templates/ini_section.epp

# Test output directory
check-local: check-parser check-nodes check-facts check-core-functions check-hiera check-iterators check-collectors check-virtual check-simple-core check-erb check-epp check-features check-chains check-append check-tags check-virtual-override check-parallel-consistency check-multinode-isolation check-class-loader check-hiera-advanced check-catalog-refs check-source-url check-unknown-type check-template-fidelity check-puppet8-lint check-user-functions check-method-dispatch check-multi-title-refs check-strict-undef-index check-function-argtypes check-strict-erb check-ruby-kwarg-shorthand
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

check-nodes:
	@echo "Running node definition tests..."
	@$(MKDIR_P) output
	@echo "  Testing node default block..."
	@if $(top_builddir)/compiler/puppetc-compile --eval --verbose $(srcdir)/puppet/node_test.pp > output/node_test.out 2>&1; then \
		if grep -q "NODE_TEST_WORKS" output/node_test.out; then \
			echo "    ✓ Node definition test passed"; \
		else \
			echo "    ✗ Node definition test failed - resource not found in output"; \
			echo "    Expected 'NODE_TEST_WORKS' in output but got:"; \
			sed 's/^/      /' output/node_test.out; \
			exit 1; \
		fi; \
	else \
		echo "    ✗ Node definition test failed - execution error"; \
		sed 's/^/      /' output/node_test.out; \
		exit 1; \
	fi
	@echo "✅ All node definition tests passed!"

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
	@if $(top_builddir)/compiler/puppetc-compile --eval --verbose $(srcdir)/puppet/simple_core_test.pp > output/core_functions.out 2>&1; then \
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

# Comprehensive memory check across the curated test fixtures.
# Linux-only (Valgrind doesn't ship on macOS or BSDs without effort).
# Not part of the default `make check` since each run is slow.
# Invoke with `make check-valgrind`.
check-valgrind: $(top_builddir)/compiler/puppetc-compile
	@echo "Running comprehensive memory checks with Valgrind..."
	@$(MKDIR_P) output
	@if [ "$$(uname -s)" != "Linux" ]; then \
		echo "  check-valgrind is Linux-only — skipping on $$(uname -s)"; \
		exit 0; \
	fi; \
	if ! command -v valgrind >/dev/null 2>&1; then \
		echo "  valgrind not found. Install with: apt-get install valgrind"; \
		exit 1; \
	fi; \
	supp="$(srcdir)/valgrind.supp"; \
	vg="valgrind --tool=memcheck --leak-check=full \
	    --errors-for-leak-kinds=definite --suppressions=$$supp \
	    --error-exitcode=1 --quiet --num-callers=20"; \
	total=0; passed=0; failed=0; \
	run_case () { \
	    label="$$1"; shift; \
	    out="output/valgrind_$$label.out"; \
	    printf "  %-40s " "$$label"; \
	    if $$vg "$$@" > "$$out" 2>&1; then \
	        echo "✓"; passed=$$((passed + 1)); \
	    else \
	        echo "✗"; failed=$$((failed + 1)); \
	        sed 's/^/      /' "$$out" | tail -30; \
	    fi; \
	    total=$$((total + 1)); \
	}; \
	C="$(top_builddir)/compiler/puppetc-compile"; \
	S="$(srcdir)"; \
	run_case parser-only          "$$C" -j "$$S/puppet/basic.pp"; \
	run_case eval-default          "$$C" -e "$$S/puppet/class.pp"; \
	run_case multinode-parallel    "$$C" -s -a -P -f "$$S/facts/multinode_topvar_facts.yaml" \
	                                  "$$S/puppet/multinode_topvar.pp"; \
	run_case multinode-sequential  "$$C" -s -a -f "$$S/facts/multinode_isolation_facts.yaml" \
	                                  "$$S/puppet/multinode_isolation.pp"; \
	run_case hiera-classes         "$$C" -p -n prod-x.example.com \
	                                  -f "$$S/hieratest/allfacts.yaml" \
	                                  -m "$$S/hieratest/modules" \
	                                  -D "$$S/hieratest/hieralocal" \
	                                  "$$S/hieratest/manifests/site.pp"; \
	run_case template-render       "$$C" -e -n test.example.com \
	                                  -t /tmp/erbtest.cfg -m "$$S/modules" \
	                                  "$$S/puppet/erbtest_site.pp"; \
	run_case validate-refs-ok      "$$C" -s "$$S/puppet/refs_ok.pp"; \
	run_case unknown-type-ok       "$$C" -s -m "$$S/modules" "$$S/puppet/loader_includes.pp"; \
	echo; \
	echo "Valgrind results: $$passed passed, $$failed failed (of $$total)"; \
	test "$$failed" = 0

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

check-virtual:
	@echo "Running virtual resource realization tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_virtual.sh > output/virtual.out 2>&1; then \
		echo "✅ All virtual resource tests passed!"; \
	else \
		echo "❌ Some virtual resource tests failed:"; \
		sed 's/^/  /' output/virtual.out; \
		exit 1; \
	fi

check-simple-core:
	@echo "Running simple core function tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_simple_core.sh > output/simple_core.out 2>&1; then \
		echo "✅ All simple core tests passed!"; \
	else \
		echo "❌ Some simple core tests failed:"; \
		sed 's/^/  /' output/simple_core.out; \
		exit 1; \
	fi

check-erb:
	@echo "Running ERB template tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_erb.sh > output/erb.out 2>&1; then \
		echo "✅ All ERB tests passed!"; \
	else \
		echo "❌ Some ERB tests failed:"; \
		sed 's/^/  /' output/erb.out; \
		exit 1; \
	fi

check-epp:
	@echo "Running EPP template tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_epp.sh > output/epp.out 2>&1; then \
		echo "✅ All EPP tests passed!"; \
	else \
		echo "❌ Some EPP tests failed:"; \
		sed 's/^/  /' output/epp.out; \
		exit 1; \
	fi

check-features:
	@echo "Running feature tests (EPP, merge ops, regex case, Sensitive)..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_features.sh > output/features.out 2>&1; then \
		echo "✅ All feature tests passed!"; \
	else \
		echo "❌ Some feature tests failed:"; \
		sed 's/^/  /' output/features.out; \
		exit 1; \
	fi

check-chains:
	@echo "Running resource chain tests (-> ~> <- <~)..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_chains.sh > output/chains.out 2>&1; then \
		echo "✅ All resource chain tests passed!"; \
	else \
		echo "❌ Some resource chain tests failed:"; \
		sed 's/^/  /' output/chains.out; \
		exit 1; \
	fi

check-append:
	@echo "Running array/hash append tests (+=)..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_append.sh > output/append.out 2>&1; then \
		echo "✅ All append tests passed!"; \
	else \
		echo "❌ Some append tests failed:"; \
		sed 's/^/  /' output/append.out; \
		exit 1; \
	fi

check-tags:
	@echo "Running tag statement tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_tags.sh > output/tags.out 2>&1; then \
		echo "✅ All tag tests passed!"; \
	else \
		echo "❌ Some tag tests failed:"; \
		sed 's/^/  /' output/tags.out; \
		exit 1; \
	fi

check-virtual-override:
	@echo "Running virtual/exported resource override tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_virtual_override.sh > output/virtual_override.out 2>&1; then \
		echo "✅ All virtual override tests passed!"; \
	else \
		echo "❌ Some virtual override tests failed:"; \
		sed 's/^/  /' output/virtual_override.out; \
		exit 1; \
	fi

check-parallel-consistency:
	@echo "Running parallel/sequential consistency tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_parallel_consistency.sh > output/parallel_consistency.out 2>&1; then \
		echo "✅ All parallel-consistency tests passed!"; \
	else \
		echo "❌ Some parallel-consistency tests failed:"; \
		sed 's/^/  /' output/parallel_consistency.out; \
		exit 1; \
	fi

check-multinode-isolation:
	@echo "Running multi-node isolation tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_multinode_isolation.sh > output/multinode_isolation.out 2>&1; then \
		echo "✅ All multi-node isolation tests passed!"; \
	else \
		echo "❌ Some multi-node isolation tests failed:"; \
		sed 's/^/  /' output/multinode_isolation.out; \
		exit 1; \
	fi

check-class-loader:
	@echo "Running class loader integration tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_class_loader.sh > output/class_loader.out 2>&1; then \
		echo "✅ All class-loader tests passed!"; \
	else \
		echo "❌ Some class-loader tests failed:"; \
		sed 's/^/  /' output/class_loader.out; \
		exit 1; \
	fi

check-hiera-advanced:
	@echo "Running advanced Hiera tests (datadir / module_name / interpolation)..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_hiera_advanced.sh > output/hiera_advanced.out 2>&1; then \
		echo "✅ All hiera-advanced tests passed!"; \
	else \
		echo "❌ Some hiera-advanced tests failed:"; \
		sed 's/^/  /' output/hiera_advanced.out; \
		exit 1; \
	fi

check-catalog-refs:
	@echo "Running catalog ref validation tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_catalog_refs.sh > output/catalog_refs.out 2>&1; then \
		echo "✅ All catalog-refs tests passed!"; \
	else \
		echo "❌ Some catalog-refs tests failed:"; \
		sed 's/^/  /' output/catalog_refs.out; \
		exit 1; \
	fi

check-source-url:
	@echo "Running puppet:/// source URL validation tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_source_url.sh > output/source_url.out 2>&1; then \
		echo "✅ All source-URL tests passed!"; \
	else \
		echo "❌ Some source-URL tests failed:"; \
		sed 's/^/  /' output/source_url.out; \
		exit 1; \
	fi

check-unknown-type:
	@echo "Running unknown resource type detection tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_unknown_type.sh > output/unknown_type.out 2>&1; then \
		echo "✅ All unknown-type tests passed!"; \
	else \
		echo "❌ Some unknown-type tests failed:"; \
		sed 's/^/  /' output/unknown_type.out; \
		exit 1; \
	fi

check-template-fidelity:
	@echo "Running ERB template fidelity tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_template_fidelity.sh > output/template_fidelity.out 2>&1; then \
		echo "✅ All template-fidelity tests passed!"; \
	else \
		echo "❌ Some template-fidelity tests failed:"; \
		sed 's/^/  /' output/template_fidelity.out; \
		exit 1; \
	fi

check-puppet8-lint:
	@echo "Running Puppet 8 legacy-function lint tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_puppet8_lint.sh > output/puppet8_lint.out 2>&1; then \
		echo "✅ All puppet8-lint tests passed!"; \
	else \
		echo "❌ Some puppet8-lint tests failed:"; \
		sed 's/^/  /' output/puppet8_lint.out; \
		exit 1; \
	fi

check-user-functions:
	@echo "Running user-defined function tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_user_functions.sh > output/user_functions.out 2>&1; then \
		echo "✅ All user-function tests passed!"; \
	else \
		echo "❌ Some user-function tests failed:"; \
		sed 's/^/  /' output/user_functions.out; \
		exit 1; \
	fi

check-method-dispatch:
	@echo "Running method-call dispatch tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_method_dispatch.sh > output/method_dispatch.out 2>&1; then \
		echo "✅ All method-dispatch tests passed!"; \
	else \
		echo "❌ Some method-dispatch tests failed:"; \
		sed 's/^/  /' output/method_dispatch.out; \
		exit 1; \
	fi

check-multi-title-refs:
	@echo "Running multi-title resource reference tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_multi_title_refs.sh > output/multi_title_refs.out 2>&1; then \
		echo "✅ All multi-title-refs tests passed!"; \
	else \
		echo "❌ Some multi-title-refs tests failed:"; \
		sed 's/^/  /' output/multi_title_refs.out; \
		exit 1; \
	fi

check-strict-undef-index:
	@echo "Running strict-undef chained-index tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_strict_undef_index.sh > output/strict_undef_index.out 2>&1; then \
		echo "✅ All strict-undef-index tests passed!"; \
	else \
		echo "❌ Some strict-undef-index tests failed:"; \
		sed 's/^/  /' output/strict_undef_index.out; \
		exit 1; \
	fi

check-function-argtypes:
	@echo "Running user-function argument type-check tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_function_argtypes.sh > output/function_argtypes.out 2>&1; then \
		echo "✅ All function-argtypes tests passed!"; \
	else \
		echo "❌ Some function-argtypes tests failed:"; \
		sed 's/^/  /' output/function_argtypes.out; \
		exit 1; \
	fi

check-strict-erb:
	@echo "Running ERB @class::var strict-mode tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_strict_erb.sh > output/strict_erb.out 2>&1; then \
		echo "✅ All strict-erb tests passed!"; \
	else \
		echo "❌ Some strict-erb tests failed:"; \
		sed 's/^/  /' output/strict_erb.out; \
		exit 1; \
	fi

check-ruby-kwarg-shorthand:
	@echo "Running Ruby keyword-arg shorthand scan tests..."
	@$(MKDIR_P) output
	@if $(srcdir)/test_ruby_kwarg_shorthand.sh > output/ruby_kwarg_shorthand.out 2>&1; then \
		echo "✅ All ruby-kwarg-shorthand tests passed!"; \
	else \
		echo "❌ Some ruby-kwarg-shorthand tests failed:"; \
		sed 's/^/  /' output/ruby_kwarg_shorthand.out; \
		exit 1; \
	fi

.PHONY: check-parser check-nodes check-facts check-core-functions check-hiera check-iterators check-collectors check-virtual check-simple-core check-erb check-epp check-features check-chains check-append check-tags check-virtual-override check-memory check-asan check-valgrind check-parallel-consistency check-multinode-isolation check-class-loader check-hiera-advanced check-catalog-refs check-source-url check-unknown-type check-template-fidelity