#!/bin/bash
# Item 19 — Ruby regex anchors / control escapes in Pattern[/.../].
#
# POSIX regex.h doesn't understand Ruby's \A (start-of-string), \z / \Z
# (end-of-string), or control escapes like \n / \0 (it treats \n as a literal
# 'n'). After item 18 made type aliases resolve, the stdlib path/mode types —
# Stdlib::Filemode = Pattern[/\A([0-7]{1,4})\z/],
# Stdlib::Unixpath = Pattern[/\A\/([^\n\/\0]+\/*)*\z/] — started rejecting valid
# values. puppet_regcomp now rewrites \A->^, \z/\Z->$ and emits the real bytes
# for \n \t \r \f \v (dropping \0), so every regex path (Pattern, =~, case,
# node match) speaks the Ruby-ish dialect.

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

echo "=== Testing Ruby regex anchors / control escapes in Pattern ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# 1) Item 19 acceptance fixture: \A...\z anchored patterns compile with 0 errors.
F1="$TMP/acc.pp"
cat > "$F1" <<'PP'
type Mode = Pattern[/\A([0-7]{1,4})\z/]
type Path = Pattern[/\A\/(.+)\z/]
define need(Mode $m = '0644', Path $p = '/etc/apt') {}
node default { need { 't': } }
PP
out=$("$PUPPETC" -s "$F1" 2>&1)
echo "$out" | grep -qE 'Total errors: +0'
check "fixture: \\A...\\z anchored aliases compile (0 errors)" $? "$out"

# 2) Real stdlib path/mode types with realistic values (path contains 'n').
F2="$TMP/std.pp"
cat > "$F2" <<'PP'
type Stdlib::Unixpath = Pattern[/\A\/([^\n\/\0]+\/*)*\z/]
type Stdlib::Filemode = Pattern[/\A([0-7]{1,4})\z/]
define usepath(Stdlib::Unixpath $dir = '/etc/apt/keyrings', Stdlib::Filemode $m = '0644') {}
node default {
  usepath { 'a': }
  usepath { 'b': dir => '/var/lib/foo/bar-baz_1', m => '755' }
}
PP
out=$("$PUPPETC" -s "$F2" 2>&1)
echo "$out" | grep -qE 'Total errors: +0'
check "stdlib Unixpath/Filemode accept valid path (incl. 'n') and mode" $? "$out"

# 3) Negative: anchored pattern still REJECTS bad values (no over-relaxation).
F3="$TMP/neg.pp"
cat > "$F3" <<'PP'
type Stdlib::Filemode = Pattern[/\A([0-7]{1,4})\z/]
define m(Stdlib::Filemode $x) {}
node default {
  m { 'ok': x => '0644' }
  m { 'b1': x => '9999' }
  m { 'b2': x => 'abcd' }
}
PP
n=$(echo "$("$PUPPETC" -s "$F3" 2>&1)" | grep -c 'incompatible value')
[ "$n" -eq 2 ]
check "anchored pattern still rejects non-octal modes (got $n of 2)" $?

# 4) The =~ match operator honours \A...\z (anchored).
F4="$TMP/match.pp"
cat > "$F4" <<'PP'
node default {
  notice("a=${'0644'   =~ /\A[0-7]+\z/}")   # true
  notice("b=${'xyz'    =~ /\A[0-7]+\z/}")   # false
  notice("c=${'12 34'  =~ /\A[0-7]+\z/}")   # false: anchored, space stops it
}
PP
out=$("$PUPPETC" -a -f /dev/null "$F4" 2>&1 | grep -oE '[abc]=(true|false)' | sort | tr '\n' ' ')
[ "$out" = "a=true b=false c=false " ]
check "=~ honours \\A...\\z anchoring (got: $out)" $?

# 5) \Z behaves like end-of-string for our purposes.
F5="$TMP/zcap.pp"
cat > "$F5" <<'PP'
type T = Pattern[/\A[a-z]+\Z/]
define u(T $s = 'hello') {}
node default { u { 'a': } u { 'b': s => 'WORLD' } }
PP
n=$(echo "$("$PUPPETC" -s "$F5" 2>&1)" | grep -c 'incompatible value')
[ "$n" -eq 1 ]
check "\\Z anchor: 'hello' accepted, 'WORLD' rejected (got $n of 1)" $?

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
