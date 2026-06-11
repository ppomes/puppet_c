#!/bin/bash
# Item 37 — bare legacy fact reads in .pp must ERROR at evaluation, and
# module-walk lint warnings must be counted in the summary.
#
# Real Puppet 8 (strict_variables) raises `Unknown variable: 'lsbdistcodename'`
# for a bare removed-legacy fact read — the catalog fails. Our evaluator used
# to resolve such reads through the facts fallback and only emit the item-13
# lint WARNING, whose count was additionally dropped from the summary for
# lazily-loaded module files. Now: the facts fallback no longer resolves
# removed legacy names (survivors still do), the variable-miss path emits the
# real-Puppet error, and module-walk lint counts land in the totals.

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

echo "=== Testing bare legacy fact reads fail at evaluation (item 37) ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

mkdir -p "$TMP/manifests" "$TMP/modules/rnode/manifests"
printf 'facts:\n  n.example.com:\n    lsbdistcodename: focal\n    networking: { hostname: n }\n' > "$TMP/facts.yaml"

# 1) The item's repro: a module class reading $lsbdistcodename — error, node
#    failed, and the lint warning COUNTED in the summary.
cat > "$TMP/modules/rnode/manifests/init.pp" <<'PP'
class rnode {
  $cn = $lsbdistcodename
  notice("cn=${cn}")
}
PP
printf 'node default { include rnode }\n' > "$TMP/manifests/site.pp"
out=$(cd "$TMP" && "$PUPPETC" -e -s -n n.example.com -f facts.yaml . 2>&1)
echo "$out" | grep -qE "Unknown variable: 'lsbdistcodename'" && \
echo "$out" | grep -qE 'Nodes failed: +1' && \
echo "$out" | grep -qE 'Total errors: +1'
check "module bare read: Unknown variable error, node failed" $? "$out"
echo "$out" | grep -qE 'Total warnings: +1'
check "module-walk lint warning now counted in the summary" $? "$out"

# 2) A shadowing assignment keeps working; survivors keep resolving.
cat > "$TMP/modules/rnode/manifests/init.pp" <<'PP'
class rnode {
  $lsbdistcodename = 'jammy'
  notice("shadow=${lsbdistcodename}")
  $e = $environment
  notice("env-ok")
}
PP
out=$(cd "$TMP" && "$PUPPETC" -e -s -n n.example.com -f facts.yaml . 2>&1)
echo "$out" | grep -qE 'shadow=jammy' && echo "$out" | grep -qv "Unknown variable" && \
echo "$out" | grep -qE 'Total errors: +0'
check "shadowed read + survivor (\$environment): no error" $? "$out"

# 3) The structured replacement compiles clean (the migrated form).
cat > "$TMP/modules/rnode/manifests/init.pp" <<'PP'
class rnode {
  $cn = $facts['lsbdistcodename']
  notice("cn=${cn}")
}
PP
out=$(cd "$TMP" && "$PUPPETC" -e -s -n n.example.com -f facts.yaml . 2>&1)
echo "$out" | grep -qE 'Total errors: +0' && echo "$out" | grep -qE 'Total warnings: +0'
check "migrated \$facts['...'] form: clean (0/0)" $? "$out"

# 4) Hiera %{::fqdn}-style interpolation is exempt (level goes empty, no crash).
mkdir -p "$TMP/hieradata/local"
printf -- '---\nrnode::v: from-global\n' > "$TMP/hieradata/global.yaml"
cat > "$TMP/hiera.yaml" <<'YAML'
---
:hierarchy:
  - "%{::fqdn}"
  - "global"
:yaml:
    :datadir: hieradata
YAML
cat > "$TMP/modules/rnode/manifests/init.pp" <<'PP'
class rnode($v = 'default') { notice("v=${v}") }
PP
out=$(cd "$TMP" && "$PUPPETC" -e -s -n n.example.com -f facts.yaml -D hiera.yaml . 2>&1)
echo "$out" | grep -qvE "Unknown variable: 'fqdn'"
check "hiera %{::fqdn} interpolation exempt (no eval error)" $? "$out"

# 5) The factless -a registration pre-pass stays silent for top-scope reads.
printf '$x = $osfamily\nnode default { notice("ok") }\n' > "$TMP/manifests/site.pp"
out=$(cd "$TMP" && "$PUPPETC" -a -s -f facts.yaml . 2>&1)
n=$(echo "$out" | grep -cE "Unknown variable: 'osfamily'")
[ "$n" -eq 1 ]
check "-a: top-scope read errors once (per-node pass), not in pre-pass (got $n)" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
