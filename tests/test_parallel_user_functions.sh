#!/bin/bash
# Item 17 — parallel mode (-P) must not SEGV when a node uses a top-level
# user-defined function and/or includes a module with metadata.json.
#
# Root cause was that puppet_env_clone_for_node() never initialised the
# per-worker env->user_functions / env->modules_p8_checked hashes (left NULL by
# calloc), so the first puppet_hash_set in a worker dereferenced NULL. Serial
# mode was unaffected. This test reproduces the crash and guards the fix:
#   - 10 consecutive -a -P -s runs must all exit 0 (rule out timing-only crash);
#   - the user function must actually RESOLVE under -P (a fresh empty hash would
#     leave it unresolved since workers don't re-run top-level statements), with
#     output identical to serial mode.

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

echo "=== Testing parallel (-P) compile with user functions + module include ==="
echo

# Module with a Puppet-8-compatible metadata.json (exercises modules_p8_checked).
mkdir -p "$TMP/mods/mymod/manifests"
printf 'class mymod {}\n' > "$TMP/mods/mymod/manifests/init.pp"
printf '{"requirements":[{"name":"puppet","version_requirement":">= 6.0.0 < 9.0.0"}]}\n' \
    > "$TMP/mods/mymod/metadata.json"

cat > "$TMP/facts.yaml" <<'YAML'
---
facts:
  n1.example.com: {fqdn: n1.example.com, hostname: n1}
  n2.example.com: {fqdn: n2.example.com, hostname: n2}
  n3.example.com: {fqdn: n3.example.com, hostname: n3}
  n4.example.com: {fqdn: n4.example.com, hostname: n4}
YAML

cat > "$TMP/site.pp" <<'PP'
function greet(String $h) >> String { "hello ${h}" }
node default {
  include mymod
  notice(greet($facts['hostname']))
}
PP

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# 1) Ten consecutive parallel runs must all exit cleanly (no SEGV / 139).
worst=0
for i in $(seq 1 10); do
    "$PUPPETC" -a -P -s -m "$TMP/mods" -f "$TMP/facts.yaml" "$TMP/site.pp" >/dev/null 2>&1
    rc=$?
    [ "$rc" -ne 0 ] && worst=$rc
done
[ "$worst" -eq 0 ]; check "10x parallel runs all exit 0 (worst rc=$worst)" $?

# 2) The user function resolves under -P, identical to serial mode.
ser=$("$PUPPETC" -a    -m "$TMP/mods" -f "$TMP/facts.yaml" "$TMP/site.pp" 2>&1 \
        | grep -oE 'hello n[0-9]' | sort -u | tr '\n' ' ')
par=$("$PUPPETC" -a -P -m "$TMP/mods" -f "$TMP/facts.yaml" "$TMP/site.pp" 2>&1 \
        | grep -oE 'hello n[0-9]' | sort -u | tr '\n' ' ')
[ "$par" = "hello n1 hello n2 hello n3 hello n4 " ]
check "user function resolves for all nodes under -P" $? "got: [$par]"
[ "$par" = "$ser" ]
check "parallel output matches serial (ser=[$ser] par=[$par])" $?

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
