#!/bin/bash
# Test script for Puppet core functions

PUPPETC="../compiler/puppetc-compile"
TEST_DIR="puppet"
FAILED=0
PASSED=0

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=== Testing Puppet Core Functions ==="
echo

# Test 1: Basic logging functions
echo "Test 1: Logging functions"
OUTPUT=$($PUPPETC --eval $TEST_DIR/core_functions_test.pp 2>&1)
if echo "$OUTPUT" | grep -q "Notice: Starting core function tests"; then
    echo -e "${GREEN}✓${NC} notice() function works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} notice() function failed"
    ((FAILED++))
fi

if echo "$OUTPUT" | grep -q "Info: This is an info message"; then
    echo -e "${GREEN}✓${NC} info() function works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} info() function failed"
    ((FAILED++))
fi

if echo "$OUTPUT" | grep -q "Warning: This is a warning message"; then
    echo -e "${GREEN}✓${NC} warning() function works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} warning() function failed"
    ((FAILED++))
fi

if echo "$OUTPUT" | grep -q "Debug: Debug message: testing = true"; then
    echo -e "${GREEN}✓${NC} debug() function works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} debug() function failed"
    ((FAILED++))
fi

echo

# Test 2: Variable interpolation in functions
echo "Test 2: Variable interpolation"
if echo "$OUTPUT" | grep -q "Notice: Variable test: hello world"; then
    echo -e "${GREEN}✓${NC} Variable interpolation in functions works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Variable interpolation failed"
    ((FAILED++))
fi

if echo "$OUTPUT" | grep -q "Notice: Server web01 listening on port 8080"; then
    echo -e "${GREEN}✓${NC} Multiple argument concatenation works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Multiple argument concatenation failed"
    ((FAILED++))
fi

echo

# Test 3: Tag functions
echo "Test 3: Tag functions"
if echo "$OUTPUT" | grep -q "Notice: This node is tagged as webserver"; then
    echo -e "${GREEN}✓${NC} tag() and tagged() functions work"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} tag/tagged functions failed"
    ((FAILED++))
fi

if echo "$OUTPUT" | grep -q "Notice: This node is NOT tagged as database"; then
    echo -e "${GREEN}✓${NC} tagged() negative test works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} tagged() negative test failed"
    ((FAILED++))
fi

echo

# Test 4: defined() function
echo "Test 4: defined() function"
if echo "$OUTPUT" | grep -q "Notice: Apache class is not defined"; then
    echo -e "${GREEN}✓${NC} defined() function works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} defined() function failed"
    ((FAILED++))
fi

echo

# Test 5: err() function
echo "Test 5: err() function"
if echo "$OUTPUT" | grep -q "Error: Configuration is invalid but continuing"; then
    echo -e "${GREEN}✓${NC} err() function works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} err() function failed"
    ((FAILED++))
fi

echo

# Test 6: fail() function (separate test that should fail)
echo "Test 6: fail() function"
cat > /tmp/fail_test.pp << 'EOF'
notice("Before fail")
fail("Intentional failure for testing")
notice("After fail - should not appear")
EOF

FAIL_OUTPUT=$($PUPPETC --eval /tmp/fail_test.pp 2>&1)
if echo "$FAIL_OUTPUT" | grep -q "Critical: Intentional failure for testing"; then
    if ! echo "$FAIL_OUTPUT" | grep -q "After fail - should not appear"; then
        echo -e "${GREEN}✓${NC} fail() function stops execution"
        ((PASSED++))
    else
        echo -e "${RED}✗${NC} fail() didn't stop execution"
        ((FAILED++))
    fi
else
    echo -e "${RED}✗${NC} fail() function not working"
    ((FAILED++))
fi

rm -f /tmp/fail_test.pp

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