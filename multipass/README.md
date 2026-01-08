# Puppetc Multipass Development Environment

Lightweight Ubuntu VM for development and testing, using [Multipass](https://multipass.run/) instead of VirtualBox/Vagrant.

**Why Multipass?**
- **Fast**: VMs start in seconds (vs minutes for VirtualBox)
- **Light**: Uses native hypervisor (Hypervisor.framework on macOS, KVM on Linux)
- **Simple**: No Vagrantfile complexity, just cloud-init

## Quick Start

```bash
# Install multipass (macOS)
brew install multipass

# Install multipass (Ubuntu)
sudo snap install multipass

# Create development VM
cd multipass
./puppetc-vm.sh create

# Run valgrind memory check
./puppetc-vm.sh valgrind

# SSH into VM for manual testing
./puppetc-vm.sh shell
```

## Commands

| Command | Description |
|---------|-------------|
| `./puppetc-vm.sh create` | Create VM with all dependencies |
| `./puppetc-vm.sh shell` | SSH into VM |
| `./puppetc-vm.sh build` | Build puppetc in VM |
| `./puppetc-vm.sh packages` | Build Debian packages |
| `./puppetc-vm.sh valgrind [file]` | Run valgrind on test file |
| `./puppetc-vm.sh valgrind-full` | Comprehensive memory check |
| `./puppetc-vm.sh test` | Run `make check` |
| `./puppetc-vm.sh stop` | Stop VM (preserves state) |
| `./puppetc-vm.sh start` | Start stopped VM |
| `./puppetc-vm.sh destroy` | Delete VM completely |
| `./puppetc-vm.sh status` | Show VM info |

## Valgrind Testing

```bash
# Quick check on simple manifest
./puppetc-vm.sh valgrind

# Test specific file
./puppetc-vm.sh valgrind tests/puppet/complex.pp

# Comprehensive check (multiple test cases)
./puppetc-vm.sh valgrind-full

# Manual valgrind in VM
./puppetc-vm.sh shell
cd puppetc
export LD_LIBRARY_PATH=./compiler/.libs:./common/.libs:./facter/.libs
valgrind --leak-check=full ./compiler/.libs/puppetc-compile -p tests/puppet/simple.pp
```

## How It Works

1. **VM Creation**: Creates Ubuntu 24.04 VM with cloud-init provisioning
2. **Dependencies**: Installs build tools, libraries, valgrind, gdb
3. **Mount**: Your project directory is mounted at `/home/ubuntu/puppetc`
4. **Build**: Runs in VM but edits happen on host (mounted directory)

## VM Specs

- **OS**: Ubuntu 24.04 LTS
- **CPUs**: 4
- **Memory**: 4GB
- **Disk**: 20GB
- **Mount**: Project directory at `/home/ubuntu/puppetc`

## Comparison with Vagrant/VirtualBox

| Feature | Multipass | Vagrant/VirtualBox |
|---------|-----------|-------------------|
| Startup time | ~10 seconds | ~2 minutes |
| Memory overhead | Low | High |
| Hypervisor | Native (HVF/KVM) | VirtualBox |
| Setup | `brew install multipass` | Install VirtualBox + Vagrant |
| Synced folders | `multipass mount` | VirtualBox shared folders |

## Troubleshooting

### Mount not working
```bash
# Remount project
multipass umount puppetc-dev
multipass mount /path/to/puppet_c puppetc-dev:/home/ubuntu/puppetc
```

### VM won't start
```bash
# Check status
multipass list

# Check logs
multipass info puppetc-dev

# Recreate if needed
./puppetc-vm.sh destroy
./puppetc-vm.sh create
```

### Valgrind shows errors in Ruby
Ruby has known valgrind issues. Focus on leaks in puppetc code:
```bash
valgrind --leak-check=full --suppressions=ruby.supp ./compiler/.libs/puppetc-compile ...
```
