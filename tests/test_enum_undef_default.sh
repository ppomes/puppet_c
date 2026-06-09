#!/bin/bash
# Item 16 — regression for the item-6 Enum refinement false positive.
#
# Real Puppet strips an explicitly-supplied undef attribute value and applies
# the parameter's default. Our compiler used to keep the undef and type-check
# it against the declared type, so a typed parameter (e.g. Enum['present',
# 'absent']) passed `undef` — typically a selector or hiera lookup that missed —
# produced a false positive once the item-6 refinement made the Enum matcher
# strict. The fix honours the strip-undef-use-default rule at the class and
# define parameter-binding sites.
#
# This test guards both that the false positive is gone AND that genuinely
# wrong values are still rejected, AND that the default is actually bound.

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

echo "=== Testing undef-attribute -> default for typed class/define params ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# 1) undef passed to a typed class/define param with a default => no error.
F1="$TMP/undef_ok.pp"
cat > "$F1" <<'PP'
class myc (Enum['present', 'absent'] $ensure = present) {}
define myd (Enum['present', 'absent'] $ensure = present) {}
node default {
  class { 'myc': ensure => undef }
  myd { 'd1': ensure => undef }
  myd { 'd2': ensure => ($facts['nope'] ? { 'a' => present }) }  # selector miss -> undef
  myd { 'd3': }                                                  # not provided
}
PP
out=$($PUPPETC -s "$F1" 2>&1)
echo "$out" | grep -qvE 'got incompatible value' && echo "$out" | grep -qE 'Total errors: +0'
check "undef/selector-miss/missing use default (0 errors)" $? "$out"

# 2) The bound value is the DEFAULT, not undef (apply phase prints the binding).
F2="$TMP/bound.pp"
cat > "$F2" <<'PP'
define myd (Enum['present', 'absent'] $ensure = present) { notice("BOUND=${ensure}") }
node default {
  myd { 'passed': ensure => absent }
  myd { 'undef':  ensure => undef }
}
PP
bound=$($PUPPETC -a -f /dev/null "$F2" 2>&1 | grep -oE 'BOUND=[a-z]+' | sort | tr '\n' ' ')
[ "$bound" = "BOUND=absent BOUND=present " ]
check "undef binds the default; explicit value preserved (got: $bound)" $?

# 3) Genuinely-wrong values are STILL rejected (the strict check is intact).
F3="$TMP/bad.pp"
cat > "$F3" <<'PP'
class myc (Enum['present', 'absent'] $ensure = present) {}
define myd (Enum['present', 'absent'] $ensure = present) {}
node default {
  class { 'myc': ensure => 'bogus' }
  myd { 'd': ensure => 'nope' }
}
PP
out=$($PUPPETC -s "$F3" 2>&1)
c=$(echo "$out" | grep -c 'got incompatible value')
[ "$c" -eq 2 ]
check "wrong non-undef values still rejected (got $c of 2)" $? "$out"

# 4) undef to a typed param with NO default is still flagged (real Puppet errors too).
F4="$TMP/nodef.pp"
cat > "$F4" <<'PP'
define needs (Enum['present', 'absent'] $e) {}
node default { needs { 'n': e => undef } }
PP
out=$($PUPPETC -s "$F4" 2>&1)
echo "$out" | grep -qE 'got incompatible value'
check "undef to no-default typed param still flagged" $? "$out"

# 5) The roadmap's exact Item 16 fixture compiles with 0 errors.
F5="$TMP/roadmap.pp"
cat > "$F5" <<'PP'
define needs_state(Enum['present','absent'] $e) {}
node default {
  $cond = (true and false)
  $resolved = $cond ? { true => present, false => absent }
  needs_state { 'a': e => $resolved }
  needs_state { 'b': e => present }
  needs_state { 'c': e => 'present' }
}
PP
out=$($PUPPETC -s "$F5" 2>&1)
echo "$out" | grep -qE 'Total errors: +0'
check "roadmap Item 16 fixture compiles cleanly" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
