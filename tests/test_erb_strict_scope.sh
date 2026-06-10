#!/bin/bash
# Item 23 — ERB scope['x::y::z'] strict-variable access (Puppet 8 semantics).
#
# Under Puppet 8, scope['a::b'] on an undefined CLASS-QUALIFIED variable raises
# "Undefined variable" — even inside a defensive `if scope[X]`. The explicit
# scope.lookupvar(X) API keeps returning nil. Unqualified names stay forgiving
# (strict_variables defaults off for those).
#
# Implementation: the native renderer aborts on a strict qualified miss so the
# Ruby fallback re-renders and raises the real error; the embedded PuppetScope
# class raises from [] and returns nil from lookupvar.

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

echo "=== Testing ERB strict qualified scope[] lookups (item 23) ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

mkdir -p "$TMP/mods/m/templates" "$TMP/mods/m/manifests"
cat > "$TMP/mods/m/manifests/init.pp" <<'PP'
class m { $redo = '4G' }
PP

# 1) Undefined qualified var via scope[] -> compile error with the exact name.
printf "<%% scope['nonexistent::ns::var'] %%>\n" > "$TMP/mods/m/templates/f1.erb"
printf "node default { include m\n \$a = template('m/f1.erb') }\n" > "$TMP/site.pp"
out=$("$PUPPETC" -s -m "$TMP/mods" "$TMP/site.pp" 2>&1); rc=$?
echo "$out" | grep -q "Undefined variable 'nonexistent::ns::var'" && [ "$rc" -ne 0 ]
check "scope['nonexistent::ns::var'] raises; compile fails" $? "$out"

# 2) scope.lookupvar on the same missing name returns nil (renders 'absent').
printf "<%%= scope.lookupvar('nonexistent::ns::var').nil? ? 'absent' : 'present' %%>\n" \
    > "$TMP/mods/m/templates/f2.erb"
printf "node default { include m\n notice(\"r=\${template('m/f2.erb')}\") }\n" > "$TMP/site.pp"
out=$("$PUPPETC" -e -m "$TMP/mods" "$TMP/site.pp" 2>&1)
echo "$out" | grep -qE 'r=absent' && echo "$out" | grep -qv 'Undefined variable'
check "scope.lookupvar(...) keeps returning nil ('absent', no error)" $? "$out"

# 3) A DEFINED qualified var via scope[] still renders (native fast path).
printf "<%%= scope['m::redo'] %%>\n" > "$TMP/mods/m/templates/f3.erb"
printf "node default { include m\n notice(\"r=\${template('m/f3.erb')}\") }\n" > "$TMP/site.pp"
out=$("$PUPPETC" -e -m "$TMP/mods" "$TMP/site.pp" 2>&1)
echo "$out" | grep -qE 'r=4G'
check "defined scope['m::redo'] still renders (4G)" $? "$out"

# 4) The percona shape: computed key + defensive if, var missing -> raises.
cat > "$TMP/mods/m/templates/f4.erb" <<'ERB'
<% t_scope = 'm' %>
<% if scope[t_scope+'::missing_setting'] %>
setting = <%= scope[t_scope+'::missing_setting'] %>
<% end %>
ERB
printf "node default { include m\n \$a = template('m/f4.erb') }\n" > "$TMP/site.pp"
out=$("$PUPPETC" -s -m "$TMP/mods" "$TMP/site.pp" 2>&1); rc=$?
echo "$out" | grep -q "Undefined variable 'm::missing_setting'" && [ "$rc" -ne 0 ]
check "computed key (if scope[t_scope+'::x']) raises on missing var" $? "$out"

# 5) Unqualified missing names stay forgiving (no error, empty render).
printf "x<%%= scope['no_such_local'] %%>y\n" > "$TMP/mods/m/templates/f5.erb"
printf "node default { include m\n notice(\"r=\${template('m/f5.erb')}\") }\n" > "$TMP/site.pp"
out=$("$PUPPETC" -e -m "$TMP/mods" "$TMP/site.pp" 2>&1)
echo "$out" | grep -qE 'r=xy' && echo "$out" | grep -qv 'Undefined variable'
check "unqualified missing name stays forgiving (renders empty)" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
