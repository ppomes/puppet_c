#!/bin/bash

# Test runner for enhanced variable system
# This script runs all variable-related test cases and reports results

PUPPETC="./src/puppetc"
TEST_DIR="./tests"
RESULTS_DIR="./test_results"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Create results directory
mkdir -p $RESULTS_DIR

echo "======================================="
echo "Enhanced Variable System Test Suite"
echo "======================================="

# Function to run a single test
run_test() {
    local test_file=$1
    local test_name=$(basename "$test_file" .pp)
    
    echo -e "\n${YELLOW}Running test: $test_name${NC}"
    echo "Test file: $test_file"
    echo "----------------------------------------"
    
    # Run the test and capture output
    if $PUPPETC -e "$test_file" > "$RESULTS_DIR/${test_name}.out" 2>&1; then
        echo -e "${GREEN}✓ PASSED${NC}: $test_name"
        
        # Show key results
        echo "Key results:"
        grep "Set \$" "$RESULTS_DIR/${test_name}.out" | head -10
        
        # Check for expected patterns
        if grep -q "Warning: Undefined variable" "$RESULTS_DIR/${test_name}.out"; then
            echo -e "${YELLOW}⚠ Note: Undefined variable warnings detected${NC}"
        fi
        
    else
        echo -e "${RED}✗ FAILED${NC}: $test_name"
        echo "Error output:"
        tail -5 "$RESULTS_DIR/${test_name}.out"
    fi
    
    echo "Full output saved to: $RESULTS_DIR/${test_name}.out"
}

# Test 1: Basic Enhanced Variables
echo -e "\n=== Test 1: Basic Variable Assignment and Lookup ==="
run_test "$TEST_DIR/test_enhanced_variables.pp"

# Test 2: Variable Scoping
echo -e "\n=== Test 2: Variable Scoping and Lookup Chain ==="
run_test "$TEST_DIR/test_variable_scoping.pp"

# Test 3: Arithmetic Operations
echo -e "\n=== Test 3: Variable Arithmetic and Expressions ==="
run_test "$TEST_DIR/test_variable_arithmetic.pp"

# Test 4: Edge Cases
echo -e "\n=== Test 4: Edge Cases and Error Handling ==="
run_test "$TEST_DIR/test_edge_cases.pp"

# Test 5: Original test file
echo -e "\n=== Test 5: Original Test File ==="
run_test "./test_variables.pp"

echo -e "\n======================================="
echo -e "${GREEN}Test Suite Complete${NC}"
echo "======================================="

# Summary
echo -e "\nTest Summary:"
echo "-------------"
total_tests=5
passed_tests=$(find $RESULTS_DIR -name "*.out" -exec grep -l "Evaluation complete" {} \; | wc -l)
failed_tests=$((total_tests - passed_tests))

echo "Total tests: $total_tests"
echo -e "Passed: ${GREEN}$passed_tests${NC}"
echo -e "Failed: ${RED}$failed_tests${NC}"

if [ $failed_tests -eq 0 ]; then
    echo -e "\n${GREEN}🎉 All tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}❌ Some tests failed. Check output files in $RESULTS_DIR${NC}"
    exit 1
fi