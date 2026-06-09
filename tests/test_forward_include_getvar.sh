#!/bin/bash
# Item 22 — forward include() of a class whose variables are read later via
# getvar() / cross-class ($a::b::c) references within the same class.
#
# The documented symptom (percona_mmm/master.pp:33, "Operator '[]' is not
# applicable to an Undef Value") does NOT reproduce on the current tree: our
# compiler already populates included classes' scopes before cross-class
# variable lookups resolve, so `include('X'); $v = getvar('::X::y')` and forward
# `$other::class::var` interpolation both resolve regardless of textual /
# include order. This test locks that behaviour in (acceptance bullet 1) and
# guards the include-before-definition ordering.

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

echo "=== Testing forward include() + getvar / cross-class variable reads ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# 1) The exact Item 22 acceptance fixture: id=79, no undef[] error.
F1="$TMP/acc.pp"
cat > "$F1" <<'PP'
class loaded {
  $hosts = {
    'host-a' => { id => 79 },
    'host-b' => { id => 80 },
  }
}
class reader {
  include 'loaded'
  $tbl = getvar('::loaded::hosts')
  $id  = $tbl['host-a']['id']
  notice("id=${id}")
}
node default { include 'reader' }
PP
out=$("$PUPPETC" -e "$F1" 2>&1)
echo "$out" | grep -qE 'id=79' && echo "$out" | grep -qvE 'not applicable to an Undef'
check "fixture: forward include + getvar resolves to id=79 (no undef[])" $? "$out"

# 2) Module-loaded class via include() with an interpolated name, then getvar
#    on the same scope — the percona_mmm::master shape.
mkdir -p "$TMP/mods/perc/manifests/env/adm"
cat > "$TMP/mods/perc/manifests/env/adm/db.pp" <<'PP'
class perc::env::adm::db { $hosts = { 'h' => { 'id' => 79 } } }
PP
cat > "$TMP/mods/perc/manifests/baseconfig.pp" <<'PP'
class perc::baseconfig { $env = 'adm' }
PP
cat > "$TMP/mods/perc/manifests/master.pp" <<'PP'
class perc::master($cluster) {
  $t_scope = "perc::env::${perc::baseconfig::env}::${cluster}"
  include("perc::env::${perc::baseconfig::env}::${cluster}")
  $tmp = getvar("::${t_scope}::hosts")
  notice("mid=${tmp['h']['id']} mscope=${t_scope}")
}
PP
S2="$TMP/site2.pp"
cat > "$S2" <<'PP'
node default {
  include perc::baseconfig
  class { 'perc::master': cluster => 'db' }
}
PP
out=$("$PUPPETC" -e -m "$TMP/mods" "$S2" 2>&1)
echo "$out" | grep -qE 'mid=79' && echo "$out" | grep -qE 'mscope=perc::env::adm::db'
check "module-loaded include() + interpolated cross-class var resolves" $? "$out"

# 3) Forward ordering: the consumer is declared BEFORE the class that sets the
#    variable it reads. Real Puppet collects then evaluates; we must too.
S3="$TMP/site3.pp"
cat > "$S3" <<'PP'
node default {
  class { 'perc::master': cluster => 'db' }
  include perc::baseconfig
}
PP
out=$("$PUPPETC" -e -m "$TMP/mods" "$S3" 2>&1)
echo "$out" | grep -qE 'mid=79' && echo "$out" | grep -qvE 'not applicable to an Undef'
check "include-before-definition: forward cross-class var still resolves" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
