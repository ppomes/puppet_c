#!/bin/bash
# Test script for array/hash append (+=)

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

echo "=== Testing Array/Hash Append (+= operator) ==="
echo

# Run the append test and get catalog output
CATALOG=$($PUPPETC --catalog $TEST_DIR/append_test.pp 2>&1)
EXIT_CODE=$?

if [ $EXIT_CODE -ne 0 ]; then
    echo -e "${RED}✗${NC} Append test failed to run (exit code: $EXIT_CODE)"
    echo "$CATALOG"
    exit 1
fi

# Test 1: Array append
echo "Test 1: Array append"
if echo "$CATALOG" | grep -q '"message": "Test 1 - Array append: \[initial, appended\]"'; then
    echo -e "${GREEN}✓${NC} Array append: \$arr1 = ['initial']; \$arr1 += ['appended'] => [initial, appended]"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Array append failed"
    echo "$CATALOG" | grep "Test 1"
    ((FAILED++))
fi

# Test 2: Array concatenation
echo "Test 2: Array concatenation"
if echo "$CATALOG" | grep -q '"message": "Test 2 - Concatenate arrays: \[a, b, c\]"'; then
    echo -e "${GREEN}✓${NC} Array concat: \$arr2 = ['a', 'b']; \$arr2 += ['c'] => [a, b, c]"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Array concatenation failed"
    echo "$CATALOG" | grep "Test 2"
    ((FAILED++))
fi

# Test 3: Append to undefined
echo "Test 3: Append to undefined variable"
if echo "$CATALOG" | grep -q '"message": "Test 3 - Append to undef: \[from_nothing\]"'; then
    echo -e "${GREEN}✓${NC} Append to undef: \$arr3 += ['from_nothing'] => [from_nothing]"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Append to undefined failed"
    echo "$CATALOG" | grep "Test 3"
    ((FAILED++))
fi

# Test 4: Hash merge
echo "Test 4: Hash merge"
if echo "$CATALOG" | grep -q '"message": "Test 4 - Hash merge:' && \
   echo "$CATALOG" | grep -q 'key1 => val1' && \
   echo "$CATALOG" | grep -q 'key2 => val2'; then
    echo -e "${GREEN}✓${NC} Hash merge: {key1 => val1} += {key2 => val2} => contains both keys"
    ((PASSED++))
else
    echo -e "${RED}✗${NC} Hash merge failed"
    echo "$CATALOG" | grep "Test 4"
    ((FAILED++))
fi

echo
echo "================================"
echo "Append Tests: $PASSED passed, $FAILED failed"
echo "================================"

if [ $FAILED -gt 0 ]; then
    exit 1
fi
exit 0
