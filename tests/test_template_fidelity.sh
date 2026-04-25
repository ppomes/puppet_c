#!/bin/bash
# Verify ERB template rendering: scope.lookupvar, @vars, .each
# iteration, hash sort, must produce a known byte-for-byte output.
# Catches regressions in the ERB engine integration.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PUPPETC="$PROJECT_DIR/compiler/.libs/puppetc-compile"
MODULES="$SCRIPT_DIR/modules"
MANIFEST="$SCRIPT_DIR/puppet/erbtest_site.pp"
EXPECTED="$SCRIPT_DIR/data/erbtest.cfg.expected"

export DYLD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${DYLD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${LD_LIBRARY_PATH:-}"

RED=$'\e[31m'; GREEN=$'\e[32m'; NC=$'\e[0m'
PASSED=0; FAILED=0

echo "=== Testing ERB template rendering fidelity ==="
echo

# Render the template via -t and diff against the expected file.
got=$($PUPPETC -e -n test.example.com -t /tmp/erbtest.cfg -m "$MODULES" "$MANIFEST" 2>/dev/null)
echo "Test 1: rendered template matches expected output bit-for-bit"
if diff -u "$EXPECTED" <(echo "$got"); then
    echo "  ${GREEN}✓${NC} byte-for-byte match"
    ((PASSED++))
else
    echo "  ${RED}✗${NC} content differs"
    ((FAILED++))
fi

echo
if [ "$FAILED" -eq 0 ]; then
    echo "${GREEN}✓ All $PASSED template-fidelity tests passed${NC}"
    exit 0
else
    echo "${RED}✗ $FAILED of $((PASSED+FAILED)) tests failed${NC}"
    exit 1
fi
