#!/bin/bash
# Verify the puppet:///modules/MOD/PATH source URL validator:
#   - Existing regular file: OK
#   - Existing directory: OK
#   - Missing file in known module: error
#   - Unknown module: error

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PUPPETC="$PROJECT_DIR/compiler/.libs/puppetc-compile"
MODULES="$SCRIPT_DIR/modules"

export DYLD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${DYLD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${LD_LIBRARY_PATH:-}"

RED=$'\e[31m'; GREEN=$'\e[32m'; NC=$'\e[0m'
PASSED=0; FAILED=0

echo "=== Testing puppet:///modules/ source validation ==="
echo

# Test 1: file + directory sources both pass
echo "Test 1: file source + directory source both accepted"
out=$($PUPPETC -s -m "$MODULES" "$SCRIPT_DIR/puppet/source_ok.pp" 2>&1)
if echo "$out" | grep -q "Status: OK" && ! echo "$out" | grep -q "Could not retrieve information"; then
    echo "  ${GREEN}✓${NC} both sources accepted"
    ((PASSED++))
else
    echo "  ${RED}✗${NC} valid sources rejected"
    echo "$out" | tail -10 | sed 's/^/      /'
    ((FAILED++))
fi

# Test 2: missing file inside known module is flagged
echo
echo "Test 2: missing file in known module is flagged"
out=$($PUPPETC -s -m "$MODULES" "$SCRIPT_DIR/puppet/source_missing.pp" 2>&1)
if echo "$out" | grep -q "puppet:///modules/srctest/no-such-file.txt"; then
    echo "  ${GREEN}✓${NC} missing file in known module flagged"
    ((PASSED++))
else
    echo "  ${RED}✗${NC} missing file NOT flagged"
    echo "$out" | tail -10 | sed 's/^/      /'
    ((FAILED++))
fi

# Test 3: unknown module is flagged
echo
echo "Test 3: unknown module is flagged"
if echo "$out" | grep -q "puppet:///modules/no-such-module/anything"; then
    echo "  ${GREEN}✓${NC} unknown module flagged"
    ((PASSED++))
else
    echo "  ${RED}✗${NC} unknown module NOT flagged"
    ((FAILED++))
fi

echo
if [ "$FAILED" -eq 0 ]; then
    echo "${GREEN}✓ All $PASSED source-URL tests passed${NC}"
    exit 0
else
    echo "${RED}✗ $FAILED of $((PASSED+FAILED)) tests failed${NC}"
    exit 1
fi
