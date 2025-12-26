#!/bin/bash
#
# Integration test runner for puppetc
# Runs tests in Docker containers and verifies results
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

TESTS_PASSED=0
TESTS_FAILED=0

# Helper functions
log_info() {
    echo -e "${YELLOW}[INFO]${NC} $1"
}

log_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    ((TESTS_PASSED++))
}

log_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    ((TESTS_FAILED++))
}

cleanup() {
    log_info "Cleaning up..."
    cd "$PROJECT_DIR"
    docker-compose -f docker-compose.test.yml down -v 2>/dev/null || true
}

trap cleanup EXIT

# Build images
log_info "Building Docker images..."
cd "$PROJECT_DIR"
docker-compose -f docker-compose.test.yml build --quiet

# Start server
log_info "Starting puppetc server..."
docker-compose -f docker-compose.test.yml up -d test-server
sleep 3

# Wait for server to be healthy
for i in {1..30}; do
    if docker-compose -f docker-compose.test.yml ps | grep -q "healthy"; then
        break
    fi
    sleep 1
done

# Check server is running
if ! docker-compose -f docker-compose.test.yml ps | grep -q "test-server.*healthy"; then
    log_fail "Server failed to start"
    docker-compose -f docker-compose.test.yml logs test-server
    exit 1
fi
log_pass "Server started successfully"

# Run agent and verification in a single container
log_info "Running puppetc agent and verification..."

docker-compose -f docker-compose.test.yml run --rm --entrypoint /bin/sh test-agent -c '
    set -e

    echo "=== Running Agent ==="
    puppetc-agent -a
    AGENT_EXIT=$?

    if [ $AGENT_EXIT -ne 0 ]; then
        echo "ERROR: Agent failed with exit code $AGENT_EXIT"
        exit 1
    fi

    echo ""
    echo "=== Running Verification Tests ==="

    PASS=0
    FAIL=0

    check_file() {
        if [ -f "$1" ]; then
            echo "PASS: File exists: $1"
            PASS=$((PASS + 1))
        else
            echo "FAIL: File missing: $1"
            FAIL=$((FAIL + 1))
        fi
    }

    check_file_content() {
        if [ -f "$1" ] && grep -q "$2" "$1"; then
            echo "PASS: File $1 contains: $2"
            PASS=$((PASS + 1))
        else
            echo "FAIL: File $1 missing or does not contain: $2"
            FAIL=$((FAIL + 1))
        fi
    }

    check_dir() {
        if [ -d "$1" ]; then
            echo "PASS: Directory exists: $1"
            PASS=$((PASS + 1))
        else
            echo "FAIL: Directory missing: $1"
            FAIL=$((FAIL + 1))
        fi
    }

    check_symlink() {
        if [ -L "$1" ]; then
            echo "PASS: Symlink exists: $1"
            PASS=$((PASS + 1))
        else
            echo "FAIL: Symlink missing: $1"
            FAIL=$((FAIL + 1))
        fi
    }

    check_host_entry() {
        if grep -q "$1" /etc/hosts; then
            echo "PASS: Host entry exists: $1"
            PASS=$((PASS + 1))
        else
            echo "FAIL: Host entry missing: $1"
            FAIL=$((FAIL + 1))
        fi
    }

    check_cron_entry() {
        if crontab -l 2>/dev/null | grep -q "$1"; then
            echo "PASS: Cron entry exists: $1"
            PASS=$((PASS + 1))
        else
            echo "FAIL: Cron entry missing: $1"
            FAIL=$((FAIL + 1))
        fi
    }

    check_cron_absent() {
        if ! crontab -l 2>/dev/null | grep -q "$1"; then
            echo "PASS: Cron entry correctly absent: $1"
            PASS=$((PASS + 1))
        else
            echo "FAIL: Cron entry should be absent: $1"
            FAIL=$((FAIL + 1))
        fi
    }

    check_group_exists() {
        if getent group "$1" >/dev/null 2>&1; then
            echo "PASS: Group exists: $1"
            PASS=$((PASS + 1))
        else
            echo "FAIL: Group missing: $1"
            FAIL=$((FAIL + 1))
        fi
    }

    check_group_gid() {
        local gid=$(getent group "$1" 2>/dev/null | cut -d: -f3)
        if [ "$gid" = "$2" ]; then
            echo "PASS: Group $1 has GID $2"
            PASS=$((PASS + 1))
        else
            echo "FAIL: Group $1 has GID $gid, expected $2"
            FAIL=$((FAIL + 1))
        fi
    }

    check_group_absent() {
        if ! getent group "$1" >/dev/null 2>&1; then
            echo "PASS: Group correctly absent: $1"
            PASS=$((PASS + 1))
        else
            echo "FAIL: Group should be absent: $1"
            FAIL=$((FAIL + 1))
        fi
    }

    check_user_exists() {
        if getent passwd "$1" >/dev/null 2>&1; then
            echo "PASS: User exists: $1"
            PASS=$((PASS + 1))
        else
            echo "FAIL: User missing: $1"
            FAIL=$((FAIL + 1))
        fi
    }

    check_user_uid() {
        local uid=$(getent passwd "$1" 2>/dev/null | cut -d: -f3)
        if [ "$uid" = "$2" ]; then
            echo "PASS: User $1 has UID $2"
            PASS=$((PASS + 1))
        else
            echo "FAIL: User $1 has UID $uid, expected $2"
            FAIL=$((FAIL + 1))
        fi
    }

    check_user_shell() {
        local shell=$(getent passwd "$1" 2>/dev/null | cut -d: -f7)
        if [ "$shell" = "$2" ]; then
            echo "PASS: User $1 has shell $2"
            PASS=$((PASS + 1))
        else
            echo "FAIL: User $1 has shell $shell, expected $2"
            FAIL=$((FAIL + 1))
        fi
    }

    check_user_absent() {
        if ! getent passwd "$1" >/dev/null 2>&1; then
            echo "PASS: User correctly absent: $1"
            PASS=$((PASS + 1))
        else
            echo "FAIL: User should be absent: $1"
            FAIL=$((FAIL + 1))
        fi
    }

    check_sysctl_value() {
        local current=$(cat /proc/sys/$(echo "$1" | tr . /) 2>/dev/null)
        if [ "$current" = "$2" ]; then
            echo "PASS: Sysctl $1 = $2"
            PASS=$((PASS + 1))
        else
            echo "FAIL: Sysctl $1 = $current, expected $2"
            FAIL=$((FAIL + 1))
        fi
    }

    check_sysctl_permanent() {
        if ls /etc/sysctl.d/*puppet*"$1"* >/dev/null 2>&1; then
            echo "PASS: Sysctl $1 has permanent config"
            PASS=$((PASS + 1))
        else
            echo "FAIL: Sysctl $1 missing permanent config"
            FAIL=$((FAIL + 1))
        fi
    }

    check_mount_exists() {
        if mount | grep -q "$1"; then
            echo "PASS: Mount exists: $1"
            PASS=$((PASS + 1))
        else
            echo "FAIL: Mount missing: $1"
            FAIL=$((FAIL + 1))
        fi
    }

    check_mount_absent() {
        if ! mount | grep -q "$1"; then
            echo "PASS: Mount correctly absent: $1"
            PASS=$((PASS + 1))
        else
            echo "FAIL: Mount should be absent: $1"
            FAIL=$((FAIL + 1))
        fi
    }

    check_fstab_entry() {
        if grep -q "$1" /etc/fstab; then
            echo "PASS: Fstab entry exists: $1"
            PASS=$((PASS + 1))
        else
            echo "FAIL: Fstab entry missing: $1"
            FAIL=$((FAIL + 1))
        fi
    }

    check_fstab_absent() {
        if ! grep -q "$1" /etc/fstab; then
            echo "PASS: Fstab entry correctly absent: $1"
            PASS=$((PASS + 1))
        else
            echo "FAIL: Fstab entry should be absent: $1"
            FAIL=$((FAIL + 1))
        fi
    }

    check_file_absent() {
        if [ ! -f "$1" ]; then
            echo "PASS: File correctly absent: $1"
            PASS=$((PASS + 1))
        else
            echo "FAIL: File should be absent: $1"
            FAIL=$((FAIL + 1))
        fi
    }

    echo ""
    echo "--- File Provider Tests ---"
    check_file "/tmp/test_file_content.txt"
    check_file_content "/tmp/test_file_content.txt" "Hello from puppetc"
    check_dir "/tmp/test_dir"
    check_file "/tmp/test_dir/nested.txt"
    check_symlink "/tmp/test_symlink"
    check_file "/tmp/test_file_source.txt"
    check_file_content "/tmp/test_file_source.txt" "puppet://"
    check_file_absent "/tmp/test_file_absent.txt"

    echo ""
    echo "--- Exec Provider Tests ---"
    check_file "/tmp/test_exec_marker.txt"
    check_file_content "/tmp/test_exec_marker.txt" "exec_test_passed"
    check_file_absent "/tmp/test_exec_unless_fail.txt"
    check_file "/tmp/test_exec_onlyif.txt"
    check_file_content "/tmp/test_exec_onlyif.txt" "onlyif_test_passed"
    check_file "/tmp/test_exec_cwd.txt"
    check_file_content "/tmp/test_exec_cwd.txt" "/tmp"
    check_file "/tmp/test_exec_date.txt"
    check_file "/tmp/test_exec_env.txt"
    check_file_content "/tmp/test_exec_env.txt" "environment_works"

    echo ""
    echo "--- Host Provider Tests ---"
    check_host_entry "testhost.example.com"
    check_host_entry "192.168.100.100"
    check_host_entry "another.example.com"

    echo ""
    echo "--- Cron Provider Tests ---"
    check_cron_entry "test_cron_simple"
    check_cron_entry "0 3"
    check_cron_entry "test_cron_full"
    check_cron_entry "30 2 15 6 1"
    check_cron_entry "test_cron_special"
    check_cron_entry "@daily"
    check_cron_absent "should_not_exist"

    echo ""
    echo "--- Group Provider Tests ---"
    check_group_exists "testgroup1"
    check_group_exists "testgroup2"
    check_group_gid "testgroup2" "5001"
    check_group_absent "testgroup_absent"

    echo ""
    echo "--- User Provider Tests ---"
    check_user_exists "testuser1"
    check_user_uid "testuser1" "5001"
    check_user_shell "testuser1" "/bin/bash"
    check_user_exists "testuser2"
    check_user_uid "testuser2" "5002"
    check_user_shell "testuser2" "/bin/sh"
    check_user_absent "testuser_absent"

    echo ""
    echo "--- Sysctl Provider Tests ---"
    check_sysctl_value "kernel.hostname" "puppetc-test-host"
    check_sysctl_value "vm.swappiness" "10"
    check_sysctl_permanent "vm-swappiness"
    check_sysctl_value "net.core.somaxconn" "1024"

    echo ""
    echo "--- Mount Provider Tests ---"
    check_mount_exists "/mnt/test_tmpfs"
    check_fstab_entry "/mnt/test_tmpfs"
    check_fstab_entry "/mnt/test_defined"
    check_mount_absent "/mnt/test_defined"
    check_mount_absent "/mnt/test_absent"
    check_fstab_absent "/mnt/test_absent"

    echo ""
    echo "================================"
    echo "Verification: $PASS passed, $FAIL failed"
    echo "================================"

    if [ $FAIL -gt 0 ]; then
        exit 1
    fi
    exit 0
'

RESULT=$?

if [ $RESULT -eq 0 ]; then
    log_pass "All tests passed"
else
    log_fail "Some tests failed"
fi

# Summary
echo ""
echo "================================"
echo "Integration Test Summary"
echo "================================"
echo -e "Framework tests: ${GREEN}$TESTS_PASSED passed${NC}, ${RED}$TESTS_FAILED failed${NC}"
echo "================================"

exit $RESULT
