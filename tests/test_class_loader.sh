#!/bin/bash
# Verify the module loader integration:
#   - Autoloaded classes are added to catalog->classes (so Class[X]
#     refs resolve)
#   - Ruby custom types in lib/puppet/type/*.rb are recognised
#   - Class autoload sets caller_module_name (so hiera() inside the
#     class can resolve %{module_name})

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PUPPETC="$PROJECT_DIR/compiler/.libs/puppetc-compile"
MODULES="$SCRIPT_DIR/modules"
MANIFEST="$SCRIPT_DIR/puppet/loader_includes.pp"

export DYLD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${DYLD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${LD_LIBRARY_PATH:-}"

RED=$'\e[31m'; GREEN=$'\e[32m'; NC=$'\e[0m'
PASSED=0; FAILED=0

echo "=== Testing class loader integration ==="
echo

# Test 1: catalog mode emits both autoloaded classes
echo "Test 1: include loadertest + loadertest::sub appear in catalog->classes"
cat_json=$($PUPPETC -c -m "$MODULES" "$MANIFEST" 2>/dev/null)
if echo "$cat_json" | grep -q '"loadertest"' && \
   echo "$cat_json" | grep -q '"loadertest::sub"'; then
    echo "  ${GREEN}✓${NC} both classes registered"
    ((PASSED++))
else
    echo "  ${RED}✗${NC} catalog missing one or both classes"
    echo "$cat_json" | python3 -c "import json,sys; d=json.load(sys.stdin); print('  classes:', d.get('classes',[]))" 2>/dev/null | sed 's/^/    /'
    ((FAILED++))
fi

# Test 2: Class['loadertest'] in require resolves (no Could-not-find)
echo
echo "Test 2: Class['loadertest'] reference resolves (no validation error)"
out=$($PUPPETC -s -m "$MODULES" "$MANIFEST" 2>&1)
if echo "$out" | grep -q "Could not find resource 'Class\[loadertest\]'"; then
    echo "  ${RED}✗${NC} Class[loadertest] flagged as missing despite include"
    echo "$out" | grep -E "Could not find" | head -3 | sed 's/^/      /'
    ((FAILED++))
elif echo "$out" | grep -q "Status: OK"; then
    echo "  ${GREEN}✓${NC} reference resolved cleanly"
    ((PASSED++))
else
    echo "  ${RED}✗${NC} compile didn't reach OK status"
    echo "$out" | tail -10 | sed 's/^/      /'
    ((FAILED++))
fi

# Test 3: Ruby custom type from lib/puppet/type/*.rb is recognised
echo
echo "Test 3: Puppet::Type.newtype(:loadertest_type) is recognised"
if echo "$out" | grep -q "Unknown resource type: 'loadertest_type'"; then
    echo "  ${RED}✗${NC} Ruby type wrongly flagged as unknown"
    ((FAILED++))
elif echo "$out" | grep -qE "Total errors:\s+0"; then
    echo "  ${GREEN}✓${NC} Ruby type accepted"
    ((PASSED++))
else
    echo "  ${RED}✗${NC} unexpected error during compile"
    echo "$out" | tail -5 | sed 's/^/      /'
    ((FAILED++))
fi

# Test 4: typo in unnamespaced type IS rejected
echo
echo "Test 4: typo in unnamespaced type (loadrtest_type) is rejected"
typo=$(mktemp --suffix=.pp)
trap 'rm -f "$typo"' EXIT
cat > "$typo" <<'EOF'
node default {
  loadrtest_type { 'oops': value => 'x' }
}
EOF
typo_out=$($PUPPETC -s -m "$MODULES" "$typo" 2>&1)
if echo "$typo_out" | grep -q "Unknown resource type: 'loadrtest_type'"; then
    echo "  ${GREEN}✓${NC} typo flagged as unknown"
    ((PASSED++))
else
    echo "  ${RED}✗${NC} typo not flagged"
    echo "$typo_out" | tail -5 | sed 's/^/      /'
    ((FAILED++))
fi

echo
if [ "$FAILED" -eq 0 ]; then
    echo "${GREEN}✓ All $PASSED class-loader tests passed${NC}"
    exit 0
else
    echo "${RED}✗ $FAILED of $((PASSED+FAILED)) tests failed${NC}"
    exit 1
fi
