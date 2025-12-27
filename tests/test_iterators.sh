#!/bin/bash
# Test script for Puppet iterator functions (each, map, filter, etc.)

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

echo "=== Testing Puppet Iterator Functions ==="
echo

# Run the iterator test
OUTPUT=$($PUPPETC --eval $TEST_DIR/iterator_test.pp 2>&1)
EXIT_CODE=$?

if [ $EXIT_CODE -ne 0 ]; then
    echo -e "${RED}✗${NC} Iterator test failed to run (exit code: $EXIT_CODE)"
    echo "$OUTPUT"
    exit 1
fi

# Test 1: each with array (single param)
echo "Test 1: each() with array (single param)"
if echo "$OUTPUT" | grep -q "Server: web1" && \
   echo "$OUTPUT" | grep -q "Server: web2" && \
   echo "$OUTPUT" | grep -q "Server: web3"; then
    echo -e "${GREEN}✓${NC} each() with array works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} each() with array failed"
    ((FAILED++))
fi

# Test 2: each with array (index, value)
echo "Test 2: each() with array (index, value)"
if echo "$OUTPUT" | grep -q "Fruit 0: apple" && \
   echo "$OUTPUT" | grep -q "Fruit 1: banana" && \
   echo "$OUTPUT" | grep -q "Fruit 2: cherry"; then
    echo -e "${GREEN}✓${NC} each() with array index works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} each() with array index failed"
    ((FAILED++))
fi

# Test 3: each with hash (key, value)
echo "Test 3: each() with hash (key, value)"
if echo "$OUTPUT" | grep -q "Config host=localhost" && \
   echo "$OUTPUT" | grep -q "Config port=8080"; then
    echo -e "${GREEN}✓${NC} each() with hash works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} each() with hash failed"
    ((FAILED++))
fi

# Test 4: each creating resources - check catalog output
echo "Test 4: each() creating resources"
CATALOG=$($PUPPETC --catalog $TEST_DIR/iterator_test.pp 2>&1)
if echo "$CATALOG" | grep -q '"title": "user-notify-alice"' && \
   echo "$CATALOG" | grep -q '"title": "user-notify-bob"'; then
    echo -e "${GREEN}✓${NC} each() creating resources works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} each() creating resources failed"
    ((FAILED++))
fi

# Test 5: nested each
echo "Test 5: nested each()"
if echo "$OUTPUT" | grep -q "Matrix\[0\]\[0\] = a" && \
   echo "$OUTPUT" | grep -q "Matrix\[0\]\[1\] = b" && \
   echo "$OUTPUT" | grep -q "Matrix\[1\]\[0\] = c" && \
   echo "$OUTPUT" | grep -q "Matrix\[1\]\[1\] = d"; then
    echo -e "${GREEN}✓${NC} nested each() works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} nested each() failed"
    ((FAILED++))
fi

# Test 6: map with array
echo "Test 6: map() with array"
if echo "$OUTPUT" | grep -q "Mapping 1" && \
   echo "$OUTPUT" | grep -q "Mapping 2" && \
   echo "$OUTPUT" | grep -q "Mapping 3"; then
    echo -e "${GREEN}✓${NC} map() with array works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} map() with array failed"
    ((FAILED++))
fi

# Test 7: map with hash
echo "Test 7: map() with hash"
if echo "$OUTPUT" | grep -q "Port http: 80" && \
   echo "$OUTPUT" | grep -q "Port https: 443"; then
    echo -e "${GREEN}✓${NC} map() with hash works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} map() with hash failed"
    ((FAILED++))
fi

# Test 8: filter with array
echo "Test 8: filter() with array"
if echo "$OUTPUT" | grep -q "Filtering: one" && \
   echo "$OUTPUT" | grep -q "Filtering: two" && \
   echo "$OUTPUT" | grep -q "Filtering: three"; then
    echo -e "${GREEN}✓${NC} filter() with array works"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} filter() with array failed"
    ((FAILED++))
fi

echo
echo "================================"
echo "Iterator Tests: $PASSED passed, $FAILED failed"
echo "================================"

if [ $FAILED -gt 0 ]; then
    exit 1
fi
exit 0
