#!/bin/bash
# Regression: if/unless used as an rvalue.
#
#   $pin = if $x { $o = [...]; "release ${o.join(', ')}" }
#
# The parser lowered `if` rvalues to a ternary keeping only the LAST statement
# of each block, so the leading assignment was dropped ("Undefined variable",
# then "join() first argument must be an array"). `unless` rvalues were not
# lowered at all and evaluated to the condition itself (true/false).
# Seen in CI on puppetlabs-apt templates/pin.pref.epp (apt::pin with
# originator/release/...): the whole node failed to compile.

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

echo "=== Testing if/unless used as rvalues ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# notices() FILE : the sorted V=... notices printed while applying FILE
notices() { $PUPPETC -a -f /dev/null "$1" 2>&1 | grep -oE 'V=.* at /' | sed 's/ at \/$//' | sort | tr '\n' '|'; }

# 1) Multi-statement if block: leading assignment runs, value is the last one,
#    and the variable stays visible after the if (no new scope in Puppet).
F1="$TMP/if_block.pp"
cat > "$F1" <<'PP'
node default {
  $p = if true {
    $o = ['a', 'b']
    "r ${o.join(',')}"
  }
  notice("V=1 ${p}")
  notice("V=2 ${o}")
}
PP
got=$(notices "$F1")
[ "$got" = "V=1 r a,b|V=2 [a, b]|" ]
check "if block: assignment kept, last value returned, scope visible (got: $got)" $? "$($PUPPETC -a -f /dev/null "$F1" 2>&1)"

# 2) elsif / else branches with several statements.
F2="$TMP/elsif_block.pp"
cat > "$F2" <<'PP'
node default {
  $a = if false { $x = 1
    'no' } elsif true { $q = 'b'
    "e ${q}" } else { $z = 2
    'z' }
  $b = if false { 'no' } else { $w = 'ww'
    $w }
  notice("V=a ${a}")
  notice("V=b ${b}")
}
PP
got=$(notices "$F2")
[ "$got" = "V=a e b|V=b ww|" ]
check "elsif/else blocks evaluated in full (got: $got)" $? "$($PUPPETC -a -f /dev/null "$F2" 2>&1)"

# 3) unless as an rvalue: body when false, else (or undef) when true.
F3="$TMP/unless.pp"
cat > "$F3" <<'PP'
node default {
  $u = 'U'
  $x = unless $u =~ Undef { 'body' }
  $y = unless $u == 'U' { 'body' }
  $z = unless $u == 'U' { 'body' } else { $e = 'E'
    "else ${e}" }
  $arr = [ unless $u =~ Undef { "o=${u}" }, unless undef =~ Undef { 'v' }, ].filter |$i| { $i != undef }
  notice("V=x ${x}")
  notice("V=y [${y}]")
  notice("V=z ${z}")
  notice("V=arr ${arr}")
}
PP
got=$(notices "$F3")
[ "$got" = "V=arr [o=U]|V=x body|V=y []|V=z else E|" ]
check "unless rvalue (got: $got)" $? "$($PUPPETC -a -f /dev/null "$F3" 2>&1)"

# 4) The puppetlabs-apt pin.pref.epp shape that failed in CI.
mkdir -p "$TMP/mod/modules/t/templates" "$TMP/mod/manifests"
cat > "$TMP/mod/modules/t/templates/pin.epp" <<'EPP'
<%- | $pin_release, $release, $originator | -%>
<%-
$pin =
if $pin_release != '' {
  $options = [
    unless $release =~ Undef         { "a=${release}" },
    unless $originator =~ Undef      { "o=${originator}" },
    ].filter |$x| { $x != undef }
   "release ${options.join(', ')}" }
else {
  'release a=default'
}
-%>
Pin: <%= $pin %>
EPP
cat > "$TMP/mod/manifests/site.pp" <<'PP'
node default {
  file { '/tmp/pin': content => epp('t/pin.epp', { 'pin_release' => 'x', 'release' => undef, 'originator' => 'Ubuntu' }) }
}
PP
out=$($PUPPETC -c -n default -m "$TMP/mod/modules" "$TMP/mod/manifests/site.pp" 2>&1)
echo "$out" | grep -q '"content": "Pin: release o=Ubuntu\\n"' && ! echo "$out" | grep -qE 'Undefined variable|Error'
check "apt pin.pref.epp renders 'Pin: release o=Ubuntu'" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
