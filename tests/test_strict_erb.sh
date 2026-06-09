#!/bin/bash
# Item 2 — the Puppet-4 ERB sugar `<%= @class::var %>` (namespaced instance var)
# warns by default and errors under --strict-erb; plain `@var` and the
# `scope['class::var']` form stay quiet.

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

echo "=== Testing ERB @class::var strict mode ==="
echo

ERB="$TMP/sugar.erb"
cat > "$ERB" <<'TPL'
host_<%= @it::baseconfig::env %>.conf
fqdn_<%= scope['it::baseconfig::env'] %>.conf
port_<%= @port %>
TPL
MAN="$TMP/m.pp"
printf '$x = template("%s")\n' "$ERB" > "$MAN"

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# Default: warn exactly once on @it::baseconfig::env; nothing for scope[] / @port.
out=$($PUPPETC -e "$MAN" 2>&1)
n=$(echo "$out" | grep -c 'instance-variable sugar')
[ "$n" -eq 1 ];                                  check "exactly one sugar diagnostic (got $n)" $? "$out"
echo "$out" | grep -qE "WARNING.*@it::baseconfig::env"
check "default emits a WARNING for @it::baseconfig::env" $? "$out"
echo "$out" | grep -qv 'ERROR'
check "default does not error" $? "$out"

# --strict-erb: same occurrence becomes an error.
out=$($PUPPETC -e --strict-erb "$MAN" 2>&1)
echo "$out" | grep -qE "ERROR.*@it::baseconfig::env.*sugar"
check "--strict-erb promotes it to an ERROR" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
