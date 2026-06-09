#!/bin/bash
# Item 22 (real cause) — a hash/array index whose KEY is itself a chained index.
#
# `$a[$b['x']['y']]['id']` was mis-grouped by the parser as
# `$a[$b['x']]['y']['id']`: the trailing ['y'] of the key leaked onto the outer
# access chain, so the key became `$b['x']` (often a Hash) instead of
# `$b['x']['y']` (the intended scalar). The real trigger was
# percona_mmm/master.pp:33:
#     $server_id = $server_id_tmp[$facts['networking']['hostname']]['id']
# where $facts['networking'] (a Hash) became the lookup key, yielding undef and
# then "Operator '[]' is not applicable to an Undef Value".
#
# Fix: build_index_expr converts the access_element key with
# convert_access_element_expr, which keeps a chained-access key intact.

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

echo "=== Testing index whose key is itself a chained index ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# 1) The core shape: inline nested-index key, then a trailing index.
F1="$TMP/inline.pp"
cat > "$F1" <<'PP'
node default {
  $a = { 'host-a' => { 'id' => 79 } }
  $b = { 'x' => { 'y' => 'host-a' } }
  notice("inline=${$a[$b['x']['y']]['id']}")
}
PP
out=$("$PUPPETC" -e "$F1" 2>&1)
echo "$out" | grep -qE 'inline=79' && echo "$out" | grep -qvE 'not applicable to an Undef'
check "\$a[\$b['x']['y']]['id'] resolves to 79 (no undef[])" $? "$out"

# 2) Equivalence: the inline form matches the via-variable form.
F2="$TMP/equiv.pp"
cat > "$F2" <<'PP'
node default {
  $a = { 'host-a' => { 'id' => 79 } }
  $b = { 'x' => { 'y' => 'host-a' } }
  $k = $b['x']['y']
  notice("viakey=${$a[$k]['id']} inline=${$a[$b['x']['y']]['id']}")
}
PP
out=$("$PUPPETC" -e "$F2" 2>&1)
echo "$out" | grep -qE 'viakey=79 inline=79'
check "inline key matches the via-variable form" $? "$out"

# 3) Deeper nesting in the key (3 levels).
F3="$TMP/triple.pp"
cat > "$F3" <<'PP'
node default {
  $a = { 'host-a' => { 'id' => 79 } }
  $h = { 'k' => { 'k2' => { 'k3' => 'host-a' } } }
  notice("triple=${$a[$h['k']['k2']['k3']]['id']}")
}
PP
echo "$("$PUPPETC" -e "$F3" 2>&1)" | grep -qE 'triple=79'
check "3-level nested key resolves" $?

# 4) The real percona_mmm/master.pp shape with a fact-derived key.
F4="$TMP/real.pp"
cat > "$TMP/facts.yaml" <<'YAML'
---
facts:
  n:
    networking: {hostname: host-a}
YAML
cat > "$F4" <<'PP'
node 'n' {
  $server_id_tmp = { 'host-a' => { 'id' => 79 }, 'host-b' => { 'id' => 80 } }
  $server_id = $server_id_tmp[$facts['networking']['hostname']]['id']
  notice("server_id=${server_id}")
}
PP
out=$("$PUPPETC" -e -n n -f "$TMP/facts.yaml" "$F4" 2>&1)
echo "$out" | grep -qE 'server_id=79' && echo "$out" | grep -qvE 'not applicable to an Undef'
check "fact-derived chained key (\$h[\$facts['networking']['hostname']]['id'])" $? "$out"

# 5) A genuinely-missing key still yields undef (no over-eager resolution).
F5="$TMP/missing.pp"
cat > "$F5" <<'PP'
node default {
  $a = { 'host-a' => { 'id' => 79 } }
  $b = { 'x' => { 'y' => 'nope' } }
  notice("missing=${$a[$b['x']['y']]}")
}
PP
out=$("$PUPPETC" -e "$F5" 2>&1)
echo "$out" | grep -qE 'missing= '
check "missing key still yields undef (not a wrong value)" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
