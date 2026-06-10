#!/bin/bash
# Item 32 — Puppet::Parser::Scope#exist? in the ERB Ruby renderer.
#
# exist?(name) is the sanctioned strict-safe probe under Puppet 8: it returns
# true/false without raising, so the PS14 production pattern
#   <% if (scope.exist?(X) ? scope[X] : nil) %>
# can guard a strict scope[X] lookup of a variable that may not be set in the
# target class. include? is an alias some older templates use. Without it the
# Ruby renderer died with "undefined method `exist?' for PuppetScope".

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

echo "=== Testing scope.exist? / scope.include? in ERB (item 32) ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

mkdir -p "$TMP/mods/settest/manifests" "$TMP/mods/settest/templates"
printf 'class settest { $known = "v42" }\n' > "$TMP/mods/settest/manifests/init.pp"

# The brief's fixture: existing var renders, missing var takes the else branch.
cat > "$TMP/mods/settest/templates/t.erb" <<'ERB'
<% if scope.exist?('settest::known') %>K=<%= scope['settest::known'] %><% end %>
<% if scope.exist?('settest::missing') %>NEVER<% else %>absent<% end %>
ERB

# The exact PS14 percona pattern: ternary probe guarding a strict scope[X]
# with a COMPUTED key — both the present and the absent variable.
cat > "$TMP/mods/settest/templates/p.erb" <<'ERB'
<% t_scope = 'settest' %>
<% if (scope.exist?(t_scope+'::known') ? scope[t_scope+'::known'] : nil) %>
cap = <%= scope[t_scope+'::known'] %>
<% end %>
<% if (scope.exist?(t_scope+'::missing') ? scope[t_scope+'::missing'] : nil) %>
cap2 = NEVER
<% end %>
ERB

# include? alias.
printf '<%% if scope.include?("settest::known") %%>inc-yes<%% end %%>\n' \
    > "$TMP/mods/settest/templates/i.erb"

cat > "$TMP/site.pp" <<'PP'
node default {
  include settest
  notice("T=${template('settest/t.erb')}")
  notice("P=${template('settest/p.erb')}")
  notice("I=${template('settest/i.erb')}")
}
PP
out=$("$PUPPETC" -e -m "$TMP/mods" "$TMP/site.pp" 2>&1)

# 1) Existing var: probe true, value rendered.
echo "$out" | grep -qE 'K=v42'
check "exist? true for a set class variable (K=v42)" $? "$out"

# 2) Missing var: probe false, else branch, NO exception.
echo "$out" | grep -qE 'absent' && echo "$out" | grep -qv 'NEVER'
check "exist? false for a missing variable (absent, no raise)" $? "$out"

# 3) PS14 ternary pattern with a computed key: guarded strict lookup works.
echo "$out" | grep -qE 'cap = v42' && echo "$out" | grep -qv 'cap2'
check "PS14 pattern: exist? guards strict scope[computed-key]" $? "$out"

# 4) include? alias behaves identically.
echo "$out" | grep -qE 'inc-yes'
check "include? alias works" $? "$out"

# 5) No 'undefined method' exceptions, 0 errors.
echo "$out" | grep -qv 'undefined method' && echo "$out" | grep -qvE '\[ERROR\]'
check "no undefined-method exceptions, no errors" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
