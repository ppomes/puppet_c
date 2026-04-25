#!/bin/bash
# Verify the harder hiera paths exercised by today's bug fixes:
#   - hiera.yaml :yaml :datadir parsed correctly (Symbol-keyed)
#   - -D <directory> auto-discovers a sibling hiera.yaml
#   - %{module_name} interpolated from the calling class (caller_module_name)
#   - %{::var} interpolated from facts/top-level vars per node

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PUPPETC="$PROJECT_DIR/compiler/.libs/puppetc-compile"
SITE="$SCRIPT_DIR/hieratest"

export DYLD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${DYLD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${LD_LIBRARY_PATH:-}"

RED=$'\e[31m'; GREEN=$'\e[32m'; NC=$'\e[0m'
PASSED=0; FAILED=0

echo "=== Testing advanced hiera (datadir, module_name, %{::env}) ==="
echo

# Test 1: env=prod node hits mymod/prod.yaml (datadir parse + module_name)
echo "Test 1: prod node finds key in mymod/prod.yaml"
out=$(cd "$SITE" && $PUPPETC -p -n prod-x.example.com -f allfacts.yaml -m modules -D hieralocal manifests/site.pp 2>&1)
if echo "$out" | grep -q "bare=prod-specific-value"; then
    echo "  ${GREEN}✓${NC} mymod/prod.yaml resolved (env=prod, module=mymod)"
    ((PASSED++))
else
    echo "  ${RED}✗${NC} prod node didn't resolve mymod/prod.yaml"
    echo "$out" | grep -E "mymod-result|Warning: hiera" | head -5 | sed 's/^/      /'
    ((FAILED++))
fi

# Test 2: env=global (stg) node falls back to mymod/global.yaml
echo
echo "Test 2: stg node falls back to mymod/global.yaml"
out=$(cd "$SITE" && $PUPPETC -p -n stg-x.example.com -f allfacts.yaml -m modules -D hieralocal manifests/site.pp 2>&1)
if echo "$out" | grep -q "bare=mymod-global-override"; then
    echo "  ${GREEN}✓${NC} fallthrough to mymod/global.yaml"
    ((PASSED++))
else
    echo "  ${RED}✗${NC} stg node didn't fall through to mymod/global.yaml"
    echo "$out" | grep -E "mymod-result|Warning: hiera" | head -5 | sed 's/^/      /'
    ((FAILED++))
fi

# Test 3: namespaced lookup (mymod::shared) resolves via hierarchy
echo
echo "Test 3: namespaced key mymod::shared resolves to global.yaml"
out=$(cd "$SITE" && $PUPPETC -p -n prod-x.example.com -f allfacts.yaml -m modules -D hieralocal manifests/site.pp 2>&1)
if echo "$out" | grep -q "shared=shared-from-global"; then
    echo "  ${GREEN}✓${NC} mymod::shared resolved"
    ((PASSED++))
else
    echo "  ${RED}✗${NC} mymod::shared not resolved"
    echo "$out" | grep -E "mymod-result|Warning: hiera" | head -5 | sed 's/^/      /'
    ((FAILED++))
fi

# Test 4: -D pointing directly at hiera.yaml file also works
echo
echo "Test 4: -D pointing at hiera.yaml file (not directory) works"
out=$(cd "$SITE" && $PUPPETC -p -n prod-x.example.com -f allfacts.yaml -m modules -D hiera.yaml manifests/site.pp 2>&1)
if echo "$out" | grep -q "bare=prod-specific-value"; then
    echo "  ${GREEN}✓${NC} explicit hiera.yaml file accepted"
    ((PASSED++))
else
    echo "  ${RED}✗${NC} -D hiera.yaml didn't work"
    echo "$out" | grep -E "Warning: hiera|mymod-result" | head -3 | sed 's/^/      /'
    ((FAILED++))
fi

echo
if [ "$FAILED" -eq 0 ]; then
    echo "${GREEN}✓ All $PASSED hiera-advanced tests passed${NC}"
    exit 0
else
    echo "${RED}✗ $FAILED of $((PASSED+FAILED)) tests failed${NC}"
    exit 1
fi
