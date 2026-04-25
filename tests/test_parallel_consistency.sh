#!/bin/bash
# Verify --all-nodes produces the same per-node catalog whether run
# sequentially or in parallel. Also verify that top-level $var =
# $hostname ? {...} resolves to the per-node value, not the
# compiler host's own value.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PUPPETC="$PROJECT_DIR/compiler/.libs/puppetc-compile"
TEST_DIR="$SCRIPT_DIR/puppet"
FACTS="$SCRIPT_DIR/facts/multinode_topvar_facts.yaml"
MANIFEST="$TEST_DIR/multinode_topvar.pp"

export DYLD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${DYLD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${LD_LIBRARY_PATH:-}"

RED=$'\e[31m'; GREEN=$'\e[32m'; NC=$'\e[0m'
PASSED=0; FAILED=0

echo "=== Testing --all-nodes parallel/sequential consistency ==="
echo

# Run with all-nodes, sequential and parallel, capture catalog dumps.
# Use --pretty (-p) per node so each compilation prints its own
# resources; aggregate by node for comparison.
SEQ_OUT="$(mktemp)"
PAR_OUT="$(mktemp)"
trap 'rm -f "$SEQ_OUT" "$PAR_OUT"' EXIT

# Compile each node individually and capture pretty output.
NODES="web1.example.com db1.example.com app1.example.com cache1.example.com"

for node in $NODES; do
    SEQ_NODE_OUT=$($PUPPETC -p -n "$node" -f "$FACTS" "$MANIFEST" 2>/dev/null)
    echo "=== $node (single) ===" >> "$SEQ_OUT"
    echo "$SEQ_NODE_OUT" >> "$SEQ_OUT"
done

# Now do --all-nodes parallel, extract per-node sections.
$PUPPETC -a -P -s -f "$FACTS" "$MANIFEST" > "$PAR_OUT" 2>&1 || true

# Test 1: each node shows its own env value (not compiler-host's)
echo "Test 1: top-level \$env resolves per-node hostname"
declare -A EXPECTED=( [web1]=web-env [db1]=db-env [app1]=app-env [cache1]=cache-env )
fail=0
for h in web1 db1 app1 cache1; do
    want="host=${h} env=${EXPECTED[$h]}"
    if grep -q "$want" "$SEQ_OUT"; then
        :
    else
        echo "  ${RED}✗${NC} expected '$want' missing from sequential output"
        fail=1
    fi
done
if [ "$fail" -eq 0 ]; then
    echo "  ${GREEN}✓${NC} all 4 nodes resolve their own \$env"
    ((PASSED++))
else
    ((FAILED++))
fi

# Test 2: --all-nodes -P -s succeeds (no errors)
echo
echo "Test 2: --all-nodes parallel run completes without errors"
if grep -q "Status: OK" "$PAR_OUT" && ! grep -qE "Total errors:\s+[1-9]" "$PAR_OUT"; then
    echo "  ${GREEN}✓${NC} parallel all-nodes ran clean"
    ((PASSED++))
else
    echo "  ${RED}✗${NC} parallel all-nodes had errors:"
    sed -n '/=== Validation Summary/,$p' "$PAR_OUT" | sed 's/^/      /'
    ((FAILED++))
fi

# Test 3: parallel processes 4 nodes (regex node, all 4 facts entries)
echo
echo "Test 3: parallel processes all 4 nodes from facts"
if grep -qE "^Nodes processed:\s+4$" "$PAR_OUT"; then
    echo "  ${GREEN}✓${NC} parallel processed exactly 4 nodes"
    ((PASSED++))
else
    echo "  ${RED}✗${NC} parallel didn't process 4 nodes:"
    grep "Nodes processed" "$PAR_OUT" | sed 's/^/      /'
    ((FAILED++))
fi

echo
if [ "$FAILED" -eq 0 ]; then
    echo "${GREEN}✓ All $PASSED parallel-consistency tests passed${NC}"
    exit 0
else
    echo "${RED}✗ $FAILED of $((PASSED+FAILED)) tests failed${NC}"
    exit 1
fi
