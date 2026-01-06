#!/bin/bash

# Feature Testing Script
# Tests new features: EPP templates, hash/array merge, regex case, Sensitive

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PUPPETC="$PROJECT_DIR/compiler/puppetc-compile"
OUTPUT_DIR="$SCRIPT_DIR/output"

# Set library path
export LD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:$LD_LIBRARY_PATH"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

echo "Feature Test Suite"
echo "=================="

mkdir -p "$OUTPUT_DIR"

total_tests=0
passed_tests=0

run_test() {
    local test_name="$1"
    local manifest="$2"
    local expected_pattern="$3"

    echo -n "Testing $test_name... "
    total_tests=$((total_tests + 1))

    # Run from project root so relative paths work
    pushd "$PROJECT_DIR" > /dev/null
    output=$("$PUPPETC" --eval "$manifest" 2>&1)
    local result=$?
    popd > /dev/null

    if [ $result -eq 0 ]; then
        if echo "$output" | grep -qF "$expected_pattern"; then
            echo -e "${GREEN}PASS${NC}"
            passed_tests=$((passed_tests + 1))
            echo "$output" > "$OUTPUT_DIR/${test_name}.out"
        else
            echo -e "${RED}FAIL${NC} - Expected pattern not found"
            echo "Expected: $expected_pattern"
            echo "Got:"
            echo "$output" | head -5
        fi
    else
        echo -e "${RED}FAIL${NC} - Command failed"
        echo "$output" | head -5
    fi
}

# Run test with catalog output (-p mode)
run_catalog_test() {
    local test_name="$1"
    local manifest="$2"
    local expected_pattern="$3"

    echo -n "Testing $test_name... "
    total_tests=$((total_tests + 1))

    pushd "$PROJECT_DIR" > /dev/null
    output=$("$PUPPETC" -p "$manifest" 2>&1)
    local result=$?
    popd > /dev/null

    if [ $result -eq 0 ]; then
        if echo "$output" | grep -qF "$expected_pattern"; then
            echo -e "${GREEN}PASS${NC}"
            passed_tests=$((passed_tests + 1))
            echo "$output" > "$OUTPUT_DIR/${test_name}.out"
        else
            echo -e "${RED}FAIL${NC} - Expected pattern not found"
            echo "Expected: $expected_pattern"
            echo "Got:"
            echo "$output" | head -10
        fi
    else
        echo -e "${RED}FAIL${NC} - Command failed"
        echo "$output" | head -5
    fi
}

echo ""
echo "=== EPP Template Tests ==="
run_test "epp_basic" "$SCRIPT_DIR/puppet/epp_test.pp" "EPP tests completed"
run_test "epp_params" "$SCRIPT_DIR/puppet/epp_test.pp" "Name: myapp"
run_test "epp_deferrable" "$SCRIPT_DIR/puppet/epp_test.pp" "Deferrable EPP:"

echo ""
echo "=== Hash/Array Merge Tests ==="
run_test "hash_merge" "$SCRIPT_DIR/puppet/merge_operations_test.pp" "Hash merge: {a => 1, b => 2, c => 3, d => 4}"
run_test "hash_override" "$SCRIPT_DIR/puppet/merge_operations_test.pp" "Hash override: {a => 1, b => 99, c => 3}"
run_test "array_concat" "$SCRIPT_DIR/puppet/merge_operations_test.pp" "Array concat: [1, 2, 3, 4, 5, 6]"
run_test "string_concat" "$SCRIPT_DIR/puppet/merge_operations_test.pp" "String concat: Hello World"
run_test "number_add" "$SCRIPT_DIR/puppet/merge_operations_test.pp" "Number add: 35"

echo ""
echo "=== Regex Case Statement Tests ==="
run_test "regex_case_present" "$SCRIPT_DIR/puppet/regex_case_test.pp" "Regex case 1: should_install"
run_test "regex_case_absent" "$SCRIPT_DIR/puppet/regex_case_test.pp" "Regex case 2: should_remove"
run_test "regex_case_default" "$SCRIPT_DIR/puppet/regex_case_test.pp" "Regex case 3: unknown"
run_test "regex_case_os" "$SCRIPT_DIR/puppet/regex_case_test.pp" "OS package manager: apt"
run_test "regex_case_file" "$SCRIPT_DIR/puppet/regex_case_test.pp" "File type: puppet"

echo ""
echo "=== Hash/Array Subtraction Tests ==="
run_test "hash_sub_single" "$SCRIPT_DIR/puppet/merge_operations_test.pp" "Hash minus 'b': {a => 1, c => 3}"
run_test "hash_sub_chain" "$SCRIPT_DIR/puppet/merge_operations_test.pp" "Hash minus 'a' and 'c': {b => 2}"
run_test "array_diff" "$SCRIPT_DIR/puppet/merge_operations_test.pp" "Array difference: [1, 3, 5]"

echo ""
echo "=== Splat Operator Tests ==="
run_test "splat_basic" "$SCRIPT_DIR/puppet/splat_test.pp" "Splat tests completed"
run_catalog_test "splat_attrs" "$SCRIPT_DIR/puppet/splat_test.pp" "mode => 0644"
run_catalog_test "splat_filtered" "$SCRIPT_DIR/puppet/splat_test.pp" "content => Overridden content"

echo ""
echo "=== Sensitive Type Tests ==="
run_test "sensitive_basic" "$SCRIPT_DIR/puppet/sensitive_test.pp" "Sensitive tests completed"
run_test "sensitive_wrap" "$SCRIPT_DIR/puppet/sensitive_test.pp" "Password wrapped:"

echo ""
echo "=== Chained Bracket Access Tests ==="
run_catalog_test "chain_two_level" "$SCRIPT_DIR/puppet/chained_access_test.pp" "Family: Debian"
run_catalog_test "chain_three_level" "$SCRIPT_DIR/puppet/chained_access_test.pp" "Major: 22.04"
run_catalog_test "chain_case" "$SCRIPT_DIR/puppet/chained_access_test.pp" "Matched Debian in case"
run_catalog_test "chain_if" "$SCRIPT_DIR/puppet/chained_access_test.pp" "Matched Ubuntu in if"
run_catalog_test "chain_interpolation" "$SCRIPT_DIR/puppet/chained_access_test.pp" "OS: Ubuntu, Family: Debian"

echo ""
echo "=== Complex Binary Expression Tests ==="
run_catalog_test "binary_simple_and" "$SCRIPT_DIR/puppet/binary_expr_test.pp" "false and true and true = false"
run_catalog_test "binary_triple_cmp" "$SCRIPT_DIR/puppet/binary_expr_test.pp" "RedHat check (should be false): false"
run_catalog_test "binary_shortcircuit" "$SCRIPT_DIR/puppet/binary_expr_test.pp" "Short-circuit evaluation works"
run_catalog_test "binary_all_true" "$SCRIPT_DIR/puppet/binary_expr_test.pp" "All conditions true: true"
run_catalog_test "binary_parens" "$SCRIPT_DIR/puppet/binary_expr_test.pp" "(false) and (true) = false"
run_catalog_test "binary_nested_parens" "$SCRIPT_DIR/puppet/binary_expr_test.pp" "(false and true) and (true) = false"
run_catalog_test "binary_or" "$SCRIPT_DIR/puppet/binary_expr_test.pp" "false or true or false = true"

echo ""
echo "=========================================="
echo -e "Feature Test Results: ${passed_tests}/${total_tests} tests passed"

if [ $passed_tests -eq $total_tests ]; then
    echo -e "${GREEN}All feature tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some feature tests failed.${NC}"
    exit 1
fi
