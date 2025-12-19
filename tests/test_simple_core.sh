#!/bin/bash
# Simple test for core functions

echo "=== Testing Core Puppet Functions ==="
echo

OUTPUT=$(../src/puppetc --eval puppet/simple_core_test.pp 2>&1)

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
FAIL_OUTPUT=$(../src/puppetc --eval /tmp/fail_test.pp 2>&1)
if echo "$FAIL_OUTPUT" | grep -q "Critical: Test failure"; then
    echo "✓ fail() function works"
else
    echo "✗ fail() function failed"
    echo "Got: $FAIL_OUTPUT"
fi
rm -f /tmp/fail_test.pp

echo
echo "=== Core Functions Implementation Complete! ==="