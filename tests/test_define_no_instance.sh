#!/bin/bash
# Item 20 — a defined-type body must not be evaluated unless an instance is
# actually declared. (On the real adm tree, apt::ppa.pp:93 / shell_join($options)
# was crashing on nodes whose catalog contains zero Apt::Ppa resources.)
#
# This guards the documented fixture (no instance -> body never runs, exactly
# one notify resource) and exercises the --verbose define-trace diagnostic,
# which names the declaration that triggers each descent into a define body
# (the tool for pinpointing a spurious instantiation on a real node).

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

echo "=== Testing defined-type body not evaluated without an instance ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# Fixture: a define whose body would crash on its default (undef.length), but no
# instance is declared.
F="$TMP/noinst.pp"
cat > "$F" <<'PP'
define needs_array(Array $a) {
  notice("len=${a.length}")
}
node default {
  notify { 'no_define_call_here': }
}
PP

# 1) Compiles cleanly: the body (which would error) is never entered.
out=$("$PUPPETC" -s "$F" 2>&1)
echo "$out" | grep -qE 'Total errors: +0'
check "no instance: 0 errors (define body not evaluated)" $? "$out"

# 2) The define body does not run in -a mode either (no notice, no trace).
out=$("$PUPPETC" -v -a -f /dev/null "$F" 2>&1)
echo "$out" | grep -qvE 'len=|define-trace'
check "no instance: body not entered under -a -v" $? "$out"

# 3) Catalog holds exactly the one notify and zero define resources.
"$PUPPETC" -c -n default "$F" 2>/dev/null > "$TMP/cat.json"
python3 - "$TMP/cat.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
res = d.get('resources') or d.get('catalog', {}).get('resources') or []
from collections import Counter
c = Counter(r.get('type', '').lower() for r in res)
sys.exit(0 if c.get('notify', 0) == 1 and c.get('needs_array', 0) == 0 else 1)
PY
check "catalog: exactly 1 notify, 0 define resources" $?

# 4) When an instance IS declared, the body runs and --verbose names the
#    declaration site (the diagnostic for spurious instantiation).
G="$TMP/inst.pp"
cat > "$G" <<'PP'
define needs_array(Array $a) { notice("len=${a.length}") }
node default {
  needs_array { 'real': a => [1, 2, 3] }
  notify { 'x': }
}
PP
out=$("$PUPPETC" -v -a -f /dev/null "$G" 2>&1)
echo "$out" | grep -qE "define-trace\] node default: entering needs_array\['real'\] \(declared at .*inst.pp:3\)"
check "real instance: --verbose trace names the declaration (file:line)" $? "$out"
echo "$out" | grep -qE 'len=3'
check "real instance: body runs (len=3)" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
