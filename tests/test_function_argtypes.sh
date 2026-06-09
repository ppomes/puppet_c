#!/bin/bash
# Item 8 — user-defined function arguments are type-checked against the
# declared parameter types; mismatches error, valid args and untyped params
# stay quiet.

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

echo "=== Testing user-function argument type checking ==="
echo

FIX="$TMP/argtypes.pp"
cat > "$FIX" <<'PP'
function require_string(String[1] $name) >> String { $name }
function dbl(Integer $n) >> Integer { $n * 2 }
function untyped($x) { $x }

$nothere = undef
notice(require_string($nothere))
notice(require_string('ok'))
notice(dbl(21))
notice(dbl('oops'))
notice(untyped(undef))
notice(untyped('anything'))
PP
out=$($PUPPETC -e "$FIX" 2>&1)

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; echo "$out" | sed 's/^/      /'; ((FAILED++)); fi; }

echo "$out" | grep -qE "require_string' parameter \\\$name: expected String\[1\]"
check "undef arg to String[1] param errors"        $?
echo "$out" | grep -qE "dbl' parameter \\\$n: expected Integer"
check "string arg to Integer param errors"         $?
n=$(echo "$out" | grep -c 'got incompatible value')
[ "$n" -eq 2 ];  check "exactly 2 type errors (got $n) — valid + untyped calls quiet" $?
echo "$out" | grep -qE 'Notice: ok' && echo "$out" | grep -qE 'Notice: 42'
check "valid typed calls succeed (ok / 42)"         $?

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
