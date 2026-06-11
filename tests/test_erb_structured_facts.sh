#!/bin/bash
# Item 14 — structured-fact type drift in ERB templates.
#
# Facter 4 reshaped several facts from flat strings/ints into Hashes/Arrays.
# Templates that still call String methods (split/gsub/tr/to_a) on them crash
# at render time ("undefined method 'split' for #<Hash>"). A static ERB scan
# flags `scope['<fact>'].<string_method>` for the known-changed facts and
# suggests the structured replacement; under --strict-erb it errors.
#
# Asserts on the specific diagnostic messages (not totals) so the test is
# immune to unrelated render errors when no facts are loaded.

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

echo "=== Testing ERB structured-fact (Facter 4 type drift) scan ==="
echo

mkdir -p "$TMP/mods/nag/templates"
cat > "$TMP/mods/nag/templates/t.erb" <<'ERB'
# Item 14: structured-fact .split etc.
<% scope['mountpoints'].split(',').each do |m| -%>
mount = <%= m %>
<% end -%>
# vs OK:
<% scope['mountpoints'].keys.each do |m| -%>
mount = <%= m %>
<% end -%>
<% scope['processors'].split(',') -%>
<% scope['memorysize'].gsub(/x/, '') -%>
<% scope['facts']['interfaces'].split(',') -%>
<% scope['blockdevices'].to_a -%>
<% scope['rackrow'].split('.') -%>
<% scope['facts']['networking']['mtu'].to_s -%>
ERB
cat > "$TMP/site.pp" <<'PP'
node default { $x = template('nag/t.erb') }
PP

out=$("$PUPPETC" -s -m "$TMP/mods" "$TMP/site.pp" 2>&1)
strict=$("$PUPPETC" -s --strict-erb -m "$TMP/mods" "$TMP/site.pp" 2>&1)

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; echo "$out" | grep -i 'ERB scope' | sed 's/^/      /'; ((FAILED++)); fi; }

# 1) .split on mountpoints (Hash) warns, with .keys suggestion, at line 2.
echo "$out" | grep -qE "t\.erb:2: .*ERB scope\['mountpoints'\]\.split.* is a Hash .* use \.keys"
check "mountpoints.split flagged (use .keys)" $?

# 2) The .keys form (line 6) is NOT flagged.
echo "$out" | grep -qvE "ERB scope\['mountpoints'\]\.keys"
check "mountpoints.keys is silent" $?

# 3) processors -> ['count'] suggestion.
echo "$out" | grep -qE "ERB scope\['processors'\]\.split.* is a Hash .* use \['count'\]"
check "processors.split flagged (use ['count'])" $?

# 4) memorysize -> memory path suggestion.
echo "$out" | grep -qE "ERB scope\['memorysize'\]\.gsub.*is a number.*memory.*system.*total"
check "memorysize.gsub flagged (use memory hash)" $?

# 5) Nested scope['facts']['interfaces'].split resolves to the 'interfaces' fact.
echo "$out" | grep -qE "ERB scope\['interfaces'\]\.split.* is a Hash"
check "nested scope['facts']['interfaces'].split flagged" $?

# 6) blockdevices.to_a flagged.
echo "$out" | grep -qE "ERB scope\['blockdevices'\]\.to_a.* is a Hash"
check "blockdevices.to_a flagged" $?

# 7) A non-changed, non-legacy fact (.split on rackrow) is NOT flagged.
echo "$out" | grep -qvE "scope\['rackrow'\]"
check "rackrow.split not flagged (unchanged fact)" $?

# 8) A nested non-changed key (mtu.to_s) is NOT flagged.
echo "$out" | grep -qvE "scope\['mtu'\]|'mtu' is"
check "facts['networking']['mtu'].to_s not flagged" $?

# 9) Exactly 5 structured-fact warnings (mountpoints, processors, memorysize,
#    interfaces, blockdevices) in non-strict mode.
n=$(echo "$out" | grep -c 'is a \(Hash\|number\) in Facter 4')
[ "$n" -eq 5 ]
check "exactly 5 structured-fact warnings (got $n)" $?

# 10) Under --strict-erb the same checks are errors, not warnings.
ns=$(echo "$strict" | grep -c '\[ERROR\].*is a \(Hash\|number\) in Facter 4')
[ "$ns" -eq 5 ]
check "--strict-erb escalates all 5 to errors (got $ns)" $?

# --- Item 29 extensions: ::-prefixed form, more methods, more Hash facts ---
cat > "$TMP/mods/nag/templates/u.erb" <<'ERB'
<% scope['::mountpoints'].split(',').each { |m| } %>
<% scope['networking'].length %>
<% scope['mountpoints'].scan(/x/) %>
<% scope['filesystems'].split(',') %>
ERB
cat > "$TMP/site2.pp" <<'PP'
node default { $x = template('nag/u.erb') }
PP
out=$("$PUPPETC" -s -m "$TMP/mods" "$TMP/site2.pp" 2>&1)

# 11) scope['::mountpoints'].split — the ::-prefixed form is recognised.
echo "$out" | grep -qE "u\.erb:1: .*'mountpoints' is a Hash"
check "item 29: scope['::mountpoints'].split flagged (:: form)" $?

# 12) networking.length — new fact + new method.
echo "$out" | grep -qE "u\.erb:2: .*'networking' is a Hash"
check "item 29: scope['networking'].length flagged" $?

# 13) mountpoints.scan — new method.
echo "$out" | grep -qE "u\.erb:3: .*'mountpoints' is a Hash"
check "item 29: scope['mountpoints'].scan flagged" $?

# 14) filesystems.split is NOT flagged — still a String in Facter 4/5.
echo "$out" | grep -qvE "'filesystems' is"
check "item 29: filesystems.split not flagged (still a String)" $?

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
