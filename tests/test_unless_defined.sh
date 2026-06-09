#!/bin/bash
# Item 15 — the `unless defined(Resource[...]) { ... }` idempotence idiom.
#
# Two bugs combined to break it:
#   1. `unless` was never converted to a statement by the tree-sitter parser —
#      it fell through to the expression fallback and its body block (with the
#      resource declarations inside) was silently dropped, so the guard never
#      declared anything.
#   2. defined(File['x']) was case-sensitive; the catalog stores `file[...]`
#      (type as written in `file { }`) but a reference yields `File[...]`, so a
#      resource already declared elsewhere was not detected and the guard failed
#      to suppress a re-declaration.
#
# This test guards: the guard declares-then-resolves, suppresses when already
# declared, still reports genuinely-missing refs, handles `else`, and that
# defined() matches case-insensitively.

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

echo "=== Testing unless defined(Resource[...]) idempotence idiom ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# 1) Guard declares the marker; a later require resolves to it.
F1="$TMP/guard.pp"
cat > "$F1" <<'PP'
node default {
  unless defined(File['/tmp/marker']) {
    file { '/tmp/marker': ensure => present }
  }
  file { '/tmp/dependent': ensure => present, require => File['/tmp/marker'] }
}
PP
out=$("$PUPPETC" -s "$F1" 2>&1)
echo "$out" | grep -qE 'Total errors: +0'
check "guard declares marker; require resolves (0 errors)" $? "$out"

# 2) Both files land in the catalog exactly once (not zero, not duplicated).
"$PUPPETC" -c -n default "$F1" 2>/dev/null > "$TMP/cat.json"
python3 - "$TMP/cat.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
res = d.get('resources') or d.get('catalog', {}).get('resources') or []
files = sorted(r.get('title') for r in res if r.get('type', '').lower() == 'file')
sys.exit(0 if files == ['/tmp/dependent', '/tmp/marker'] else 1)
PY
check "both files in catalog exactly once" $?

# 3) When already declared elsewhere, the guard SUPPRESSES the re-declaration
#    (defined() finds the existing file -> no duplicate).
F3="$TMP/predeclared.pp"
cat > "$F3" <<'PP'
node default {
  file { '/tmp/marker': ensure => present }
  unless defined(File['/tmp/marker']) {
    file { '/tmp/marker': ensure => present }
  }
  file { '/tmp/dependent': ensure => present, require => File['/tmp/marker'] }
}
PP
out=$("$PUPPETC" -s "$F3" 2>&1)
echo "$out" | grep -qvE 'Duplicate declaration' && echo "$out" | grep -qE 'Total errors: +0'
check "pre-declared marker: guard suppresses, no duplicate" $? "$out"

# 4) A reference to a resource declared NOWHERE is still reported (no silencing).
F4="$TMP/missing.pp"
cat > "$F4" <<'PP'
node default {
  file { '/tmp/dependent': ensure => present, require => File['/tmp/never'] }
}
PP
out=$("$PUPPETC" -s "$F4" 2>&1)
echo "$out" | grep -qE "Could not find resource 'File\[/tmp/never\]'"
check "genuinely-missing reference still reported" $? "$out"

# 5) unless/else executes the correct branch.
F5="$TMP/else.pp"
cat > "$F5" <<'PP'
node default {
  unless true  { notice("then-WRONG") } else { notice("else-RIGHT") }
  unless false { notice("then-RIGHT") } else { notice("else-WRONG") }
}
PP
out=$("$PUPPETC" -a -f /dev/null "$F5" 2>&1 | grep -oE '(then|else)-(RIGHT|WRONG)' | sort | tr '\n' ' ')
[ "$out" = "else-RIGHT then-RIGHT " ]
check "unless/else runs the correct branch (got: $out)" $?

# 6) defined(File['x']) matches a `file { 'x': }` declaration (case-insensitive).
F6="$TMP/case.pp"
cat > "$F6" <<'PP'
node default {
  file { '/tmp/x': ensure => present }
  notice("found=${defined(File['/tmp/x'])}")
}
PP
out=$("$PUPPETC" -a -f /dev/null "$F6" 2>&1 | grep -oE 'found=[a-z]+')
[ "$out" = "found=true" ]
check "defined(File['x']) finds file { 'x': } (case-insensitive)" $?

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
