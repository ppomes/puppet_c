#!/bin/bash
# Test script for tag statement and tagged() function

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

echo "=== Testing Tag Statement and tagged() Function ==="
echo

# Run the tag test and get catalog output
CATALOG=$($PUPPETC --catalog $TEST_DIR/tag_test.pp 2>&1)
EXIT_CODE=$?

if [ $EXIT_CODE -ne 0 ]; then
    echo -e "${RED}✗${NC} Tag test failed to run (exit code: $EXIT_CODE)"
    echo "$CATALOG"
    exit 1
fi

# Test 1: Basic tag is applied to resource
echo "Test 1: Basic tag statement"
if echo "$CATALOG" | grep -q '"title": "test1"' && \
   echo "$CATALOG" | grep -q '"tags": \[.*"basic_tag"'; then
    echo -e "${GREEN}✓${NC} Resource test1 has basic_tag"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Resource test1 missing basic_tag"
    ((FAILED++))
fi

# Test 2: Multiple tags are applied
echo "Test 2: Multiple tags"
if echo "$CATALOG" | grep -q '"title": "test2"' && \
   echo "$CATALOG" | grep -q '"tag_one"' && \
   echo "$CATALOG" | grep -q '"tag_two"'; then
    echo -e "${GREEN}✓${NC} Resource test2 has both tag_one and tag_two"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Resource test2 missing tags"
    ((FAILED++))
fi

# Test 3: tagged() function returns true for set tag
echo "Test 3: tagged() function returns true"
if echo "$CATALOG" | grep -q '"title": "test3"' && \
   echo "$CATALOG" | grep -q '"message": "Tagged as webserver"'; then
    echo -e "${GREEN}✓${NC} tagged('webserver') returned true"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} tagged('webserver') did not return true"
    ((FAILED++))
fi

# Test 4: tagged() function returns false for non-existent tag
echo "Test 4: tagged() function returns false"
if echo "$CATALOG" | grep -q '"title": "test4"' && \
   echo "$CATALOG" | grep -q '"message": "Not tagged as nonexistent"'; then
    echo -e "${GREEN}✓${NC} tagged('nonexistent') returned false"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} tagged('nonexistent') did not return false"
    ((FAILED++))
fi

# Test 5: All resources have expected tags
echo "Test 5: All 4 resources present"
RESOURCE_COUNT=$(echo "$CATALOG" | grep -c '"type": "notify"')
if [ "$RESOURCE_COUNT" -eq 4 ]; then
    echo -e "${GREEN}✓${NC} All 4 notify resources present"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Expected 4 resources, found $RESOURCE_COUNT"
    ((FAILED++))
fi

echo
echo "================================"
echo "Tag Tests: $PASSED passed, $FAILED failed"
echo "================================"

if [ $FAILED -gt 0 ]; then
    exit 1
fi
exit 0
