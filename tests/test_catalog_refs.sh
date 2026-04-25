#!/bin/bash
# Verify the catalog ref validator (require/subscribe/before/notify):
#   - Refs to existing resources / included classes pass cleanly
#   - Refs to absent resources are reported with the standard
#     "Could not find resource 'X[Y]' in parameter 'p'" message
#   - Refs to defined-but-not-included classes are also flagged

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PUPPETC="$PROJECT_DIR/compiler/.libs/puppetc-compile"

export DYLD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${DYLD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${LD_LIBRARY_PATH:-}"

RED=$'\e[31m'; GREEN=$'\e[32m'; NC=$'\e[0m'
PASSED=0; FAILED=0

echo "=== Testing catalog reference validation ==="
echo

# Test 1: valid refs pass
echo "Test 1: valid refs (existing File + included Class) pass"
out=$($PUPPETC -s "$SCRIPT_DIR/puppet/refs_ok.pp" 2>&1)
if echo "$out" | grep -q "Status: OK" && ! echo "$out" | grep -q "Could not find resource"; then
    echo "  ${GREEN}✓${NC} clean compile"
    ((PASSED++))
else
    echo "  ${RED}✗${NC} valid refs flagged or compile failed"
    echo "$out" | tail -10 | sed 's/^/      /'
    ((FAILED++))
fi

# Test 2: missing File ref reported
echo
echo "Test 2: subscribe to undeclared File is reported"
out=$($PUPPETC -s "$SCRIPT_DIR/puppet/refs_missing_resource.pp" 2>&1)
if echo "$out" | grep -q "Could not find resource 'File\[/etc/never-declared\]'"; then
    echo "  ${GREEN}✓${NC} missing File flagged"
    ((PASSED++))
else
    echo "  ${RED}✗${NC} missing File NOT flagged"
    echo "$out" | tail -10 | sed 's/^/      /'
    ((FAILED++))
fi

# Test 3: defined-but-not-included class is flagged
echo
echo "Test 3: notify Class['never_used'] (defined but not included) reported"
out=$($PUPPETC -s "$SCRIPT_DIR/puppet/refs_missing_class.pp" 2>&1)
if echo "$out" | grep -q "Could not find resource 'Class\[never_used\]'"; then
    echo "  ${GREEN}✓${NC} unincluded Class flagged"
    ((PASSED++))
else
    echo "  ${RED}✗${NC} Class[never_used] NOT flagged"
    echo "$out" | tail -10 | sed 's/^/      /'
    ((FAILED++))
fi

echo
if [ "$FAILED" -eq 0 ]; then
    echo "${GREEN}✓ All $PASSED catalog-refs tests passed${NC}"
    exit 0
else
    echo "${RED}✗ $FAILED of $((PASSED+FAILED)) tests failed${NC}"
    exit 1
fi
