#!/bin/bash
# Items 27/28 — legacy-fact lint coverage gaps found in the OpenVox 8 trial:
#   27: bareword fact in a class-parameter DEFAULT expression
#       (class c($accepteddomains = [$domain])) — and a default referencing an
#       EARLIER PARAMETER of the same name must NOT be flagged (params shadow).
#   28: ${::fact} interpolation inside double-quoted strings
#       (command => "/bin/cp .../${::fqdn}.pem ...").
# Plus the delivery gap: module manifests are loaded lazily and never passed
# through the entry program's lint — the loader now runs a facts-only lint once
# per parsed module file (funcall/import/dup-key checks stay off there).

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

echo "=== Testing legacy facts in param defaults / interpolation / modules ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# 1) Item 27 fixture: bareword fact in a param default is flagged.
F1="$TMP/d.pp"
cat > "$F1" <<'PP'
class rsyslog_server($env, $device='/dev/xvdb', $accepteddomains=[$domain]) { }
node default { class { 'rsyslog_server': env => 'adm' } }
PP
out=$("$PUPPETC" -s "$F1" 2>&1)
echo "$out" | grep -qE '\$domain is a legacy top-scope fact'
check "bareword \$domain in param default flagged" $? "$out"

# 2) A default referencing an EARLIER PARAM of a fact name is NOT flagged
#    (params shadow facts in the class's own scope).
F2="$TMP/shadow.pp"
cat > "$F2" <<'PP'
class e1(String $hostname, $greeting = "hi ${hostname}") { }
node default { class { 'e1': hostname => 'a' } }
PP
out=$("$PUPPETC" -s "$F2" 2>&1)
[ "$(echo "$out" | grep -cE 'legacy top-scope fact')" -eq 0 ]
check "default referencing earlier param not flagged (shadowed)" $? "$out"

# 3) Item 28 fixture: ${::fact} inside a double-quoted string is flagged.
F3="$TMP/interp.pp"
cat > "$F3" <<'PP'
class t28a { exec { 'cp': command => "/bin/cp /var/lib/ssl/${::fqdn}.pem /tmp/" } }
node default { include t28a }
PP
out=$("$PUPPETC" -s "$F3" 2>&1)
echo "$out" | grep -qE '\$::fqdn is a legacy top-scope fact'
check "\${::fqdn} in double-quoted string flagged" $? "$out"

# 4) Module-loaded manifests are linted too (the 88-sites delivery gap):
#    a class in modules/ with default + interpolation + bareword reads.
mkdir -p "$TMP/mods/m/manifests"
cat > "$TMP/mods/m/manifests/init.pp" <<'PP'
class m($accepteddomains = [$domain]) {
  exec { 'cp': command => "/bin/cp /ssl/${::fqdn}.pem /tmp/" }
  $h = $hostname
}
PP
F4="$TMP/site.pp"
printf 'node default { include m }\n' > "$F4"
out=$("$PUPPETC" -s -m "$TMP/mods" "$F4" 2>&1)
n=$(echo "$out" | grep -cE 'init\.pp:[0-9]+: \$(::)?(domain|fqdn|hostname) is a legacy top-scope fact')
[ "$n" -eq 3 ]
check "module-loaded class linted: 3 sites flagged (got $n)" $? "$out"

# 5) Facts-only in modules: validate_* in a module file does NOT become an
#    error (the full funcall lint stays scoped to the entry program).
cat > "$TMP/mods/m/manifests/v.pp" <<'PP'
class m::v { validate_re('x', 'x') }
PP
F5="$TMP/site2.pp"
printf 'node default { include m::v }\n' > "$F5"
out=$("$PUPPETC" -s -m "$TMP/mods" "$F5" 2>&1)
[ "$(echo "$out" | grep -cE 'validate_re.*removed in Puppet 8')" -eq 0 ]
check "module files: validate_* NOT escalated (facts-only walk)" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
