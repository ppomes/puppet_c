#!/bin/bash
# Item 34 — the -a global pre-pass evaluates site.pp top-scope with NO facts.
#
# In -a mode the top-level statements run once for registration (no node
# bound, $facts undef), then re-run per node with real facts. The strict
# chained-[] check (item 7) fired in that factless pre-pass — e.g.
# `$jbossenv = $facts['networking']['hostname'] ? {...}` — a context real
# Puppet never has, and the single diagnostic counted as 2 errors. Now the
# pre-pass is silent for that check (per-node evaluation does the real one)
# and one diagnostic counts once. Side note: facts that still exist under
# Puppet 8 (puppetversion, clientcert) get softer lint wording.

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

echo "=== Testing -a global pre-pass vs per-node evaluation (item 34) ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

mkdir -p "$TMP/manifests"
printf 'facts:\n  web1.example.com:\n    networking: { hostname: web1 }\n  db1.example.com:\n    networking: { hostname: db1 }\n' > "$TMP/facts.yaml"

# 1) Fact-dependent top-scope selector: -a must be clean and agree with -e.
cat > "$TMP/manifests/site.pp" <<'PP'
$jbossenv = $facts['networking']['hostname'] ? {
  /^web/  => 'prod',
  default => 'adm',
}
node default { notice("host=${facts['networking']['hostname']} env=${jbossenv}") }
PP
out=$(cd "$TMP" && "$PUPPETC" -a -s -f facts.yaml . 2>&1)
echo "$out" | grep -qE 'Total errors: +0' && echo "$out" | grep -qE 'Status: OK'
check "-a: \$facts[...] at top scope -> 0 errors, Status OK" $? "$out"

# 2) Per-node values are computed from each node's facts (not the pre-pass).
vals=$(cd "$TMP" && "$PUPPETC" -a -f facts.yaml . 2>&1 | grep -oE 'host=[a-z0-9]+ env=[a-z]+' | sort | tr '\n' ' ')
[ "$vals" = "host=db1 env=adm host=web1 env=prod " ]
check "-a: per-node top-scope re-evaluation (got: $vals)" $?

# 3) -e agrees.
out=$(cd "$TMP" && "$PUPPETC" -e -s -n web1.example.com -f facts.yaml . 2>&1)
echo "$out" | grep -qE 'Total errors: +0'
check "-e -n: same tree, 0 errors (modes agree)" $? "$out"

# 4) A genuinely-broken chained [] still errors, count == diagnostics, in BOTH modes.
cat > "$TMP/manifests/site.pp" <<'PP'
$h = { 'a' => 1 }
$y = $h['x']['y']
node default { notify { 'x': } }
PP
out=$(cd "$TMP" && "$PUPPETC" -e -s -n web1.example.com -f facts.yaml . 2>&1)
d=$(echo "$out" | grep -c 'not applicable to an Undef')
echo "$out" | grep -qE "Total errors: +$d" && [ "$d" -ge 1 ]
check "-e: broken chain errors; Total errors == diagnostics ($d)" $? "$out"
out=$(cd "$TMP" && "$PUPPETC" -a -s -f facts.yaml . 2>&1)
d=$(echo "$out" | grep -c 'not applicable to an Undef')
echo "$out" | grep -qE "Total errors: +$d" && [ "$d" -ge 1 ]
check "-a: broken chain still reported (per node); count == diagnostics ($d)" $? "$out"

# 5) Wording: still-existing facts soft, removed facts hard.
cat > "$TMP/manifests/site.pp" <<'PP'
node default { $v = $puppetversion $h = $hostname notice($v) }
PP
out=$(cd "$TMP" && "$PUPPETC" -e -s -n web1.example.com -f facts.yaml . 2>&1)
echo "$out" | grep -qE '\$puppetversion still works under Puppet 8, but prefer' && \
echo "$out" | grep -qE '\$hostname is a legacy top-scope fact removed'
check "puppetversion soft wording; hostname hard wording" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
