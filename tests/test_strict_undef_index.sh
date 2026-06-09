#!/bin/bash
# Item 7 — chained [] on a missing intermediate is an error (Puppet 8:
# "Operator '[]' is not applicable to an Undef Value"), while a single index
# yielding undef, or a selector-guarded access, is not.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PUPPETC="$PROJECT_DIR/compiler/.libs/puppetc-compile"

export DYLD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${DYLD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${LD_LIBRARY_PATH:-}"

RED=$'\e[31m'; GREEN=$'\e[32m'; NC=$'\e[0m'
PASSED=0; FAILED=0
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
MSG="not applicable to an Undef Value"

echo "=== Testing strict-undef chained [] ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# 1) Roadmap fixture: only the 3-deep chain on a missing intermediate errors.
FIX="$TMP/undef.pp"
cat > "$FIX" <<'PP'
$net  = { 'interfaces' => {} }
$ip0  = $net['interfaces']['eth0']['ip']
$ip1  = $net['interfaces']['eth0']
$safe = $net['interfaces'] ? {
  Hash    => $net['interfaces']['eth0'],
  default => undef
}
PP
out=$($PUPPETC -e "$FIX" 2>&1)
n=$(echo "$out" | grep -c "$MSG")
[ "$n" -eq 1 ];                          check "exactly one Undef-Value error (got $n)" $? "$out"
echo "$out" | grep -qE ":2:.*$MSG";      check "error attributed to the \$ip0 chain (line 2)" $? "$out"

# 2) Positive control: every intermediate present -> no error.
OK="$TMP/ok.pp"
printf '$h = { "a" => { "b" => { "c" => 1 } } }\n$x = $h["a"]["b"]["c"]\nnotice($x)\n' > "$OK"
out=$($PUPPETC -e "$OK" 2>&1)
echo "$out" | grep -qv "$MSG" && echo "$out" | grep -qE 'Notice: 1'
check "all-keys-present chain yields no error (returns 1)" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
