#!/bin/bash
# Item 30 — per-tree resource policy (.puppetc-policy.json).
#
# An opt-in, per-branch policy file at the tree root lets each environment
# declare deprecated/disallowed resources, e.g. apt::source['openvox7'] on a
# branch that targets OpenVox 8. Checks run at resource-declaration time (the
# resolved title), match type case-insensitively and title exactly or by
# POSIX-ERE "title_pattern", dedupe to one diagnostic per type[title], and
# honour "level": "warning" (default) or "error". No file -> feature off.

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

echo "=== Testing per-tree resource policy (.puppetc-policy.json) ==="
echo

check() { if [ "$2" -eq 0 ]; then echo "  ${GREEN}✓${NC} $1"; ((PASSED++)); \
          else echo "  ${RED}✗${NC} $1"; [ -n "${3:-}" ] && echo "$3" | sed 's/^/      /'; ((FAILED++)); fi; }

# Tree layout: manifests/site.pp + modules/apt (a define) + the policy file.
mkdir -p "$TMP/manifests" "$TMP/modules/apt/manifests"
printf 'define apt::source($location = "", $repos = "", $ensure = present) {}\n' \
    > "$TMP/modules/apt/manifests/source.pp"
cat > "$TMP/manifests/site.pp" <<'PP'
node default {
  apt::source { 'openvox7': location => 'https://apt.voxpupuli.org', repos => 'openvox7' }
  apt::source { 'openvox5': location => 'https://apt.voxpupuli.org', repos => 'openvox5' }
  apt::source { 'openvox8': location => 'https://apt.voxpupuli.org', repos => 'openvox8' }
  apt::source { 'docker':   location => 'https://download.docker.com' }
  notify { 'legacy-banner': }
}
PP
cat > "$TMP/.puppetc-policy.json" <<'EOF'
{
  "deprecated_resources": [
    { "type": "apt::source", "title": "openvox7",
      "reason": "this branch targets OpenVox 8 - use the openvox8 repo" },
    { "type": "apt::source", "title_pattern": "^openvox[0-6]$",
      "reason": "ancient OpenVox repo", "level": "error" },
    { "type": "notify", "title": "legacy-banner",
      "reason": "banner notify retired" }
  ]
}
EOF

out=$(cd "$TMP" && "$PUPPETC" -s . 2>&1)

# 1) Exact-title entry warns, naming resource and reason.
echo "$out" | grep -qE "Policy .*apt::source\['openvox7'\] is deprecated: this branch targets OpenVox 8"
check "exact title: apt::source['openvox7'] warns with reason" $? "$out"

# 2) title_pattern entry matches openvox5 and escalates to an error.
echo "$out" | grep -qE "\[ERROR\].*Policy .*apt::source\['openvox5'\] is disallowed: ancient OpenVox repo"
check "title_pattern + level=error: openvox5 is an error" $? "$out"

# 3) Built-in types are policed too (notify path).
echo "$out" | grep -qE "Policy .*notify\['legacy-banner'\] is deprecated"
check "built-in resource (notify) policed too" $? "$out"

# 4) Non-matching titles are silent (openvox8, docker) — match the resource
#    reference, not the reason text (which mentions openvox8).
[ "$(echo "$out" | grep -cE "Policy .*\['(openvox8|docker)'\]")" -eq 0 ]
check "openvox8 / docker not flagged" $? "$out"

# 5) Exactly 1 error + 2 warnings from policy; compile fails on the error.
[ "$(echo "$out" | grep -cE 'Policy \(')" -eq 3 ] && echo "$out" | grep -qE 'Total errors: +1'
check "totals: 3 policy diagnostics, 1 error" $? "$out"

# 6) Without the policy file the feature is entirely off.
rm "$TMP/.puppetc-policy.json"
out=$(cd "$TMP" && "$PUPPETC" -s . 2>&1)
[ "$(echo "$out" | grep -cE 'Policy \(')" -eq 0 ] && echo "$out" | grep -qE 'Total errors: +0'
check "no policy file: no diagnostics, 0 errors" $? "$out"

echo
echo "Results: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
