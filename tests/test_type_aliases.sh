#!/bin/bash
# Item 18 — user-defined type aliases (`type Stdlib::Fqdn = Pattern[/.../]`).
#
# Before this, `$fqdn =~ Stdlib::Fqdn` was always false (the matcher returned
# false for any non-builtin type name), so `unless $fqdn =~ Stdlib::Fqdn { fail }`
# wrongly failed. Now `type X = <type>` is parsed and registered, and the type
# matcher resolves named aliases — both for `=~` and for typed parameters. Module
# aliases under <module>/types/*.pp are loaded lazily on first reference.

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

echo "=== Testing user-defined type aliases (type X = ...) ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# 1) Acceptance fixture: inline alias, valid FQDN -> unless skips, 0 errors.
F1="$TMP/acc.pp"
cat > "$F1" <<'PP'
type Stdlib::Fqdn = Pattern[/^[a-z0-9.-]+$/]
node default {
  $f = 'host1.example.com'
  unless $f =~ Stdlib::Fqdn { fail("${f} is not a valid Fqdn") }
  notify { 'OK': }
}
PP
out=$("$PUPPETC" -s "$F1" 2>&1)
echo "$out" | grep -qvE 'not a valid Fqdn|Critical' && echo "$out" | grep -qE 'Total errors: +0'
check "inline alias: valid FQDN matches, unless skipped" $? "$out"

# 2) Invalid value still fails (alias resolution returns false -> unless runs).
F2="$TMP/neg.pp"
cat > "$F2" <<'PP'
type Stdlib::Fqdn = Pattern[/^[a-z0-9.-]+$/]
node default {
  $f = 'NOT VALID!!'
  unless $f =~ Stdlib::Fqdn { fail("${f} is not a valid Fqdn") }
}
PP
"$PUPPETC" -s "$F2" 2>&1 | grep -qE 'not a valid Fqdn'
check "inline alias: invalid value still fails (no false negative)" $?

# 3) Alias as a typed parameter: valid accepted, invalid rejected.
F3="$TMP/param.pp"
cat > "$F3" <<'PP'
type Stdlib::Fqdn = Pattern[/^[a-z0-9.-]+$/]
define usefqdn(Stdlib::Fqdn $host) {}
node default {
  usefqdn { 'good': host => 'ok.example.com' }
  usefqdn { 'bad':  host => 'BAD HOST' }
}
PP
out=$("$PUPPETC" -s "$F3" 2>&1)
n=$(echo "$out" | grep -c "parameter .host expected Stdlib::Fqdn, got incompatible value")
[ "$n" -eq 1 ]
check "alias typed param: exactly 1 rejection (bad host), good accepted (got $n)" $? "$out"

# 4) Variant alias referencing builtins resolves (recursion through the alias).
F4="$TMP/variant.pp"
cat > "$F4" <<'PP'
type My::Scalar = Variant[Integer, Boolean]
define needscalar(My::Scalar $v) {}
node default {
  needscalar { 'a': v => 5 }       # valid (Integer)
  needscalar { 'b': v => true }    # valid (Boolean)
  needscalar { 'c': v => 'str' }   # invalid
}
PP
out=$("$PUPPETC" -s "$F4" 2>&1)
n=$(echo "$out" | grep -c "expected My::Scalar, got incompatible value")
[ "$n" -eq 1 ]
check "Variant alias resolves (only 'str' rejected, got $n)" $? "$out"

# 5) Lazy-loaded module alias from <module>/types/fqdn.pp.
mkdir -p "$TMP/mods/stdlib/types/ip"
printf 'type Stdlib::Fqdn = Pattern[/^[a-z0-9.-]+$/]\n' > "$TMP/mods/stdlib/types/fqdn.pp"
printf 'type Stdlib::IP::Address = Pattern[/^[0-9.]+$/]\n' > "$TMP/mods/stdlib/types/ip/address.pp"
F5="$TMP/mod.pp"
cat > "$F5" <<'PP'
node default {
  $f = 'host1.example.com'
  unless $f =~ Stdlib::Fqdn { fail("bad fqdn") }
  $ip = '10.0.0.1'
  unless $ip =~ Stdlib::IP::Address { fail("bad ip") }
  notify { 'OK': }
}
PP
out=$("$PUPPETC" -s -m "$TMP/mods" "$F5" 2>&1)
echo "$out" | grep -qvE 'bad fqdn|bad ip|Critical' && echo "$out" | grep -qE 'Total errors: +0'
check "lazy-load module alias (incl. nested Stdlib::IP::Address)" $? "$out"

# 6) Unknown alias with no definition/module accepts silently (no false positive).
F6="$TMP/unknown.pp"
cat > "$F6" <<'PP'
define usething(Whatever::Unknown $x) {}
node default { usething { 'a': x => 'anything' } }
PP
out=$("$PUPPETC" -s "$F6" 2>&1)
echo "$out" | grep -qE 'Total errors: +0'
check "unknown alias (no definition) accepts silently" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
