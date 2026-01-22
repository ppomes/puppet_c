#!/bin/bash
# Test script for resource chains (-> and ~>)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PUPPETC="$PROJECT_DIR/compiler/.libs/puppetc-compile"
TEST_DIR="$SCRIPT_DIR/puppet"
FAILED=0
PASSED=0

# Set library path
export DYLD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:$DYLD_LIBRARY_PATH"
export LD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:$LD_LIBRARY_PATH"

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=== Testing Resource Chains ==="
echo

# Run the chain test and get catalog output
CATALOG=$($PUPPETC --catalog $TEST_DIR/chain_test.pp 2>&1)
EXIT_CODE=$?

if [ $EXIT_CODE -ne 0 ]; then
    echo -e "${RED}✗${NC} Chain test failed to run (exit code: $EXIT_CODE)"
    echo "$CATALOG"
    exit 1
fi

# Test 1: Simple before chain - check edge exists
echo "Test 1: Simple before chain (->)"
if echo "$CATALOG" | grep -q '"source": {"type": "Notify", "title": "chain_first"}' && \
   echo "$CATALOG" | grep -q '"target": {"type": "Notify", "title": "chain_second"}' && \
   echo "$CATALOG" | grep -q '"relationship": "before"'; then
    echo -e "${GREEN}✓${NC} Before chain edge exists: Notify[chain_first] -> Notify[chain_second]"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Before chain edge missing"
    ((FAILED++))
fi

# Test 2: Notify chain - check edge exists with notify relationship
echo "Test 2: Notify chain (~>)"
if echo "$CATALOG" | grep -q '"source": {"type": "Notify", "title": "chain_source"}' && \
   echo "$CATALOG" | grep -q '"target": {"type": "Notify", "title": "chain_target"}' && \
   echo "$CATALOG" | grep -q '"relationship": "notifies"'; then
    echo -e "${GREEN}✓${NC} Notify chain edge exists: Notify[chain_source] ~> Notify[chain_target]"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Notify chain edge missing"
    ((FAILED++))
fi

# Test 3: Multi-step chain - check both edges exist
echo "Test 3: Multi-step chain (A -> B ~> C)"
if echo "$CATALOG" | grep -q '"source": {"type": "Notify", "title": "multi_a"}' && \
   echo "$CATALOG" | grep -q '"target": {"type": "Notify", "title": "multi_b"}'; then
    echo -e "${GREEN}✓${NC} First edge: Notify[multi_a] -> Notify[multi_b]"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} First edge of multi-chain missing"
    ((FAILED++))
fi

if echo "$CATALOG" | grep -q '"source": {"type": "Notify", "title": "multi_b"}' && \
   echo "$CATALOG" | grep -q '"target": {"type": "Notify", "title": "multi_c"}'; then
    echo -e "${GREEN}✓${NC} Second edge: Notify[multi_b] ~> Notify[multi_c]"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Second edge of multi-chain missing"
    ((FAILED++))
fi

# Test 4: Reverse arrow - check edge is reversed correctly
echo "Test 4: Reverse arrow (<-)"
if echo "$CATALOG" | grep -q '"source": {"type": "Notify", "title": "rev_before"}' && \
   echo "$CATALOG" | grep -q '"target": {"type": "Notify", "title": "rev_after"}'; then
    echo -e "${GREEN}✓${NC} Reverse arrow correctly creates: Notify[rev_before] -> Notify[rev_after]"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Reverse arrow edge incorrect"
    ((FAILED++))
fi

# Test 5: Check edges array exists and has entries
echo "Test 5: Edges array in catalog"
EDGE_COUNT=$(echo "$CATALOG" | grep -c '"relationship":')
if [ "$EDGE_COUNT" -ge 4 ]; then
    echo -e "${GREEN}✓${NC} Catalog has $EDGE_COUNT edges"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Expected at least 4 edges, got $EDGE_COUNT"
    ((FAILED++))
fi

# Test 6: All resources are in catalog
echo "Test 6: All resources in catalog"
RESOURCE_COUNT=0
for title in chain_first chain_second chain_source chain_target multi_a multi_b multi_c rev_after rev_before; do
    if echo "$CATALOG" | grep -q "\"title\": \"$title\""; then
        ((RESOURCE_COUNT++))
    fi
done
if [ "$RESOURCE_COUNT" -eq 9 ]; then
    echo -e "${GREEN}✓${NC} All 9 resources present in catalog"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Expected 9 resources, found $RESOURCE_COUNT"
    ((FAILED++))
fi

echo
echo "================================"
echo "Resource Chain Tests: $PASSED passed, $FAILED failed"
echo "================================"

if [ $FAILED -gt 0 ]; then
    exit 1
fi
exit 0
