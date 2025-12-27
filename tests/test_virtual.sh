#!/bin/bash
# Test script for virtual resources (@resource syntax) and realize()

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PUPPETC="$PROJECT_DIR/src/.libs/puppetc"
TEST_DIR="$SCRIPT_DIR/puppet"
FAILED=0
PASSED=0

# Set library path
export LD_LIBRARY_PATH="$PROJECT_DIR/src/.libs:$PROJECT_DIR/common/.libs:$LD_LIBRARY_PATH"

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=== Testing Virtual Resources ==="
echo

# Run the virtual resource test and get catalog output
CATALOG=$($PUPPETC --catalog $TEST_DIR/virtual_test.pp 2>&1)
EXIT_CODE=$?

if [ $EXIT_CODE -ne 0 ]; then
    echo -e "${RED}✗${NC} Virtual resource test failed to run (exit code: $EXIT_CODE)"
    echo "$CATALOG"
    exit 1
fi

# Test 1: Virtual resource virtual_alice should be in catalog (it was realized)
echo "Test 1: Realized virtual resource appears in catalog"
if echo "$CATALOG" | grep -q '"title": "virtual_alice"'; then
    echo -e "${GREEN}✓${NC} virtual_alice is in catalog after realize()"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} virtual_alice should be in catalog after realize()"
    ((FAILED++))
fi

# Test 2: Virtual resource virtual_bob should NOT be in catalog (not realized)
echo "Test 2: Non-realized virtual resource not in catalog"
if echo "$CATALOG" | grep -q '"title": "virtual_bob"'; then
    echo -e "${RED}✗${NC} virtual_bob should NOT be in catalog (wasn't realized)"
    ((FAILED++))
else
    echo -e "${GREEN}✓${NC} virtual_bob correctly not in catalog (wasn't realized)"
    ((PASSED++))
fi

# Test 3: Multiple realized resources should be in catalog
echo "Test 3: Multiple realized virtual resources in catalog"
if echo "$CATALOG" | grep -q '"title": "virtual_msg1"' && \
   echo "$CATALOG" | grep -q '"title": "virtual_msg2"'; then
    echo -e "${GREEN}✓${NC} Multiple virtual resources realized correctly"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Multiple virtual resources not realized"
    ((FAILED++))
fi

# Test 4: Regular resource should still work
echo "Test 4: Regular resource in catalog"
if echo "$CATALOG" | grep -q '"title": "regular_notify"'; then
    echo -e "${GREEN}✓${NC} Regular resource still works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Regular resource not in catalog"
    ((FAILED++))
fi

# Test 5: Check for proper warning on non-existent virtual resource
echo "Test 5: Warning for non-existent virtual resource"
if echo "$CATALOG" | grep -q "is not a virtual resource"; then
    echo -e "${GREEN}✓${NC} Warning issued for non-existent virtual resource"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} No warning for non-existent virtual resource"
    ((FAILED++))
fi

# Test 6: Verify resource parameters are preserved
echo "Test 6: Resource parameters preserved after realize"
if echo "$CATALOG" | grep -q '"uid": 1001' && \
   echo "$CATALOG" | grep -q '"/bin/bash"'; then
    echo -e "${GREEN}✓${NC} Resource parameters preserved correctly"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Resource parameters not preserved"
    ((FAILED++))
fi

echo
echo "================================"
echo "Virtual Resource Tests: $PASSED passed, $FAILED failed"
echo "================================"

if [ $FAILED -gt 0 ]; then
    exit 1
fi
exit 0
