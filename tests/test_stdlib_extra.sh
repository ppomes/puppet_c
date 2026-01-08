#!/bin/bash
# Test script for additional stdlib functions

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR/.."

# Set library path (LD_LIBRARY_PATH for Linux, DYLD_LIBRARY_PATH for macOS)
export LD_LIBRARY_PATH="./compiler/.libs:./common/.libs:./facter/.libs:$LD_LIBRARY_PATH"
export DYLD_LIBRARY_PATH="./compiler/.libs:./common/.libs:./facter/.libs:$DYLD_LIBRARY_PATH"

PUPPETC="./compiler/.libs/puppetc-compile"
FAILED=0
PASSED=0

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

run_test() {
    local name="$1"
    local code="$2"
    local expected="$3"

    # Write code to temp file
    TMPFILE=$(mktemp /tmp/puppet_test_XXXXXX.pp)
    echo "$code" > "$TMPFILE"

    OUTPUT=$(eval $PUPPETC -p "$TMPFILE" 2>&1)
    rm -f "$TMPFILE"

    if echo "$OUTPUT" | grep -qF "$expected"; then
        echo -e "${GREEN}✓${NC} $name"
        ((PASSED++))
    else
        echo -e "${RED}✗${NC} $name"
        echo "  Expected: $expected"
        echo "  Got: $OUTPUT"
        ((FAILED++))
    fi
}

echo "=== Testing Additional Stdlib Functions ==="
echo

# deep_merge tests
echo "--- deep_merge() ---"
run_test "deep_merge basic" \
    '$h = deep_merge({"a" => 1}, {"b" => 2}); notify { "test": message => $h }' \
    "a => 1"

run_test "deep_merge recursive" \
    '$h = deep_merge({"x" => {"a" => 1}}, {"x" => {"b" => 2}}); notify { "test": message => $h }' \
    "a => 1"

# prefix tests
echo "--- prefix() ---"
run_test "prefix basic" \
    '$a = prefix(["one", "two"], "pre_"); notify { "test": message => $a }' \
    "pre_one"

# suffix tests
echo "--- suffix() ---"
run_test "suffix basic" \
    '$a = suffix(["one", "two"], "_end"); notify { "test": message => $a }' \
    "one_end"

# delete_undef_values tests
echo "--- delete_undef_values() ---"
run_test "delete_undef_values" \
    '$h = delete_undef_values({"a" => 1, "b" => undef}); notify { "test": message => $h }' \
    "a => 1"

# union tests
echo "--- union() ---"
run_test "union basic" \
    '$a = union([1, 2], [2, 3]); notify { "test": message => $a }' \
    "1, 2, 3"

# intersection tests
echo "--- intersection() ---"
run_test "intersection basic" \
    '$a = intersection([1, 2, 3], [2, 3, 4]); notify { "test": message => $a }' \
    "2, 3"

# difference tests
echo "--- difference() ---"
run_test "difference basic" \
    '$a = difference([1, 2, 3], [2]); notify { "test": message => $a }' \
    "1, 3"

# swapcase tests
echo "--- swapcase() ---"
run_test "swapcase basic" \
    '$s = swapcase("Hello"); notify { "test": message => $s }' \
    "hELLO"

# clamp tests
echo "--- clamp() ---"
run_test "clamp within range" \
    '$n = clamp(5, 1, 10); notify { "test": message => $n }' \
    "5"
run_test "clamp below min" \
    '$n = clamp(-5, 1, 10); notify { "test": message => $n }' \
    "1"
run_test "clamp above max" \
    '$n = clamp(15, 1, 10); notify { "test": message => $n }' \
    "10"

# count tests
echo "--- count() ---"
run_test "count array" \
    '$n = count([1, 2, 3]); notify { "test": message => $n }' \
    "3"

# zip tests
echo "--- zip() ---"
run_test "zip basic" \
    '$a = zip(["a", "b"], [1, 2]); notify { "test": message => $a }' \
    "a, 1"

# any2bool tests
echo "--- any2bool() ---"
run_test "any2bool string true" \
    '$b = any2bool("yes"); notify { "test": message => $b }' \
    "true"
run_test "any2bool empty string" \
    '$b = any2bool(""); notify { "test": message => $b }' \
    "false"

# bool2num tests
echo "--- bool2num() ---"
run_test "bool2num true" \
    '$n = bool2num(true); notify { "test": message => $n }' \
    "1"
run_test "bool2num false" \
    '$n = bool2num(false); notify { "test": message => $n }' \
    "0"

# num2bool tests
echo "--- num2bool() ---"
run_test "num2bool 1" \
    '$b = num2bool(1); notify { "test": message => $b }' \
    "true"
run_test "num2bool 0" \
    '$b = num2bool(0); notify { "test": message => $b }' \
    "false"

# shell_split tests
echo "--- shell_split() ---"
run_test "shell_split basic" \
    '$a = shell_split("a b c"); notify { "test": message => $a }' \
    "a, b, c"

# squeeze tests
echo "--- squeeze() ---"
run_test "squeeze basic" \
    '$s = squeeze("heeello"); notify { "test": message => $s }' \
    "helo"

# Summary
echo
echo "=== Test Summary ==="
echo -e "Passed: ${GREEN}$PASSED${NC}"
echo -e "Failed: ${RED}$FAILED${NC}"

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed${NC}"
    exit 1
fi
