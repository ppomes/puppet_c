#!/bin/bash
# Item 11 — user-defined `function name(Type $x) >> Ret { ... }` definitions:
# they parse, register, and execute (positional args, defaults, return = last
# expression), and are no longer reported as "Unknown function".

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

echo "=== Testing user-defined functions ==="
echo

FIX="$TMP/fn.pp"
cat > "$FIX" <<'PP'
function math::double(Integer $n) >> Integer { $n * 2 }
function strings::greet(String[1] $who = 'world') >> String {
  "hello ${who}"
}

notice(math::double(21))
notice(strings::greet('puppet'))
notice(strings::greet())
PP

out=$($PUPPETC -e "$FIX" 2>&1)
rc=$?

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; echo "$out" | sed 's/^/      /'; ((FAILED++)); fi; }

echo "$out" | grep -qE 'Notice: 42';          check "math::double(21) => 42"            $?
echo "$out" | grep -qE 'Notice: hello puppet'; check "greet('puppet') => 'hello puppet'" $?
echo "$out" | grep -qE 'Notice: hello world';  check "greet() => 'hello world' (default)" $?
! echo "$out" | grep -qi 'Unknown function';   check "no 'Unknown function'"             $?
[ "$rc" -eq 0 ];                               check "clean exit ($rc)"                  $?

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
