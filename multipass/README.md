# Puppetc Multipass Test Environment

Lightweight alternative to Vagrant/VirtualBox for testing puppetc server/agent.

## Quick Start

```bash
# Install multipass
brew install multipass        # macOS
sudo snap install multipass   # Ubuntu

# Start server + agent VMs
cd multipass
./puppetc-vm.sh up

# Test agent
./puppetc-vm.sh agent -n      # noop mode
./puppetc-vm.sh agent -a      # apply mode
```

## Commands

| Command | Description |
|---------|-------------|
| `./puppetc-vm.sh up` | Create and start server + agent VMs |
| `./puppetc-vm.sh down` | Stop VMs (preserves state) |
| `./puppetc-vm.sh destroy` | Delete VMs completely |
| `./puppetc-vm.sh ssh server` | SSH into server |
| `./puppetc-vm.sh ssh agent` | SSH into agent |
| `./puppetc-vm.sh agent -n` | Run agent (noop) |
| `./puppetc-vm.sh agent -a` | Run agent (apply) |
| `./puppetc-vm.sh logs` | Follow server logs |
| `./puppetc-vm.sh status` | Show VM status |

## Testing Puppet Code

Edit `puppetcode/manifests/site.pp` on your host - changes are immediately available:

```bash
# Edit manifest
vim ../puppetcode/manifests/site.pp

# Test (no restart needed)
./puppetc-vm.sh agent -n
```

## Comparison with Vagrant

| | Multipass | Vagrant/VirtualBox |
|---|---|---|
| Startup | ~10 sec | ~2 min |
| Hypervisor | Native (HVF/KVM) | VirtualBox |
| Memory | Low | High |
| Install | `brew install multipass` | VirtualBox + Vagrant |
