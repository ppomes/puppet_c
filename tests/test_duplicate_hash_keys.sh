#!/bin/bash
# Item 12 — duplicate hash keys.
#
# Puppet 8 turns the Puppet 7 "last wins" silent overwrite into a fatal error:
# a literal key declared more than once in the same hash is rejected. Our
# all-literals hash fast path used to collapse `{ 'a'=>1, 'a'=>3 }` via
# puppet_hash_set (dropping the earlier entry), hiding the duplicate. We now
# keep the un-collapsed form when a duplicate literal key is present and report
# it from the Puppet 8 lint pass. Variable/dynamic keys can't be proven at
# compile time and are left to runtime.

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

echo "=== Testing duplicate hash key detection (Puppet 8 fatal error) ==="
echo

dup_count() { echo "$1" | grep -c 'declared more than once'; }
check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# 1) Acceptance fixture: 1 error for the duplicate 'a'; names the key.
F1="$TMP/acc.pp"
cat > "$F1" <<'PP'
$keys = {
  'a' => 1,
  'b' => 2,
  'a' => 3,
}
$dynamic = {
  $name1 => 1,
  $name2 => 2,
}
PP
out=$("$PUPPETC" -s "$F1" 2>&1)
[ "$(dup_count "$out")" -eq 1 ] && echo "$out" | grep -qE "key 'a' is declared more than once"
check "fixture: exactly 1 error naming duplicate key 'a'" $? "$out"

# 2) The variable-keyed hash alone produces no error.
F2="$TMP/var.pp"
cat > "$F2" <<'PP'
node default {
  $h = { $a => 1, $b => 2 }
}
PP
out=$("$PUPPETC" -s "$F2" 2>&1)
echo "$out" | grep -qE 'Total errors: +0'
check "variable-keyed hash: no error (can't prove at compile time)" $? "$out"

# 3) Duplicate key with DYNAMIC values is still caught (not just all-literal).
F3="$TMP/dyn.pp"
cat > "$F3" <<'PP'
node default {
  $x = 5
  $h = { 'k' => $x, 'k' => 6 }
}
PP
[ "$(dup_count "$("$PUPPETC" -s "$F3" 2>&1)")" -eq 1 ]
check "dup key with dynamic values is caught" $?

# 4) Nested hash duplicate is caught.
F4="$TMP/nest.pp"
cat > "$F4" <<'PP'
$h = { 'outer' => { 'x' => 1, 'x' => 2 } }
PP
[ "$(dup_count "$("$PUPPETC" -s "$F4" 2>&1)")" -eq 1 ]
check "nested hash duplicate is caught" $?

# 5) No false positives: distinct keys, mixed variable key, case-distinct keys.
F5="$TMP/ok.pp"
cat > "$F5" <<'PP'
$h1 = { 'a' => 1, 'b' => 2, 'c' => 3 }
$h2 = { $v => 1, 'a' => 2 }
$h3 = { 'a' => 1, 'A' => 2 }
PP
out=$("$PUPPETC" -s "$F5" 2>&1)
echo "$out" | grep -qE 'Total errors: +0'
check "distinct / variable / case-distinct keys: no false positives" $? "$out"

# 6) The compile fails (non-zero exit) when a duplicate is present.
"$PUPPETC" -s "$F1" >/dev/null 2>&1
[ "$?" -ne 0 ]
check "duplicate key makes the compile fail (non-zero exit)" $?

# --- Item 36: evaluation-time detection ------------------------------------

# 7) Keys that only collide AFTER interpolation error at evaluation.
F7="$TMP/interp.pp"
cat > "$F7" <<'PP'
node default {
  $vlan = '122'
  $h = {
    "ituser@10.2.${vlan}.%/db" => 1,
    "ituser@10.2.122.%/db"     => 1,
  }
  notice('done')
}
PP
out=$("$PUPPETC" -s -e "$F7" 2>&1)
echo "$out" | grep -qE "The key 'ituser@10.2.122.%/db' is declared more than once"
check "item 36: post-interpolation duplicate detected at evaluation" $? "$out"

# 8) Distinct interpolations do NOT error, and interpolated-key entries are
#    no longer silently dropped from all-literal-value hashes.
F8="$TMP/distinct.pp"
cat > "$F8" <<'PP'
node default {
  $a = 'x'
  $h = { "k${a}" => 'v', "j${a}" => 'w' }
  notice("kept=${h['kx']}${h['jx']}")
}
PP
out=$("$PUPPETC" -e "$F8" 2>&1)
echo "$out" | grep -qE 'kept=vw' && echo "$out" | grep -qv 'declared more than once'
check "item 36: distinct interpolated keys kept, no false positive" $? "$out"

# 9) Duplicate literal keys in a MODULE file (lazily loaded) are detected —
#    real Puppet 8 fails language validation on module files too.
mkdir -p "$TMP/modules/dup36/manifests"
cat > "$TMP/modules/dup36/manifests/init.pp" <<'PP'
class dup36 {
  $users = {
    'heartbeat@host.preprod' => 1,
    'heartbeat@host.preprod' => 1,
  }
}
PP
F9="$TMP/site36.pp"
printf 'node default { include dup36 }\n' > "$F9"
out=$("$PUPPETC" -s -m "$TMP/modules" "$F9" 2>&1)
echo "$out" | grep -qE "key 'heartbeat@host.preprod' is declared more than once"
check "item 36: duplicate literal key in module file detected" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
