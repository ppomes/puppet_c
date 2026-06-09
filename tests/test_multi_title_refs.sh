#!/bin/bash
# Item 9 — Resource[$array] multi-title references resolve per element instead of
# being stringified into one bogus key like Notify[[a, b, c]].

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

echo "=== Testing Resource[\$array] multi-title references ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# 1) All elements present -> 0 errors (was: "Could not find resource Notify[[a, b, c]]")
OK="$TMP/ok.pp"
cat > "$OK" <<'PP'
node default {
  notify { ['a', 'b', 'c']: }
  $titles = ['a', 'b', 'c']
  notify { 'gate': require => Notify[$titles] }
}
PP
out=$($PUPPETC -s "$OK" 2>&1)
echo "$out" | grep -qE 'Status: OK' && echo "$out" | grep -qvE 'Could not find'
check "all-present multi-title ref compiles cleanly" $? "$out"

# 2) Per-element resolution: a missing element is flagged as Notify[absent],
#    NOT as a stringified array.
MISS="$TMP/miss.pp"
cat > "$MISS" <<'PP'
node default {
  notify { 'a': }
  notify { 'gate2': require => Notify[['a', 'absent']] }
}
PP
out=$($PUPPETC -s "$MISS" 2>&1)
echo "$out" | grep -qE "Could not find resource 'Notify\[absent\]'"
check "missing element flagged per-element (Notify[absent])" $? "$out"
echo "$out" | grep -qvE 'Notify\[\['   # no stringified-array key
check "no stringified-array key in error" $? "$out"

# 3) Regression: a single (scalar) missing ref still errors.
SINGLE="$TMP/single.pp"
cat > "$SINGLE" <<'PP'
node default {
  notify { 'a': }
  notify { 'g': require => Notify['absent'] }
}
PP
out=$($PUPPETC -s "$SINGLE" 2>&1)
echo "$out" | grep -qE "Could not find resource 'Notify\[absent\]'"
check "single-title missing ref still errors" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
