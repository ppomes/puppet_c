#!/bin/bash
# Test script for virtual resource collectors

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PUPPETC="$SCRIPT_DIR/../compiler/puppetc-compile"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

echo "=== Testing Virtual Resource Collectors ==="
echo

# Run from project root
cd "$SCRIPT_DIR/.."

# Run the collector test with catalog output to see what resources were realized
# Set library path for both Linux (LD_LIBRARY_PATH) and macOS (DYLD_LIBRARY_PATH)
export LD_LIBRARY_PATH=./compiler/.libs:./common/.libs:./facter/.libs:$LD_LIBRARY_PATH
export DYLD_LIBRARY_PATH=./compiler/.libs:./common/.libs:./facter/.libs:$DYLD_LIBRARY_PATH
OUTPUT=$(./compiler/.libs/puppetc-compile -e -c tests/puppet/collector_test.pp 2>&1)
EXIT_CODE=$?

echo "Test output:"
echo "$OUTPUT"
echo

# Check for expected realized resources in catalog
FAILED=0

# Test 1: Both users should be realized
if echo "$OUTPUT" | grep -q '"type": "user"' && echo "$OUTPUT" | grep -q '"title": "collector_alice"'; then
    echo -e "${GREEN}✓${NC} Test 1: User collector_alice realized"
else
    echo -e "${RED}✗${NC} Test 1: User collector_alice NOT realized"
    FAILED=1
fi

if echo "$OUTPUT" | grep -q '"title": "collector_bob"'; then
    echo -e "${GREEN}✓${NC} Test 1: User collector_bob realized"
else
    echo -e "${RED}✗${NC} Test 1: User collector_bob NOT realized"
    FAILED=1
fi

# Test 2: Only present file should be realized
if echo "$OUTPUT" | grep -q '"title": "/tmp/collector_present"'; then
    echo -e "${GREEN}✓${NC} Test 2: File /tmp/collector_present realized"
else
    echo -e "${RED}✗${NC} Test 2: File /tmp/collector_present NOT realized"
    FAILED=1
fi

if echo "$OUTPUT" | grep -q '"title": "/tmp/collector_absent"'; then
    echo -e "${RED}✗${NC} Test 2: File /tmp/collector_absent should NOT be realized"
    FAILED=1
else
    echo -e "${GREEN}✓${NC} Test 2: File /tmp/collector_absent correctly not realized"
fi

# Test 3: vim and emacs should be realized, nano should not
if echo "$OUTPUT" | grep -q '"title": "collector_vim"'; then
    echo -e "${GREEN}✓${NC} Test 3: Package collector_vim realized"
else
    echo -e "${RED}✗${NC} Test 3: Package collector_vim NOT realized"
    FAILED=1
fi

if echo "$OUTPUT" | grep -q '"title": "collector_emacs"'; then
    echo -e "${GREEN}✓${NC} Test 3: Package collector_emacs realized"
else
    echo -e "${RED}✗${NC} Test 3: Package collector_emacs NOT realized"
    FAILED=1
fi

if echo "$OUTPUT" | grep -q '"title": "collector_nano"'; then
    echo -e "${RED}✗${NC} Test 3: Package collector_nano should NOT be realized"
    FAILED=1
else
    echo -e "${GREEN}✓${NC} Test 3: Package collector_nano correctly not realized"
fi

# Test 5: apache should be realized, nginx should not
if echo "$OUTPUT" | grep -q '"title": "collector_apache"'; then
    echo -e "${GREEN}✓${NC} Test 5: Service collector_apache realized"
else
    echo -e "${RED}✗${NC} Test 5: Service collector_apache NOT realized"
    FAILED=1
fi

if echo "$OUTPUT" | grep -q '"title": "collector_nginx"'; then
    echo -e "${RED}✗${NC} Test 5: Service collector_nginx should NOT be realized"
    FAILED=1
else
    echo -e "${GREEN}✓${NC} Test 5: Service collector_nginx correctly not realized"
fi

echo
if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All collector tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some collector tests failed${NC}"
    exit 1
fi
