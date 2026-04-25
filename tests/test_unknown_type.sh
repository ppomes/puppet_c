#!/bin/bash
# Verify the unknown-resource-type detection layer:
#   - Built-ins (file, package, ...) accepted
#   - Custom Ruby type via Puppet::Type.newtype(:t) accepted
#   - Module define autoloaded via init.pp accepted
#   - Typo in unnamespaced type rejected
#   - Namespaced type pointing nowhere rejected

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PUPPETC="$PROJECT_DIR/compiler/.libs/puppetc-compile"
MODULES="$SCRIPT_DIR/modules"

export DYLD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${DYLD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$PROJECT_DIR/compiler/.libs:$PROJECT_DIR/common/.libs:$PROJECT_DIR/facter/.libs:${LD_LIBRARY_PATH:-}"

RED=$'\e[31m'; GREEN=$'\e[32m'; NC=$'\e[0m'
PASSED=0; FAILED=0
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "=== Testing unknown resource type detection ==="
echo

run_case() {
    local title="$1" manifest="$2" expect="$3"
    echo "Test: $title"
    out=$($PUPPETC -s -m "$MODULES" "$manifest" 2>&1)
    case "$expect" in
        ok)
            if echo "$out" | grep -q "Status: OK" && ! echo "$out" | grep -q "Unknown resource type"; then
                echo "  ${GREEN}✓${NC} accepted"
                ((PASSED++))
            else
                echo "  ${RED}✗${NC} unexpectedly rejected"
                echo "$out" | tail -8 | sed 's/^/      /'
                ((FAILED++))
            fi
            ;;
        reject:*)
            local needle="${expect#reject:}"
            if echo "$out" | grep -q "Unknown resource type: '$needle'"; then
                echo "  ${GREEN}✓${NC} rejected with correct type name"
                ((PASSED++))
            else
                echo "  ${RED}✗${NC} not rejected, or wrong message"
                echo "$out" | tail -8 | sed 's/^/      /'
                ((FAILED++))
            fi
            ;;
    esac
    echo
}

cat > "$TMP/builtin.pp" <<'EOF'
node default {
  file { '/tmp/x': ensure => present }
  package { 'curl': ensure => present }
  service { 'cron': ensure => running }
  notify { 'tag': message => 'hi' }
}
EOF
run_case "builtins (file/package/service/notify) accepted" "$TMP/builtin.pp" ok

cat > "$TMP/rbtype.pp" <<'EOF'
node default {
  loadertest_type { 'rb': value => 'configured' }
}
EOF
run_case "Ruby custom type from lib/puppet/type accepted" "$TMP/rbtype.pp" ok

cat > "$TMP/define_autoload.pp" <<'EOF'
node default {
  include loadertest::sub
}
EOF
run_case "autoloaded class accepted" "$TMP/define_autoload.pp" ok

cat > "$TMP/typo.pp" <<'EOF'
node default {
  fiel { '/tmp/x': ensure => present }
}
EOF
run_case "typo 'fiel' rejected" "$TMP/typo.pp" "reject:fiel"

cat > "$TMP/typo_ns.pp" <<'EOF'
node default {
  loadertest::nonexistent { 'x': }
}
EOF
run_case "namespaced type that can't autoload rejected" "$TMP/typo_ns.pp" "reject:loadertest::nonexistent"

cat > "$TMP/typo2.pp" <<'EOF'
node default {
  packge { 'curl': ensure => present }
}
EOF
run_case "typo 'packge' rejected" "$TMP/typo2.pp" "reject:packge"

if [ "$FAILED" -eq 0 ]; then
    echo "${GREEN}✓ All $PASSED unknown-type tests passed${NC}"
    exit 0
else
    echo "${RED}✗ $FAILED of $((PASSED+FAILED)) tests failed${NC}"
    exit 1
fi
