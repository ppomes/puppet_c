#!/bin/bash
# Item 21 — the fact() built-in (Puppet 6+).
#
# fact('os.distro.codename') is sugar for $facts['os']['distro']['codename']
# (a dotted, optionally array-indexed path). Our interpreter didn't implement
# it ("Unknown function: fact"), so apt::ppa's
#   $release = fact('os.distro.codename')
# stayed unresolved, the generated sources filename never matched
# $facts['apt_sources'], the `unless … in …` guard never short-circuited, and
# shell_join($options /*undef*/) crashed. fact() now walks the structured
# $facts hash, returning undef for a missing segment (like dig()).

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

echo "=== Testing the fact() built-in (dotted-path \$facts lookup) ==="
echo

cat > "$TMP/facts.yaml" <<'YAML'
---
facts:
  fact-fn-test:
    networking:
      hostname: fact-fn-test
    os:
      distro:
        codename: noble
        id: Ubuntu
      family: Debian
    mountpoints: ['/', '/boot']
    apt_sources: ['adiscon-ubuntu-v8-stable-noble.sources', 'other.list']
YAML

run() { "$PUPPETC" -e -n fact-fn-test -f "$TMP/facts.yaml" "$1" 2>&1; }
check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# 1) Acceptance fixture: nested-path lookups; missing path -> empty (undef).
F1="$TMP/acc.pp"
cat > "$F1" <<'PP'
node 'fact-fn-test' {
  $codename = fact('os.distro.codename')
  $hostname = fact('networking.hostname')
  $missing  = fact('nothing.here')
  notice("codename=${codename}, host=${hostname}, missing=${missing}")
}
PP
out=$(run "$F1")
echo "$out" | grep -qE 'codename=noble, host=fact-fn-test, missing=' && echo "$out" | grep -qvE 'Unknown function'
check "fact() resolves nested paths; missing -> undef" $? "$out"

# 2) Array index segment and a deep-missing path (no error, just undef).
F2="$TMP/idx.pp"
cat > "$F2" <<'PP'
node 'fact-fn-test' {
  notice("fam=${fact('os.family')}")
  notice("mp0=${fact('mountpoints.0')}")
  notice("deep=${fact('os.distro.nope.deeper')}")
}
PP
out=$(run "$F2")
echo "$out" | grep -qE 'fam=Debian' && echo "$out" | grep -qE 'mp0=/' && echo "$out" | grep -qE 'deep= '
check "array index ('.0') works; deep-missing path -> undef" $? "$out"

# 3) The apt::ppa pattern: filename built from fact() matches apt_sources, so
#    the `unless … in …` guard short-circuits (the real bug's mechanism).
F3="$TMP/ppa.pp"
cat > "$F3" <<'PP'
node 'fact-fn-test' {
  $fn = "adiscon-ubuntu-v8-stable-${fact('os.distro.codename')}.sources"
  notice("fn=${fn}")
  notice("present=${$fn in $facts['apt_sources']}")
  if $fn in $facts['apt_sources'] { notify { 'already-present': } }
  else { notify { 'would-add': } }
}
PP
out=$(run "$F3")
echo "$out" | grep -qE 'fn=adiscon-ubuntu-v8-stable-noble\.sources' && echo "$out" | grep -qE 'present=true'
check "apt::ppa filename resolves and matches apt_sources (guard skips)" $? "$out"

# 4) Compiles with 0 errors and fact() is no longer 'Unknown function'.
out=$(run "$F1")
echo "$out" | grep -qE 'Total errors: +0' || "$PUPPETC" -s -e -n fact-fn-test -f "$TMP/facts.yaml" "$F1" 2>&1 | grep -qE 'Total errors: +0'
check "fact() recognised (no 'Unknown function: fact'), 0 errors" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
