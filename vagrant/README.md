# Puppetc Vagrant Test Environment

Two Debian 12 VMs for testing puppetc:
- **server** (192.168.56.10) - runs puppetc-server
- **agent** (192.168.56.11) - runs puppetc-agent

## Quick Start

```bash
cd vagrant

# Start server (builds packages, installs server)
vagrant up server

# Start agent
vagrant up agent

# Test
vagrant ssh agent
puppetc-agent -s http://192.168.56.10:8140 -n   # noop
puppetc-agent -s http://192.168.56.10:8140 -a   # apply
```

## Directory Structure

```
vagrant/
├── Vagrantfile
├── README.md
├── puppetcode/           # Shared with server as /etc/puppet/manifests
│   └── site.pp           # Edit this to test your Puppet code!
└── .gitignore
```

## Testing Puppet Code

Edit `puppetcode/site.pp` on your host - changes are immediately available on the server!

```bash
# Edit manifest on host
vim puppetcode/site.pp

# Test on agent (no need to restart server)
vagrant ssh agent -c "puppetc-agent -s http://192.168.56.10:8140 -n"
```

## Commands

```bash
# Start both VMs
vagrant up

# SSH into VMs
vagrant ssh server
vagrant ssh agent

# Restart server after manifest changes (usually not needed)
vagrant ssh server -c "sudo systemctl restart puppetc-server"

# Rebuild packages after C code changes
vagrant rsync server
vagrant ssh server -c "cd /vagrant_src && make && dpkg-buildpackage -us -uc -b"
vagrant ssh server -c "sudo apt-get install -y --reinstall ../*.deb && sudo systemctl restart puppetc-server"

# Destroy and recreate
vagrant destroy -f
vagrant up
```

## VM Details

| VM | IP | Purpose |
|----|----|----|
| server | 192.168.56.10 | puppetc-server on port 8140 |
| agent | 192.168.56.11 | puppetc-agent |

## Testing on Agent

```bash
vagrant ssh agent

# Show facts
puppetc-agent -s http://192.168.56.10:8140 -f

# Noop mode (dry run)
puppetc-agent -s http://192.168.56.10:8140 -n

# Apply catalog
puppetc-agent -s http://192.168.56.10:8140 -a

# Verbose mode
puppetc-agent -s http://192.168.56.10:8140 -a -v
```

## Server Management

```bash
vagrant ssh server

# Check server status
sudo systemctl status puppetc-server

# View server logs
sudo journalctl -u puppetc-server -f

# Restart server
sudo systemctl restart puppetc-server

# Manifests are at /etc/puppet/manifests (synced from puppetcode/)
ls /etc/puppet/manifests/
```
