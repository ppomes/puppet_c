#!/bin/bash
# Item 4 — a node that includes a module whose metadata.json declares a puppet
# version_requirement excluding Puppet 8 gets an error; modules with a
# compatible range or no metadata are accepted. Fixture lives in an isolated
# temp module dir (not tests/modules) to avoid perturbing other tests.

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

echo "=== Testing metadata.json puppet version_requirement vs Puppet 8 ==="
echo

for m in oldmod newmod nometa; do
    mkdir -p "$TMP/$m/manifests"
    printf 'class %s {}\n' "$m" > "$TMP/$m/manifests/init.pp"
done
# oldmod: excludes 8 (< 8.0.0). newmod: allows 8 (< 9.0.0). nometa: no metadata.
printf '{"requirements":[{"name":"puppet","version_requirement":">= 6.0.0 < 8.0.0"}]}\n' > "$TMP/oldmod/metadata.json"
printf '{"requirements":[{"name":"puppet","version_requirement":">= 6.0.0 < 9.0.0"}]}\n' > "$TMP/newmod/metadata.json"

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# 1) Node includes all three: exactly one error (oldmod), non-zero exit.
MAN="$TMP/all.pp"
printf 'node default {\n  include oldmod\n  include newmod\n  include nometa\n}\n' > "$MAN"
out=$($PUPPETC -s -m "$TMP" "$MAN" 2>&1); rc=$?
echo "$out" | grep -qE "Module 'oldmod' requires puppet .*< 8.0.0.* incompatible"
check "oldmod (< 8.0.0) errors with its requirement" $? "$out"
echo "$out" | grep -qvE "Module 'newmod'" && echo "$out" | grep -qvE "Module 'nometa'"
check "newmod (< 9.0.0) and nometa (no metadata) accepted" $? "$out"
echo "$out" | grep -qE 'Total errors: +1'; check "exactly one error" $? "$out"
[ "$rc" -ne 0 ];                            check "non-zero exit on failure ($rc)" $? "$out"

# 2) Node includes only the compatible/no-metadata modules: clean.
MAN2="$TMP/ok.pp"
printf 'node default {\n  include newmod\n  include nometa\n}\n' > "$MAN2"
out=$($PUPPETC -s -m "$TMP" "$MAN2" 2>&1); rc=$?
echo "$out" | grep -qE 'Total errors: +0' && [ "$rc" -eq 0 ]
check "compatible-only node: 0 errors, exit 0" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
