#!/bin/bash
# Item 33 — module-layer hiera (data-in-modules) automatic parameter lookup.
#
# Real Puppet's lookup tier 3: when a class parameter has no explicit value,
# modules/<mod>/hiera.yaml (v5) is consulted — hierarchy paths interpolated
# with %{facts.x.y} against the node's facts, data files read from
# modules/<mod>/<datadir>/, first hit for "<class>::<param>" wins, manifest
# default only as the last fallback. Real-world target: apt::ppa_options
# (['-y'] from modules/apt/data/os/Ubuntu.yaml) feeding shell_join() in
# apt::ppa — previously undef -> "shell_join() argument must be an array".
#
# Also covers the delivery fix: puppet_loader_include_class (the autoload
# include path every module class takes) previously bound params from
# defaults only — no APL at all.

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

echo "=== Testing module-layer hiera APL (data in modules) ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# --- fixture tree ---------------------------------------------------------
mkdir -p "$TMP/manifests" "$TMP/modules/m33/manifests" \
         "$TMP/modules/m33/data/os/Ubuntu" "$TMP/modules/mbad/manifests" \
         "$TMP/modules/mbad/data"
cat > "$TMP/modules/m33/hiera.yaml" <<'EOF'
---
version: 5
defaults:
  datadir: data
  data_hash: yaml_data
hierarchy:
  - name: "os/major"
    paths:
      - "os/%{facts.os.name}/%{facts.os.release.major}.yaml"
      - "os/%{facts.os.name}.yaml"
  - name: 'common'
    path: 'common.yaml'
EOF
printf 'm33::opt:\n  - -y\n  - --allow-unauth\n' > "$TMP/modules/m33/data/os/Ubuntu/22.yaml"
printf 'm33::opt:\n  - -y\n' > "$TMP/modules/m33/data/os/Ubuntu.yaml"
printf 'm33::opt: []\n' > "$TMP/modules/m33/data/common.yaml"
cat > "$TMP/modules/m33/manifests/init.pp" <<'PP'
class m33 (Optional[Array[String[1]]] $opt = undef, $dflt = 'manifest-default') {
  notice("RESULT opt=<${shell_join($opt)}> dflt=${dflt}")
}
PP
# a module with an unsupported backend: must fall back to defaults + warn once
cat > "$TMP/modules/mbad/hiera.yaml" <<'EOF'
---
version: 5
defaults:
  datadir: data
  data_hash: json_data
hierarchy:
  - name: 'common'
    path: 'common.yaml'
EOF
printf 'mbad::x: from-data\n' > "$TMP/modules/mbad/data/common.yaml"
cat > "$TMP/modules/mbad/manifests/init.pp" <<'PP'
class mbad ($x = 'fallback', $y = 'fallback2') {
  notice("MBAD x=${x} y=${y}")
}
PP
cat > "$TMP/manifests/site.pp" <<'PP'
node /u22/ { include m33 }
node /u20/ { include m33 }
node /deb/ {
  include m33
  include mbad
}
node /exp/ {
  class { 'm33': opt => ['-x'] }
}
PP
cat > "$TMP/facts.yaml" <<'YAML'
facts:
  u22.example.com:
    os: { name: Ubuntu, release: { major: "22" } }
  u20.example.com:
    os: { name: Ubuntu, release: { major: "20" } }
  deb.example.com:
    os: { name: Debian, release: { major: "12" } }
  exp.example.com:
    os: { name: Ubuntu, release: { major: "22" } }
YAML

run() { (cd "$TMP" && "$PUPPETC" -e -n "$1" -f facts.yaml . 2>&1); }

# 1) Two-level path precedence: Ubuntu/22 gets os/Ubuntu/22.yaml.
run u22.example.com | grep -qE 'RESULT opt=<-y --allow-unauth> dflt=manifest-default'
check "Ubuntu/22 resolves os/%{facts.os.name}/%{facts.os.release.major}.yaml" $?

# 2) Fallthrough: Ubuntu/20 (no 20.yaml) gets os/Ubuntu.yaml.
run u20.example.com | grep -qE 'RESULT opt=<-y> '
check "Ubuntu/20 falls through to os/%{facts.os.name}.yaml" $?

# 3) Debian falls to common.yaml ([]), shell_join succeeds — the acceptance shape.
out=$(run deb.example.com)
echo "$out" | grep -qE 'RESULT opt=<> ' && echo "$out" | grep -qv 'must be an array'
check "Debian -> common.yaml empty array; no shell_join error" $? "$out"

# 4) A param with no hiera key keeps the manifest default.
run u22.example.com | grep -qE 'dflt=manifest-default'
check "param without hiera key keeps manifest default" $?

# 5) Explicit declaration wins over module hiera.
run exp.example.com | grep -qE 'RESULT opt=<-x> '
check "explicit class { } argument beats module hiera" $?

# 6) Unsupported backend: defaults used, exactly one warning.
out=$(run deb.example.com)
echo "$out" | grep -qE 'MBAD x=fallback y=fallback2'
check "json_data module: params fall back to defaults" $? "$out"
[ "$(echo "$out" | grep -c 'unsupported backend')" -eq 1 ]
check "unsupported-backend warning emitted exactly once" $? "$out"

# 7) Parallel -a -P: per-node values correct and deterministic across runs.
p1=$(cd "$TMP" && "$PUPPETC" -a -P -f facts.yaml . 2>&1 | grep -oE 'RESULT opt=<[^>]*>' | sort | tr '\n' ' ')
p2=$(cd "$TMP" && "$PUPPETC" -a -P -f facts.yaml . 2>&1 | grep -oE 'RESULT opt=<[^>]*>' | sort | tr '\n' ' ')
echo "$p1" | grep -q -- '-y --allow-unauth' && echo "$p1" | grep -q 'opt=<>' && [ "$p1" = "$p2" ]
check "-a -P: per-node values correct + deterministic (got: $p1)" $?

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
