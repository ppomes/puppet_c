#!/bin/bash
# Item 1 — legacy validate_* / is_* functions (removed in stdlib 9 / Puppet 8)
# are flagged as errors during a normal compile (no -8 flag), and the count
# lands in the standard --summary stream.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PUPPETC="$PROJECT_DIR/compiler/.libs/puppetc-compile"

export DYLD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${DYLD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${LD_LIBRARY_PATH:-}"

RED=$'\e[31m'; GREEN=$'\e[32m'; NC=$'\e[0m'
PASSED=0; FAILED=0
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "=== Testing Puppet 8 legacy validate_*/is_* detection ==="
echo

# Fixture: 5 legacy stdlib functions removed in Puppet 8 -> 5 errors.
FIX="$TMP/legacy.pp"
cat > "$FIX" <<'PP'
validate_string('foo')
validate_re('abc', '^a')
validate_array([1, 2, 3])
if is_string('x') { notice('a') }
if is_array(['x']) { notice('b') }
PP

# Clean fixture: no legacy functions -> 0 errors.
CLEAN="$TMP/clean.pp"
printf 'notice("hello world")\n' > "$CLEAN"

check() {  # $1 desc  $2 condition-result(0/1)  $3 detail
    if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++));
    else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi
}

echo "Test 1: 5 legacy functions are reported as errors in the summary"
out=$($PUPPETC -e -s "$FIX" 2>&1); rc=$?
errline=$(echo "$out" | grep -E 'Total errors:' | tail -1)
removed=$(echo "$out" | grep -c 'removed in Puppet 8')
[ "$removed" -eq 5 ];                         check "5 'removed in Puppet 8' diagnostics" $? "$out"
echo "$errline" | grep -qE 'Total errors: +5'; check "summary 'Total errors: 5'"          $? "$errline"
[ "$rc" -ne 0 ];                              check "non-zero exit ($rc)"                  $? "$out"

echo
echo "Test 2: a clean manifest passes (no false positives)"
out=$($PUPPETC -e -s "$CLEAN" 2>&1); rc=$?
echo "$out" | grep -qE 'Total errors: +0'; check "summary 'Total errors: 0'" $? "$out"
[ "$rc" -eq 0 ];                           check "zero exit ($rc)"           $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
