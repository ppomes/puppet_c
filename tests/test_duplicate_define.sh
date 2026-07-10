#!/bin/bash
# Item 39 — duplicate declaration of a DEFINED-TYPE instance must fail cleanly
# (exit 1), not SIGABRT with a double free.
#
# The duplicate branch of the define-instance path destroyed title_val and then
# `continue`d the title-expansion loop, whose per-instance cleanup destroyed it
# again — double free (exit 134). For multi-title arrays the mid-loop destroy
# would also have poisoned the remaining iterations, which read elements
# pointing into title_val. Native resources were unaffected.

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

echo "=== Testing duplicate defined-type declarations (no double free) ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# 1) Duplicate define instance: clean exit 1 with the diagnostic, no SIGABRT.
F1="$TMP/dup.pp"
cat > "$F1" <<'PP'
define dup_inner($p) { }
node default {
  dup_inner { 'x.com': p => 1 }
  dup_inner { 'x.com': p => 1 }
}
PP
out=$("$PUPPETC" -a -s "$F1" 2>&1); rc=$?
[ "$rc" -eq 1 ] && echo "$out" | grep -qE 'Duplicate declaration - dup_inner\[x.com\]'
check "define dup: diagnostic + exit 1 (got rc=$rc, 134 would be SIGABRT)" $? "$out"

# 2) Parity: native resource duplicate behaves the same.
F2="$TMP/nat.pp"
cat > "$F2" <<'PP'
node default {
  exec { 'x.com': command => '/bin/true' }
  exec { 'x.com': command => '/bin/true' }
}
PP
out=$("$PUPPETC" -a -s "$F2" 2>&1); rc=$?
[ "$rc" -eq 1 ] && echo "$out" | grep -qE 'Duplicate declaration'
check "native dup parity: diagnostic + exit 1 (got rc=$rc)" $? "$out"

# 3) Multi-title array where ONE element duplicates: the dup errors, the
#    remaining elements are still instantiated (title_val stays alive).
F3="$TMP/arr.pp"
cat > "$F3" <<'PP'
define dup_inner($p) { notice("inst=${title}") }
node default {
  dup_inner { 'a.com': p => 1 }
  dup_inner { ['a.com', 'b.com', 'c.com']: p => 1 }
}
PP
out=$("$PUPPETC" -a "$F3" 2>&1); rc=$?
echo "$out" | grep -qE 'Duplicate declaration - dup_inner\[a.com\]' && \
echo "$out" | grep -qE 'inst=b.com' && echo "$out" | grep -qE 'inst=c.com' && \
[ "$rc" -eq 1 ]
check "array title: dup element errors, b.com/c.com still instantiated" $? "$out"

# 4) Same under -P (a worker abort would previously kill the run).
printf 'facts:\n  n1.example.com: { f: 1 }\n  n2.example.com: { f: 2 }\n' > "$TMP/facts.yaml"
out=$("$PUPPETC" -a -P -s -f "$TMP/facts.yaml" "$F1" 2>&1); rc=$?
[ "$rc" -eq 1 ] && echo "$out" | grep -qE 'Duplicate declaration'
check "-P parallel: clean failure, no abort (got rc=$rc)" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
