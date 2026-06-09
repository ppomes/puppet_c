#!/bin/bash
# Item 13 — bare-word top-scope facts removed in Puppet 8.
#
# Legacy facts read either bare ($hostname) or explicit ($::osfamily) are
# removed in Puppet 8 in favour of $facts[...]. We warn by default (error under
# --puppet8-strict-facts) and suggest the structured replacement, while
# respecting shadowing: a preceding local assignment or a class/define
# parameter of the same name means the read refers to the local, not the fact.

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

echo "=== Testing legacy top-scope fact detection (bare + ::) ==="
echo

warn_count() { echo "$1" | grep -c 'legacy top-scope fact'; }
check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# 1) Acceptance fixture: 2 warnings (bare read-before-assign + $::osfamily),
#    the shadowed read silent, 0 errors.
F1="$TMP/acc.pp"
cat > "$F1" <<'PP'
class need_fact {
  notify { "host=${hostname}": }
  $hostname = $facts['networking']['hostname']
  notify { "host=${hostname}": }
}
if $::osfamily == 'Debian' { include apt }
PP
out=$("$PUPPETC" -s "$F1" 2>&1)
[ "$(warn_count "$out")" -eq 2 ] && echo "$out" | grep -qE 'Total errors: +0'
check "fixture: exactly 2 fact warnings, 0 errors" $? "$out"
echo "$out" | grep -qE "\\\$hostname is a legacy" && echo "$out" | grep -qE "\\\$::osfamily is a legacy"
check "both bare \$hostname and \$::osfamily flagged, with suggestions" $? "$out"

# 2) --puppet8-strict-facts turns the warnings into errors.
out=$("$PUPPETC" -s --puppet8-strict-facts "$F1" 2>&1)
echo "$out" | grep -qE 'Total errors: +2'
check "--puppet8-strict-facts: 2 errors instead of warnings" $? "$out"

# 3) Order-sensitivity: a read AFTER the assignment is NOT flagged (shadowed),
#    a read BEFORE is. The fixture's first ${hostname} warns, second doesn't —
#    verify only line 2 is flagged for hostname.
out=$("$PUPPETC" -s "$F1" 2>&1)
echo "$out" | grep -E '\$hostname is a legacy' | grep -qE ':2:'
check "read-before-assign flagged; read-after shadowed (line 2 only)" $? "$out"

# 4) Class/define parameters of the same name shadow the fact.
F4="$TMP/param.pp"
cat > "$F4" <<'PP'
class c(String $fqdn) {
  notify { "a=${fqdn}": }
}
define d(String $kernel) {
  notify { "k=${kernel}": }
}
PP
out=$("$PUPPETC" -s "$F4" 2>&1)
[ "$(warn_count "$out")" -eq 0 ]
check "class/define parameters shadow facts (no warning)" $? "$out"

# 5) No false positives: $environment (builtin), qualified $apache::port,
#    and $facts/$trusted are never flagged.
F5="$TMP/ok.pp"
cat > "$F5" <<'PP'
class e {
  notify { "env=${environment}": }
  $p = $apache::port
  $h = $facts['networking']['hostname']
  $t = $trusted['certname']
}
PP
out=$("$PUPPETC" -s "$F5" 2>&1)
[ "$(warn_count "$out")" -eq 0 ]
check "environment / qualified var / \$facts / \$trusted not flagged" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
