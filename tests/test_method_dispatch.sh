#!/bin/bash
# Item 10 — method-call dispatch: obj.method(args) lowers to method(obj, args)
# so $h.dig(...), [..].sort, {..}.keys[.sort] work (and chain).

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

echo "=== Testing method-call dispatch ==="
echo

FIX="$TMP/m.pp"
cat > "$FIX" <<'PP'
$h = { 'a' => { 'b' => 1 } }
notice($h.dig('a', 'b'))
notice([3, 1, 2].sort)
notice({ 'x' => 10, 'y' => 20 }.keys.sort)
$missing = $h.dig('a', 'nope', 'deep')
notice($missing)
PP

out=$($PUPPETC -e "$FIX" 2>&1)
rc=$?

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; echo "$out" | sed 's/^/      /'; ((FAILED++)); fi; }

echo "$out" | grep -qE 'Notice: 1( |$)';        check "\$h.dig('a','b') => 1"               $?
echo "$out" | grep -qE 'Notice: \[1, 2, 3\]';   check "[3,1,2].sort => [1, 2, 3]"           $?
echo "$out" | grep -qE 'Notice: \[x, y\]';      check "{x,y}.keys.sort => [x, y]"           $?
echo "$out" | grep -qE 'Notice: undef';         check "\$h.dig('a','nope','deep') => undef" $?
! echo "$out" | grep -qi 'Unknown function';    check "no 'Unknown function'"               $?
[ "$rc" -eq 0 ];                                check "clean exit ($rc)"                    $?

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
