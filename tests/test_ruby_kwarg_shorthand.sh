#!/bin/bash
# Item 3 — scanned provider/type .rb files using the Ruby keyword-argument
# shorthand `def foo(arg:, …)` (no default) warn; defaulted keywords and
# symbols do not. Fixture lives in an isolated temp module so it doesn't
# perturb other tests that scan tests/modules.

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

echo "=== Testing Ruby def keyword-arg shorthand scan ==="
echo

mkdir -p "$TMP/p8/lib/puppet/type"
cat > "$TMP/p8/lib/puppet/type/bad_shorthand.rb" <<'RB'
Puppet::Type.newtype(:badshort) do
  newparam(:name)
  def go(name:, expired:, weak_ssl: false)
    [name, expired, weak_ssl]
  end
end
RB
cat > "$TMP/p8/lib/puppet/type/good.rb" <<'RB'
Puppet::Type.newtype(:goodtype) do
  newparam(:value)
  # defaulted keyword + symbols must NOT warn
  def go(bar: 'default')
    [:name, :value, bar]
  end
end
RB
# Declaring the custom type forces the Ruby-type scan (built-ins don't).
MAN="$TMP/site.pp"
printf 'badshort { "x": }\n' > "$MAN"

out=$($PUPPETC -e -m "$TMP" "$MAN" 2>&1)

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; echo "$out" | sed 's/^/      /'; ((FAILED++)); fi; }

n=$(echo "$out" | grep -c 'keyword-argument shorthand')
[ "$n" -eq 1 ];                                       check "exactly one shorthand warning (got $n)" $?
echo "$out" | grep -qE "bad_shorthand\.rb:3:.*shorthand 'name:'"
check "warning on bad_shorthand.rb def line, names 'name:'" $?
echo "$out" | grep -qv 'good.rb'
check "good.rb (defaulted keyword + symbols) does not warn" $?

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
