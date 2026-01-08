#!/bin/bash
#
# Multipass VM management for puppetc development and testing
# Much lighter than VirtualBox/Vagrant!
#
# Usage:
#   ./puppetc-vm.sh create    - Create VM with build dependencies
#   ./puppetc-vm.sh shell     - SSH into VM
#   ./puppetc-vm.sh build     - Build packages in VM
#   ./puppetc-vm.sh valgrind  - Run valgrind memory check
#   ./puppetc-vm.sh test      - Run make check in VM
#   ./puppetc-vm.sh destroy   - Destroy VM
#   ./puppetc-vm.sh status    - Show VM status

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
VM_NAME="puppetc-dev"
CLOUD_INIT="$SCRIPT_DIR/cloud-init.yaml"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info() { echo -e "${GREEN}[INFO]${NC} $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

check_multipass() {
    if ! command -v multipass &> /dev/null; then
        error "Multipass not installed. Install from: https://multipass.run/"
    fi
}

vm_exists() {
    multipass list --format csv 2>/dev/null | grep -q "^${VM_NAME},"
}

vm_running() {
    multipass list --format csv 2>/dev/null | grep -q "^${VM_NAME},Running"
}

create_vm() {
    check_multipass

    if vm_exists; then
        warn "VM '$VM_NAME' already exists"
        if ! vm_running; then
            info "Starting VM..."
            multipass start "$VM_NAME"
        fi
        return 0
    fi

    info "Creating VM '$VM_NAME' with Ubuntu 24.04..."
    multipass launch 24.04 \
        --name "$VM_NAME" \
        --cpus 4 \
        --memory 4G \
        --disk 20G \
        --cloud-init "$CLOUD_INIT"

    info "Waiting for cloud-init to complete..."
    multipass exec "$VM_NAME" -- cloud-init status --wait

    info "Mounting project directory..."
    multipass mount "$PROJECT_DIR" "$VM_NAME":/home/ubuntu/puppetc

    info "VM ready!"
    echo ""
    echo "  Shell:    $0 shell"
    echo "  Build:    $0 build"
    echo "  Valgrind: $0 valgrind"
    echo "  Test:     $0 test"
}

shell_vm() {
    check_multipass
    if ! vm_exists; then
        error "VM '$VM_NAME' does not exist. Run: $0 create"
    fi
    if ! vm_running; then
        info "Starting VM..."
        multipass start "$VM_NAME"
    fi
    multipass shell "$VM_NAME"
}

build_vm() {
    check_multipass
    if ! vm_running; then
        error "VM '$VM_NAME' not running. Run: $0 create"
    fi

    info "Building puppetc in VM..."
    multipass exec "$VM_NAME" -- bash -c '
        cd /home/ubuntu/puppetc
        ./autogen.sh
        ./configure
        make -j$(nproc)
    '
    info "Build complete!"
}

build_packages() {
    check_multipass
    if ! vm_running; then
        error "VM '$VM_NAME' not running. Run: $0 create"
    fi

    info "Building Debian packages in VM..."
    multipass exec "$VM_NAME" -- bash -c '
        cd /home/ubuntu/puppetc
        ./autogen.sh
        dpkg-buildpackage -us -uc -b
        ls -la ../*.deb
    '
    info "Packages built! Copy them from VM with: multipass transfer $VM_NAME:/home/ubuntu/*.deb ."
}

run_valgrind() {
    check_multipass
    if ! vm_running; then
        error "VM '$VM_NAME' not running. Run: $0 create"
    fi

    local test_file="${1:-tests/puppet/simple.pp}"

    info "Running valgrind on $test_file..."
    multipass exec "$VM_NAME" -- bash -c "
        cd /home/ubuntu/puppetc

        # Ensure built
        if [ ! -f compiler/.libs/puppetc-compile ]; then
            echo 'Building first...'
            ./autogen.sh && ./configure && make -j\$(nproc)
        fi

        export LD_LIBRARY_PATH=./compiler/.libs:./common/.libs:./facter/.libs

        valgrind --leak-check=full \
                 --show-leak-kinds=definite,possible \
                 --track-origins=yes \
                 --error-exitcode=1 \
                 ./compiler/.libs/puppetc-compile -p '$test_file'
    "
}

run_valgrind_full() {
    check_multipass
    if ! vm_running; then
        error "VM '$VM_NAME' not running. Run: $0 create"
    fi

    info "Running comprehensive valgrind check..."
    multipass exec "$VM_NAME" -- bash -c '
        cd /home/ubuntu/puppetc

        # Ensure built
        if [ ! -f compiler/.libs/puppetc-compile ]; then
            echo "Building first..."
            ./autogen.sh && ./configure && make -j$(nproc)
        fi

        export LD_LIBRARY_PATH=./compiler/.libs:./common/.libs:./facter/.libs

        echo "=== Testing simple.pp ==="
        valgrind --leak-check=full --error-exitcode=1 \
            ./compiler/.libs/puppetc-compile -p tests/puppet/simple.pp

        echo ""
        echo "=== Testing with modules ==="
        valgrind --leak-check=full --error-exitcode=1 \
            ./compiler/.libs/puppetc-compile -p -m tests/modules tests/puppet/basic.pp

        echo ""
        echo "=== Testing ERB templates ==="
        valgrind --leak-check=full --error-exitcode=1 \
            ./compiler/.libs/puppetc-compile -p -m tests/modules tests/puppet/erb_basic.pp 2>/dev/null || true

        echo ""
        echo "Valgrind checks complete!"
    '
}

run_tests() {
    check_multipass
    if ! vm_running; then
        error "VM '$VM_NAME' not running. Run: $0 create"
    fi

    info "Running tests in VM..."
    multipass exec "$VM_NAME" -- bash -c '
        cd /home/ubuntu/puppetc

        # Ensure built
        if [ ! -f compiler/.libs/puppetc-compile ]; then
            echo "Building first..."
            ./autogen.sh && ./configure && make -j$(nproc)
        fi

        make check
    '
}

destroy_vm() {
    check_multipass
    if ! vm_exists; then
        warn "VM '$VM_NAME' does not exist"
        return 0
    fi

    info "Destroying VM '$VM_NAME'..."
    multipass delete "$VM_NAME"
    multipass purge
    info "VM destroyed"
}

status_vm() {
    check_multipass
    echo "=== Multipass VMs ==="
    multipass list
    echo ""
    if vm_exists; then
        echo "=== VM Info ==="
        multipass info "$VM_NAME"
    fi
}

stop_vm() {
    check_multipass
    if vm_running; then
        info "Stopping VM..."
        multipass stop "$VM_NAME"
        info "VM stopped"
    else
        warn "VM not running"
    fi
}

start_vm() {
    check_multipass
    if ! vm_exists; then
        error "VM '$VM_NAME' does not exist. Run: $0 create"
    fi
    if vm_running; then
        warn "VM already running"
    else
        info "Starting VM..."
        multipass start "$VM_NAME"
        info "VM started"
    fi
}

usage() {
    cat <<EOF
Multipass VM management for puppetc development

Usage: $0 <command> [args]

Commands:
  create          Create VM with build dependencies and valgrind
  shell           SSH into VM
  build           Build puppetc (./configure && make)
  packages        Build Debian packages
  valgrind [file] Run valgrind on a test file (default: tests/puppet/simple.pp)
  valgrind-full   Run comprehensive valgrind checks
  test            Run make check
  start           Start VM
  stop            Stop VM
  destroy         Destroy VM
  status          Show VM status

Examples:
  $0 create                              # Create development VM
  $0 shell                               # SSH into VM
  $0 valgrind                            # Quick memory check
  $0 valgrind tests/puppet/complex.pp   # Test specific file
  $0 valgrind-full                       # Comprehensive check

The project directory is automatically mounted at /home/ubuntu/puppetc
EOF
}

# Main
case "${1:-}" in
    create)     create_vm ;;
    shell)      shell_vm ;;
    build)      build_vm ;;
    packages)   build_packages ;;
    valgrind)   run_valgrind "${2:-}" ;;
    valgrind-full) run_valgrind_full ;;
    test)       run_tests ;;
    start)      start_vm ;;
    stop)       stop_vm ;;
    destroy)    destroy_vm ;;
    status)     status_vm ;;
    -h|--help|help) usage ;;
    *)
        usage
        exit 1
        ;;
esac
