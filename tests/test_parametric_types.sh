#!/bin/bash
# Item 6 refinement — value_matches_type_str now validates Enum / Pattern /
# Variant (previously accepted silently). Exercised via the function-arg
# type-check path.

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

echo "=== Testing Enum / Pattern / Variant type checking ==="
echo

FIX="$TMP/param.pp"
cat > "$FIX" <<'PP'
function ne(Enum['red','green'] $c) >> String { $c }
function np(Pattern[/^[0-9]+$/] $n) >> String { $n }
function nv(Variant[Integer, Boolean] $x) >> String { 'ok' }

notice(ne('red'))
notice(ne('blue'))
notice(np('123'))
notice(np('abc'))
notice(nv(5))
notice(nv(true))
notice(nv('s'))
PP
out=$($PUPPETC -e "$FIX" 2>&1)

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; echo "$out" | sed 's/^/      /'; ((FAILED++)); fi; }

n=$(echo "$out" | grep -c 'expected .* got incompatible value')
[ "$n" -eq 3 ];                                   check "exactly 3 type errors (got $n)" $?
echo "$out" | grep -qE "ne' parameter \\\$c: expected Enum"
check "Enum: 'blue' rejected"                     $?
echo "$out" | grep -qE "np' parameter \\\$n: expected Pattern"
check "Pattern: 'abc' rejected"                   $?
echo "$out" | grep -qE "nv' parameter \\\$x: expected Variant"
check "Variant: 's' rejected (Integer|Boolean)"   $?
# Valid calls must not be among the errors: ne('red'), np('123'), nv(5), nv(true).
echo "$out" | grep -E 'expected .* got incompatible' | grep -qvE ":(4|6|8|9):"
check "valid Enum/Pattern/Variant calls accepted" $?

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
