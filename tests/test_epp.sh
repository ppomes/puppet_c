#!/bin/bash

# EPP Template Testing Script
# Tests EPP template functionality including trim markers and control structures

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="$SCRIPT_DIR/output"

# Set library path
export LD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:$LD_LIBRARY_PATH"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

echo "EPP Template Test Suite"
echo "======================="

mkdir -p "$OUTPUT_DIR"

total_tests=0
passed_tests=0

# Run a test with inline EPP via manifest
run_epp_test() {
    local test_name="$1"
    local manifest_content="$2"
    local expected="$3"

    echo -n "Testing $test_name... "
    total_tests=$((total_tests + 1))

    # Create temp manifest
    local tmpfile=$(mktemp /tmp/epp_test_XXXXXX.pp)
    echo "$manifest_content" > "$tmpfile"

    cd "$PROJECT_DIR"
    if output=$($PROJECT_DIR/compiler/.libs/puppetc-compile -p "$tmpfile" -m tests/modules 2>&1); then
        # Extract message content - everything between "message => " and standalone ","
        # Stop when we hit a line that's just ","
        local content=$(echo "$output" | awk '
            /message =>/ { found=1; sub(/.*message => /,""); print; next }
            found && /^,$/ { exit }
            found && /^Total:/ { exit }
            found { print }
        ' | sed '/^$/d')

        if [ "$content" = "$expected" ]; then
            echo -e "${GREEN}PASS${NC}"
            passed_tests=$((passed_tests + 1))
        else
            echo -e "${RED}FAIL${NC}"
            echo "  Expected: '$expected'"
            echo "  Got:      '$content'"
        fi
    else
        echo -e "${RED}FAIL${NC} - Command failed"
        echo "  Error: $output"
    fi

    rm -f "$tmpfile"
}

# Run a test checking that output contains all expected lines (order-independent)
run_epp_test_contains() {
    local test_name="$1"
    local manifest_content="$2"
    shift 2
    local expected_lines=("$@")

    echo -n "Testing $test_name... "
    total_tests=$((total_tests + 1))

    local tmpfile=$(mktemp /tmp/epp_test_XXXXXX.pp)
    echo "$manifest_content" > "$tmpfile"

    cd "$PROJECT_DIR"
    if output=$($PROJECT_DIR/compiler/.libs/puppetc-compile -p "$tmpfile" -m tests/modules 2>&1); then
        local content=$(echo "$output" | awk '
            /message =>/ { found=1; sub(/.*message => /,""); print; next }
            found && /^,$/ { exit }
            found && /^Total:/ { exit }
            found { print }
        ' | sed '/^$/d')

        local all_found=true
        for line in "${expected_lines[@]}"; do
            if ! echo "$content" | grep -qF "$line"; then
                all_found=false
                break
            fi
        done

        if $all_found; then
            echo -e "${GREEN}PASS${NC}"
            passed_tests=$((passed_tests + 1))
        else
            echo -e "${RED}FAIL${NC}"
            echo "  Expected lines: ${expected_lines[*]}"
            echo "  Got: '$content'"
        fi
    else
        echo -e "${RED}FAIL${NC} - Command failed"
        echo "  Error: $output"
    fi

    rm -f "$tmpfile"
}

echo ""
echo "Running EPP Tests..."
echo ""

# Test 1: Basic trim markers
run_epp_test "basic_trim" \
    "\$r = epp('epp_test/trim_basic.epp', {'name' => 'World'})
notify { 'test': message => \$r }" \
    "Hello World!"

# Test 2: Each loop with trim markers (no extra blank lines)
run_epp_test_contains "each_trim" \
    "\$r = epp('epp_test/trim_newlines.epp', {'items' => {'a' => 1, 'b' => 2}})
notify { 'test': message => \$r }" \
    "a = 1" "b = 2"

# Test 3: If/elsif/else - branch 'a'
run_epp_test "if_elsif_a" \
    "\$r = epp('epp_test/if_elsif_else.epp', {'value' => 'a'})
notify { 'test': message => \$r }" \
    "result: alpha"

# Test 4: If/elsif/else - branch 'b'
run_epp_test "if_elsif_b" \
    "\$r = epp('epp_test/if_elsif_else.epp', {'value' => 'b'})
notify { 'test': message => \$r }" \
    "result: beta"

# Test 5: If/elsif/else - else branch
run_epp_test "if_elsif_else" \
    "\$r = epp('epp_test/if_elsif_else.epp', {'value' => 'x'})
notify { 'test': message => \$r }" \
    "result: other"

# Test 6: Conditional each (mixed true/false/values)
run_epp_test_contains "conditional_each" \
    "\$r = epp('epp_test/conditional_each.epp', {'items' => {'enabled' => true, 'count' => 5, 'disabled' => false}})
notify { 'test': message => \$r }" \
    "enabled" "count = 5" "skip"

# Test 7: Comments should not appear in output
run_epp_test "comments" \
    "\$r = epp('epp_test/comments.epp')
notify { 'test': message => \$r }" \
    "line1
line2"

# Test 8: INI-style section (like mysql my.cnf)
run_epp_test_contains "ini_section" \
    "\$r = epp('epp_test/ini_section.epp', {'section' => 'mysqld', 'options' => {'port' => 3306, 'bind' => '0.0.0.0'}})
notify { 'test': message => \$r }" \
    "[mysqld]" "port = 3306" "bind = 0.0.0.0"

echo ""
echo "=========================================="
echo "EPP Test Results: ${passed_tests}/${total_tests} tests passed"

if [ $passed_tests -eq $total_tests ]; then
    echo -e "${GREEN}All EPP tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some EPP tests failed.${NC}"
    exit 1
fi
