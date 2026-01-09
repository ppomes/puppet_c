#!/bin/bash
#
# Multipass VM management for puppetc server/agent testing
# Lighter alternative to Vagrant/VirtualBox
#
# Usage:
#   ./puppetc-vm.sh up           - Create and start server + agent VMs
#   ./puppetc-vm.sh down         - Stop VMs
#   ./puppetc-vm.sh destroy      - Destroy VMs
#   ./puppetc-vm.sh ssh server   - SSH into server
#   ./puppetc-vm.sh ssh agent    - SSH into agent
#   ./puppetc-vm.sh status       - Show VM status

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CLOUD_INIT="$SCRIPT_DIR/cloud-init.yaml"

VM_SERVER="puppetc-server"
VM_AGENT="puppetc-agent"

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
        error "Multipass not installed. Install from: https://multipass.run/
  macOS:  brew install multipass
  Ubuntu: sudo snap install multipass"
    fi
}

vm_exists() {
    multipass list --format csv 2>/dev/null | grep -q "^${1},"
}

vm_running() {
    multipass list --format csv 2>/dev/null | grep -q "^${1},Running"
}

get_vm_ip() {
    multipass info "$1" --format csv 2>/dev/null | tail -1 | cut -d',' -f3
}

create_vm() {
    local name="$1"
    local memory="${2:-2G}"

    if vm_exists "$name"; then
        if ! vm_running "$name"; then
            info "Starting existing VM '$name'..."
            multipass start "$name"
        else
            info "VM '$name' already running"
        fi
        return 0
    fi

    info "Creating VM '$name'..."
    multipass launch 24.04 \
        --name "$name" \
        --cpus 2 \
        --memory "$memory" \
        --disk 10G \
        --cloud-init "$CLOUD_INIT"

    info "Waiting for cloud-init..."
    multipass exec "$name" -- cloud-init status --wait >/dev/null 2>&1 || true
}

build_and_install() {
    local vm_name="$1"

    info "Building and installing packages on $vm_name..."

    # Mount source code (read-only for source, we build in /tmp)
    multipass mount "$PROJECT_DIR" "$vm_name":/home/ubuntu/puppetc-src 2>/dev/null || true

    multipass exec "$vm_name" -- bash -c '
        set -e

        # Check if already installed
        if command -v puppetc-compile >/dev/null 2>&1; then
            echo "puppetc already installed: $(which puppetc-compile)"
            exit 0
        fi

        echo "=== Building puppetc packages ==="

        # Copy source to local directory (avoid cross-platform binary issues)
        echo "Copying source to /tmp/puppetc-build..."
        rm -rf /tmp/puppetc-build
        mkdir -p /tmp/puppetc-build
        rsync -a --exclude=".git" --exclude="*.o" --exclude="*.lo" --exclude="*.la" \
              --exclude=".libs" --exclude="autom4te.cache" \
              /home/ubuntu/puppetc-src/ /tmp/puppetc-build/
        cd /tmp/puppetc-build

        # Extra clean
        rm -rf */.*libs */.libs debian/tmp debian/*.debhelper* 2>/dev/null || true
        rm -f config.status config.log Makefile 2>/dev/null || true

        # Build packages
        echo "Running autogen.sh..."
        ./autogen.sh

        echo "Building Debian packages (this may take a few minutes)..."
        dpkg-buildpackage -us -uc -b -j$(nproc)

        # Install packages
        echo "Installing packages..."
        ls -la /tmp/*.deb
        sudo dpkg -i /tmp/*.deb || sudo apt-get -f install -y

        echo "=== Installation complete ==="
        which puppetc-compile
        puppetc-compile --version 2>/dev/null || true
    '
}

setup_server() {
    info "Setting up server..."

    build_and_install "$VM_SERVER"

    # Setup puppet directories with links to mounted manifests
    multipass mount "$PROJECT_DIR" "$VM_SERVER":/home/ubuntu/puppetc 2>/dev/null || true

    multipass exec "$VM_SERVER" -- bash -c '
        # Create puppet directories
        sudo mkdir -p /etc/puppet/manifests /etc/puppet/modules /etc/puppet/hiera /var/lib/puppetc

        # Link to mounted code for live editing
        sudo rm -rf /etc/puppet/manifests /etc/puppet/modules /etc/puppet/hiera
        sudo ln -sf /home/ubuntu/puppetc/puppetcode/manifests /etc/puppet/manifests
        sudo ln -sf /home/ubuntu/puppetc/puppetcode/modules /etc/puppet/modules
        sudo ln -sf /home/ubuntu/puppetc/puppetcode/hiera /etc/puppet/hiera
    '

    local server_ip=$(get_vm_ip "$VM_SERVER")
    info "Server IP: $server_ip"
}

setup_agent() {
    local server_ip=$(get_vm_ip "$VM_SERVER")

    info "Setting up agent (server: $server_ip)..."

    build_and_install "$VM_AGENT"

    info "Agent ready. Server IP: $server_ip"
}

start_server_daemon() {
    local server_ip=$(get_vm_ip "$VM_SERVER")

    info "Starting puppetc-server on $server_ip:8140..."

    # Kill any existing server
    multipass exec "$VM_SERVER" -- pkill -f puppetc-server 2>/dev/null || true

    # Start server in background (binaries installed via deb)
    multipass exec "$VM_SERVER" -- bash -c '
        nohup puppetc-server -v -p 8140 \
            -m /etc/puppet/modules \
            -D /etc/puppet/hiera \
            -P /var/lib/puppetc/puppetdb.sqlite \
            /etc/puppet > /tmp/puppetc-server.log 2>&1 &
        sleep 2
        if pgrep -f puppetc-server > /dev/null; then
            echo "Server started (PID: $(pgrep -f puppetc-server))"
        else
            echo "Failed to start server. Check /tmp/puppetc-server.log"
            cat /tmp/puppetc-server.log
            exit 1
        fi
    '
}

up() {
    check_multipass

    info "Creating VMs..."
    create_vm "$VM_SERVER" "2G"
    create_vm "$VM_AGENT" "2G"

    setup_server
    setup_agent
    start_server_daemon

    local server_ip=$(get_vm_ip "$VM_SERVER")
    local agent_ip=$(get_vm_ip "$VM_AGENT")

    echo ""
    echo "=========================================="
    echo -e "${GREEN}VMs ready!${NC}"
    echo ""
    echo "Server: $server_ip:8140"
    echo "Agent:  $agent_ip"
    echo ""
    echo "Test commands:"
    echo "  $0 agent -n    # Run agent in noop mode"
    echo "  $0 agent -a    # Run agent in apply mode"
    echo "  $0 logs        # Watch server logs"
    echo "  $0 ssh agent   # SSH into agent VM"
    echo ""
    echo "Edit puppetcode/manifests/site.pp and test!"
    echo "=========================================="
}

down() {
    check_multipass
    info "Stopping VMs..."
    multipass stop "$VM_SERVER" 2>/dev/null || true
    multipass stop "$VM_AGENT" 2>/dev/null || true
    info "VMs stopped"
}

destroy() {
    check_multipass
    info "Destroying VMs..."
    multipass delete "$VM_SERVER" 2>/dev/null || true
    multipass delete "$VM_AGENT" 2>/dev/null || true
    multipass purge
    info "VMs destroyed"
}

ssh_vm() {
    check_multipass
    local target="$1"
    local vm_name=""

    case "$target" in
        server) vm_name="$VM_SERVER" ;;
        agent)  vm_name="$VM_AGENT" ;;
        *)      error "Usage: $0 ssh [server|agent]" ;;
    esac

    if ! vm_running "$vm_name"; then
        error "VM '$vm_name' not running. Run: $0 up"
    fi

    multipass shell "$vm_name"
}

run_agent() {
    check_multipass
    local server_ip=$(get_vm_ip "$VM_SERVER")
    local mode="${1:--n}"  # default to noop

    if ! vm_running "$VM_AGENT"; then
        error "Agent VM not running. Run: $0 up"
    fi

    # Use installed binary
    multipass exec "$VM_AGENT" -- puppetc-agent -s "http://$server_ip:8140" $mode -v
}

status() {
    check_multipass
    echo "=== Multipass VMs ==="
    multipass list
    echo ""

    if vm_running "$VM_SERVER"; then
        local server_ip=$(get_vm_ip "$VM_SERVER")
        echo "Server: $server_ip:8140"
        echo "  Logs: multipass exec $VM_SERVER -- tail -f /tmp/puppetc-server.log"
    fi

    if vm_running "$VM_AGENT"; then
        local agent_ip=$(get_vm_ip "$VM_AGENT")
        echo "Agent:  $agent_ip"
    fi
}

logs() {
    check_multipass
    if ! vm_running "$VM_SERVER"; then
        error "Server VM not running"
    fi
    multipass exec "$VM_SERVER" -- tail -f /tmp/puppetc-server.log
}

rebuild() {
    check_multipass

    info "Rebuilding packages..."

    # Force reinstall by removing packages first
    for vm in "$VM_SERVER" "$VM_AGENT"; do
        if vm_running "$vm"; then
            info "Rebuilding on $vm..."
            multipass exec "$vm" -- bash -c '
                sudo dpkg -r puppetc puppetc-server puppetc-agent 2>/dev/null || true
            '
        fi
    done

    # Rebuild and reinstall
    if vm_running "$VM_SERVER"; then
        build_and_install "$VM_SERVER"
    fi
    if vm_running "$VM_AGENT"; then
        build_and_install "$VM_AGENT"
    fi

    # Restart server
    if vm_running "$VM_SERVER"; then
        start_server_daemon
    fi

    info "Rebuild complete!"
}

usage() {
    cat <<EOF
Multipass VM management for puppetc server/agent testing

Usage: $0 <command> [args]

Commands:
  up              Create and start server + agent VMs
  down            Stop VMs (preserves state)
  destroy         Destroy VMs completely
  rebuild         Rebuild and reinstall packages after code changes
  ssh server      SSH into server VM
  ssh agent       SSH into agent VM
  agent [opts]    Run puppetc-agent (default: -n for noop)
  logs            Follow server logs
  status          Show VM status

Examples:
  $0 up                    # Start everything
  $0 agent -n              # Run agent in noop mode
  $0 agent -a              # Run agent in apply mode
  $0 logs                  # Watch server logs
  $0 rebuild               # After code changes, rebuild packages
  $0 down                  # Stop VMs
  $0 destroy               # Clean up

Edit puppetcode/manifests/site.pp - changes are live (no rebuild needed)!
For code changes, run: $0 rebuild
EOF
}

# Main
case "${1:-}" in
    up)         up ;;
    down)       down ;;
    destroy)    destroy ;;
    rebuild)    rebuild ;;
    ssh)        ssh_vm "${2:-}" ;;
    agent)      shift; run_agent "$@" ;;
    logs)       logs ;;
    status)     status ;;
    -h|--help|help) usage ;;
    *)
        usage
        exit 1
        ;;
esac
