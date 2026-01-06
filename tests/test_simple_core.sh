#!/bin/bash
# Simple test for core functions

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Set library path (LD_LIBRARY_PATH for Linux, DYLD_LIBRARY_PATH for macOS)
export LD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:$LD_LIBRARY_PATH"
export DYLD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:$DYLD_LIBRARY_PATH"

echo "=== Testing Core Puppet Functions ==="
echo

OUTPUT=$(../compiler/puppetc-compile --eval puppet/simple_core_test.pp 2>&1)

echo "Output from core functions test:"
echo "$OUTPUT"
echo

# Check if the log entries are there
if echo "$OUTPUT" | grep -q "Notice: Test message"; then
    echo "✓ notice() function works"
else
    echo "✗ notice() function failed"
fi

if echo "$OUTPUT" | grep -q "Info: Info test"; then
    echo "✓ info() function works" 
else
    echo "✗ info() function failed"
fi

if echo "$OUTPUT" | grep -q "Warning: Warning test"; then
    echo "✓ warning() function works"
else
    echo "✗ warning() function failed"
fi

if echo "$OUTPUT" | grep -q "Debug: Debug test"; then
    echo "✓ debug() function works"
else
    echo "✗ debug() function failed"
fi

# Test fail function
echo
echo "Testing fail() function:"
echo 'fail("Test failure")' > /tmp/fail_test.pp
FAIL_OUTPUT=$(../compiler/puppetc-compile --eval /tmp/fail_test.pp 2>&1)
if echo "$FAIL_OUTPUT" | grep -q "Critical: Test failure"; then
    echo "✓ fail() function works"
else
    echo "✗ fail() function failed"
    echo "Got: $FAIL_OUTPUT"
fi
rm -f /tmp/fail_test.pp

echo
echo "=== Core Functions Implementation Complete! ==="