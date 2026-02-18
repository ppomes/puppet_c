#!/bin/bash
# Test resource overrides on virtual and exported resources

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
NC='\033[0m' # No Color

echo "=== Testing Resource Overrides on Virtual/Exported Resources ==="
echo

# Run the test and get catalog output
CATALOG=$($PUPPETC --catalog $TEST_DIR/virtual_override_test.pp 2>&1)
EXIT_CODE=$?

if [ $EXIT_CODE -ne 0 ]; then
    echo -e "${RED}✗${NC} Virtual override test failed to run (exit code: $EXIT_CODE)"
    echo "$CATALOG"
    exit 1
fi

# Test 1: Virtual resource override applied
echo "Test 1: Virtual resource override"
if echo "$CATALOG" | grep -q '"title": "virtual_with_override"' && \
   echo "$CATALOG" | grep -q '"message": "overridden message"'; then
    echo -e "${GREEN}✓${NC} Virtual resource override applied correctly"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Virtual resource override not applied"
    echo "$CATALOG" | grep -A5 "virtual_with_override"
    ((FAILED++))
fi

# Test 2: Exported resource override doesn't cause error (no crash/error)
echo "Test 2: Exported resource override (no error)"
if ! echo "$CATALOG" | grep -qi "error.*exported_with_override"; then
    echo -e "${GREEN}✓${NC} Exported resource override accepted without error"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Exported resource override caused error"
    ((FAILED++))
fi

# Test 3: Conditional override on virtual resource
echo "Test 3: Conditional override on virtual resource"
if echo "$CATALOG" | grep -q '"title": "conditional_override"' && \
   echo "$CATALOG" | grep -q '"message": "conditional override applied"'; then
    echo -e "${GREEN}✓${NC} Conditional override applied correctly"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Conditional override not applied"
    echo "$CATALOG" | grep -A5 "conditional_override"
    ((FAILED++))
fi

echo
echo "================================"
echo "Virtual Override Tests: $PASSED passed, $FAILED failed"
echo "================================"

if [ $FAILED -gt 0 ]; then
    exit 1
fi
exit 0
