#!/bin/bash
# Item 31 — scope['facts'] must resolve in the Ruby ERB fallback.
#
# The facts hash was bound as @facts / $facts but never into $puppet_vars —
# the hash PuppetScope actually reads. Simple <%= %> expressions render on the
# native engine (which resolves 'facts' through the interpreter) and worked,
# but any template with statements (if/assignment) falls back to Ruby, where
# scope['facts'] resolved to nothing: pre-item-23 that meant '' and a silently
# WRONG branch; post-item-23 it crashed with "undefined method `[]' for nil".
# The migrated adm branch (structured forms recommended by items 24-26) failed
# 30/30 nodes on exactly this. Now the canonical nested facts hash (same
# structure as .pp's $facts) is bound into $puppet_vars too.

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

echo "=== Testing scope['facts'] in the Ruby ERB fallback (item 31) ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

mkdir -p "$TMP/mods/m/templates"
cat > "$TMP/facts.yaml" <<'YAML'
facts:
  test.example.com:
    networking: { hostname: testhost, fqdn: test.example.com }
YAML

# The four variants from the brief: A (expression), B (assignment),
# C (if condition — the one that failed), D (trim-mode assignment).
printf '<%%= scope["facts"]["networking"]["hostname"] %%>\n' > "$TMP/mods/m/templates/a.erb"
printf '<%% x = scope["facts"] %%><%%= x ? "ok" : "NIL" %%>\n' > "$TMP/mods/m/templates/b.erb"
printf '<%% if scope["facts"]["networking"] %%>yes<%% else %%>no<%% end %%>\n' > "$TMP/mods/m/templates/c.erb"
printf '<%%- x = scope["facts"] -%%><%%= x ? "ok" : "NIL" %%>\n' > "$TMP/mods/m/templates/d.erb"
# The migrated ntp.conf.erb shape: structured fact inside an if condition.
cat > "$TMP/mods/m/templates/ntp.erb" <<'ERB'
<%-
servers = ['test.example.com', 'other.example.com']
if servers.include?(scope['facts']['networking']['fqdn']) then
-%>
IS_SERVER
<%- else -%>
IS_CLIENT
<%- end -%>
ERB

cat > "$TMP/site.pp" <<'PP'
node 'test.example.com' {
  notice("A=${template('m/a.erb')}")
  notice("B=${template('m/b.erb')}")
  notice("C=${template('m/c.erb')}")
  notice("D=${template('m/d.erb')}")
  notice("N=${template('m/ntp.erb')}")
}
PP
out=$("$PUPPETC" -e -n test.example.com -f "$TMP/facts.yaml" -m "$TMP/mods" "$TMP/site.pp" 2>&1)

# 1) Variant A (native expression path) still renders the fact.
echo "$out" | grep -qE 'A=testhost'
check "A: <%= scope['facts'][...][...] %> renders (native path)" $? "$out"

# 2) Variant B: scope['facts'] truthy in an assignment (Ruby path).
echo "$out" | grep -qE 'B=ok'
check "B: assignment sees scope['facts'] (Ruby path)" $? "$out"

# 3) Variant C — the regression: chained [] in an if condition.
echo "$out" | grep -qE 'C=yes' && echo "$out" | grep -qv "undefined method"
check "C: if scope['facts']['networking'] takes the yes branch" $? "$out"

# 4) Variant D: trim-mode assignment.
echo "$out" | grep -qE 'D=ok'
check "D: trim-mode assignment sees scope['facts']" $? "$out"

# 5) The migrated ntp shape: include? on the structured fqdn matches.
echo "$out" | grep -qE 'IS_SERVER'
check "ntp shape: servers.include?(scope['facts']..['fqdn']) matches" $? "$out"

# 6) No '[] for nil' Ruby exceptions anywhere and 0 errors.
echo "$out" | grep -qv "for nil" && echo "$out" | grep -qvE '\[ERROR\]'
check "no nil-[] crashes, no errors" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
