#!/bin/bash
# Items 24/25/26 — legacy-fact lookups in ERB that break under Puppet 8/Facter 5
# (found during the live OpenVox 8 trial):
#   24: scope['::hostname']            raises "Undefined variable" (strict)
#   25: scope.lookupvar('::fqdn')      ALSO raises (lookupvar is strict too)
#   26: @lsbdistrelease                is nil -> wrong template branch, silently
# All three are flagged with the structured scope['facts'][...] replacement.
# Class variables (scope['::a::b'], @class::var) and still-valid names
# (environment, facts, trusted) must not be flagged.

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

echo "=== Testing ERB legacy-fact scope/lookupvar/@ivar scans (items 24-26) ==="
echo

mkdir -p "$TMP/mods/m/templates"
cat > "$TMP/mods/m/templates/t.erb" <<'ERB'
<% x = scope['::hostname'] %>
<% y = scope.lookupvar('::fqdn') %>
<% if @lsbdistrelease.to_s >= '20.04' %>
PY3
<% end %>
<% ok1 = scope['facts']['networking']['hostname'] %>
<% ok2 = scope['::myclass::var'] %>
<% ok3 = @accepteddomains %>
<% ok4 = scope['::environment'] %>
admin@hostname in body text is not a tag
ERB
cat > "$TMP/site.pp" <<'PP'
node default { $x = template('m/t.erb') }
PP

out=$("$PUPPETC" -s -m "$TMP/mods" "$TMP/site.pp" 2>&1)
strict=$("$PUPPETC" -s --strict-erb -m "$TMP/mods" "$TMP/site.pp" 2>&1)

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; echo "$out" | grep -E 'ERB ' | sed 's/^/      /'; ((FAILED++)); fi; }

# 1) Item 24: scope['::hostname'] flagged with the scope['facts'] suggestion.
echo "$out" | grep -qE "ERB scope\['::hostname'\] raises .*use scope\['facts'\]\['networking'\]\['hostname'\]"
check "scope['::hostname'] flagged with structured suggestion" $?

# 2) Item 25: scope.lookupvar('::fqdn') flagged, noting lookupvar is also strict.
echo "$out" | grep -qE "ERB scope\.lookupvar\('::fqdn'\): lookupvar is also strict.*scope\['facts'\]\['networking'\]\['fqdn'\]"
check "scope.lookupvar('::fqdn') flagged (lookupvar also strict)" $?

# 3) Item 26: @lsbdistrelease flagged as nil-under-Facter-5.
echo "$out" | grep -qE "ERB '@lsbdistrelease': legacy fact instance variable is nil.*scope\['facts'\]\['os'\]\['distro'\]\['release'\]\['full'\]"
check "@lsbdistrelease flagged (nil under Facter 5)" $?

# 4) Exactly 3 legacy-scope warnings — the negatives are silent.
n=$(echo "$out" | grep -cE "raises \"Undefined variable\"|lookupvar is also strict|legacy fact instance variable")
[ "$n" -eq 3 ]
check "negatives silent: class vars / facts / @class_var / environment (got $n of 3)" $?

# 5) --strict-erb escalates all three to errors.
ns=$(echo "$strict" | grep -cE '\[ERROR\].*(raises "Undefined variable"|lookupvar is also strict|legacy fact instance variable)')
[ "$ns" -eq 3 ]
check "--strict-erb escalates all 3 to errors (got $ns)" $?

# --- Item 35: unqualified legacy reads (no ::) are flagged AND fail at eval --
mkdir -p "$TMP/mods/m35/templates" "$TMP/manifests"
cat > "$TMP/mods/m35/templates/u.erb" <<'ERB'
ip=<%= scope['ipaddress'] %>
host=<%= scope['hostname'] %>
ERB
printf 'node default { $x = template(%s) }\n' "'m35/u.erb'" > "$TMP/site35.pp"
printf 'facts:\n  n.example.com:\n    hostname: n\n    ipaddress: 1.2.3.4\n' > "$TMP/facts35.yaml"
out=$("$PUPPETC" -e -s -n n.example.com -f "$TMP/facts35.yaml" -m "$TMP/mods" "$TMP/site35.pp" 2>&1)

# 6) Both unqualified reads flagged by the lint.
n=$(echo "$out" | grep -cE "ERB scope\['(ipaddress|hostname)'\] raises")
[ "$n" -eq 2 ]
check "item 35: scope['ipaddress']/scope['hostname'] (no ::) flagged (got $n)" $? "$out"

# 7) Evaluation raises like real Puppet 8 even though the facts hold values.
echo "$out" | grep -qE "Undefined variable '(ipaddress|hostname)'"
check "item 35: unqualified legacy read raises at evaluation" $? "$out"

# 8) A genuine manifest variable of the same name still resolves.
cat > "$TMP/mods/m35/templates/v.erb" <<'ERB'
host=<%= scope['hostname'] %>
ERB
printf 'node default { $hostname = "fromvar"\n notice("G=${template(%s)}") }\n' "'m35/v.erb'" > "$TMP/site35b.pp"
out=$("$PUPPETC" -e -n n.example.com -f "$TMP/facts35.yaml" -m "$TMP/mods" "$TMP/site35b.pp" 2>&1)
echo "$out" | grep -qE 'G=host=fromvar'
check "item 35: genuine \$hostname manifest variable still resolves" $? "$out"

# 9) lookupvar of a legacy name -> nil (defensive API); survivors don't raise.
cat > "$TMP/mods/m35/templates/w.erb" <<'ERB'
lv=<%= scope.lookupvar('hostname').nil? ? 'absent' : 'present' %> pv_ok=<%= scope['puppetversion'].nil? ? 'yes' : 'yes' %>
ERB
printf 'node default { notice("W=${template(%s)}") }\n' "'m35/w.erb'" > "$TMP/site35c.pp"
out=$("$PUPPETC" -e -n n.example.com -f "$TMP/facts35.yaml" -m "$TMP/mods" "$TMP/site35c.pp" 2>&1)
echo "$out" | grep -qE 'W=lv=absent pv_ok=yes' && echo "$out" | grep -qv "Undefined variable 'puppetversion'"
check "item 35: lookupvar(legacy)->nil; survivor puppetversion never raises" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
