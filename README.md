# Puppet-C

> **Warning:** This project is experimental and under active development.

A fast, lightweight Puppet compiler written in C for local manifest development and CI/CD validation.

## Why Puppet-C?

**The problem**: Ruby Puppet doesn't provide a good way to compile and validate manifests locally without a full Puppet infrastructure. Developers working on Puppet code often need to push changes to test them, making the feedback loop slow and cumbersome.

**The solution**: Puppet-C compiles catalogs locally in under a second, with full support for modules, templates, Hiera, and facts. It's ideal for:

- **Local development**: Validate your manifests and templates before committing
- **CI/CD pipelines**: Check catalog coherence for all nodes in seconds
- **Debugging**: See exactly what resources would be created for any node

## Key Features

- **Fast**: Compile a full catalog with templates in <1 second
- **Parallel validation**: Check hundreds of nodes in parallel for CI/CD
- **Minimal dependencies**: Pure C with optional Ruby for ERB templates
- **Complete toolchain**: Includes compiler, server, agent, and facter binaries

## Quick Start

### Option 1: Docker (No Installation)

```bash
# Clone the repository
git clone https://github.com/ppomes/puppet_c.git
cd puppet_c

# Build and run with Docker
docker compose build compiler
docker compose run --rm compiler -p -n mynode.example.com \
    -f /puppet/facts.yaml -m /puppet/modules /puppet/manifests/site.pp
```

Edit `puppetcode/manifests/site.pp` on your host - changes are reflected immediately.

### Option 2: Ubuntu/Debian Packages

```bash
# Build packages (requires build dependencies)
dpkg-buildpackage -us -uc -b

# Install the compiler
sudo dpkg -i ../puppetc_*.deb ../libpuppetc0_*.deb ../libpuppetc-common0_*.deb

# Run
puppetc-compile -p -n mynode.example.com -m modules/ manifests/site.pp
```

### Option 3: Build from Source

See [Installation](#installation) below.

## Installation

### Prerequisites

**Required** (compiler and facter):
- GCC and standard build tools
- libtree-sitter
- Ruby 3.0-3.3 with development headers (for ERB templates)
- libyaml (for Hiera)
- libssl/openssl (for SSL/TLS and crypto functions)

**Optional** (server and agent):
- libmicrohttpd (for puppetc-server HTTP/HTTPS support)
- libsqlite3 (for PuppetDB support in server)
- libcurl with OpenSSL (for puppetc-agent mTLS)

### Installing Dependencies

**Debian/Ubuntu (compiler only):**
```bash
sudo apt-get install build-essential autoconf automake libtool pkg-config \
  libtree-sitter-dev ruby3.2-dev libyaml-dev libssl-dev
```

**Debian/Ubuntu (all components):**
```bash
sudo apt-get install build-essential autoconf automake libtool pkg-config \
  libtree-sitter-dev ruby3.2-dev libyaml-dev libssl-dev \
  libmicrohttpd-dev libcurl4-openssl-dev libsqlite3-dev
```

**macOS (Homebrew):**
```bash
brew install pkg-config tree-sitter ruby@3.3 libyaml openssl \
  autoconf automake libtool
# Optional, for server/agent:
brew install libmicrohttpd curl sqlite3
```

### Building from Source

**Linux (compiler and facter only):**
```bash
./autogen.sh
./configure
make
make check
```

**Linux (all components):**
```bash
./autogen.sh
./configure --enable-server --enable-agent
make
make check
```

**macOS (with Homebrew, compiler and facter only):**
```bash
./autogen.sh
./configure \
  --with-treesitter=$(brew --prefix tree-sitter) \
  --with-ruby=$(brew --prefix ruby@3.3) \
  --with-yaml=$(brew --prefix libyaml) \
  --with-openssl=$(brew --prefix openssl)
make
make check
```

**macOS (with Homebrew, all components):**
```bash
./autogen.sh
./configure \
  --with-treesitter=$(brew --prefix tree-sitter) \
  --with-ruby=$(brew --prefix ruby@3.3) \
  --with-yaml=$(brew --prefix libyaml) \
  --with-openssl=$(brew --prefix openssl) \
  --with-microhttpd=$(brew --prefix libmicrohttpd) \
  --with-curl=$(brew --prefix curl) \
  --with-sqlite=$(brew --prefix sqlite3) \
  --enable-server --enable-agent
make
make check
```

### macOS (Homebrew formula)

A Homebrew formula is provided for the compiler and facter:

```bash
brew install --HEAD Formula/puppet-c.rb
```

### Configure Options

| Option | Description |
|--------|-------------|
| `--enable-server` | Build puppetc-server (requires libmicrohttpd, sqlite3) |
| `--enable-agent` | Build puppetc-agent (requires libcurl) |
| `--disable-server` | Skip server even if dependencies are present |
| `--disable-agent` | Skip agent even if dependencies are present |
| `--enable-debug` | Enable debug mode |
| `--with-treesitter=PATH` | Path to tree-sitter installation |
| `--with-ruby=PATH` | Path to Ruby installation |
| `--with-yaml=PATH` | Path to libyaml installation |
| `--with-openssl=PATH` | Path to OpenSSL installation |
| `--with-microhttpd=PATH` | Path to libmicrohttpd installation |
| `--with-curl=PATH` | Path to libcurl installation |
| `--with-sqlite=PATH` | Path to SQLite3 installation |

By default, server and agent are built automatically if their dependencies are found, and skipped otherwise.

## Usage

### Compiler (puppetc-compile)

The main tool for local development and CI/CD validation.

```bash
# Pretty output (human-readable, colored)
puppetc-compile -p -n mynode.example.com -m modules/ manifests/site.pp

# With facts file
puppetc-compile -p -n mynode -m modules/ -f facts.yaml manifests/site.pp

# JSON catalog output
puppetc-compile -c -n mynode -m modules/ manifests/site.pp

# Validate all nodes (CI/CD)
puppetc-compile --all-nodes -m modules/ -f allfacts.yaml manifests/site.pp

# Parallel validation (3x faster)
puppetc-compile --all-nodes -P -m modules/ -f allfacts.yaml manifests/site.pp

# Parse only (syntax check)
puppetc-compile manifest.pp

# Verbose output (debug)
puppetc-compile -v -p -n mynode manifests/site.pp
```

Run `puppetc-compile --help` for all options.

**Example output:**
```
notify/system_info:  testnode.example.com
  message => Host: testnode.example.com (192.168.1.10) - OS: Debian,

file//tmp/puppetc-demo:  testnode.example.com
  ensure => directory,
  mode => 0755,

Total: 41 resources
```

### Facter (facter_c)

Native fact collection, compatible with Puppet facts format.

```bash
# Show all facts
facter_c

# Specific facts
facter_c hostname ipaddress osfamily

# JSON output
facter_c -j
```

### Server (puppetc-server)

REST API server for catalog compilation, with embedded PuppetDB and SSL/TLS mutual authentication.

**Features:**
- HTTPS with TLS 1.2+ encryption
- Certificate Authority (CA) for signing agent certificates
- Automatic CA generation on first startup
- Configurable auto-signing (policy-based, whitelist, or naive modes)
- Mutual TLS (mTLS) authentication support

```bash
# Start server (CA auto-generated on first run)
puppetc-server -p 8140 /etc/puppet

# With PuppetDB and custom CA directory
puppetc-server -p 8140 -P /var/lib/puppetc/puppetdb.sqlite \
               -C /etc/puppetc/ssl/ca /etc/puppet

# Compile catalog via API (with mTLS)
curl -X POST https://localhost:8140/puppet/v4/catalog \
     --cacert /etc/puppetc/ssl/ca/ca_crt.pem \
     --cert /var/lib/puppetc/ssl/certs/node1.pem \
     --key /var/lib/puppetc/ssl/private_keys/node1.pem \
     -H 'Content-Type: application/json' \
     -d '{"certname": "node1.example.com", "facts": {"hostname": "node1"}}'

# Query PuppetDB
curl https://localhost:8140/pdb/query/v4/nodes --cacert /etc/puppetc/ssl/ca/ca_crt.pem
curl https://localhost:8140/pdb/query/v4/facts/node1.example.com --cacert /etc/puppetc/ssl/ca/ca_crt.pem
```

**Certificate Storage:**
- CA certificate: `/etc/puppetc/ssl/ca/ca_crt.pem`
- CA private key: `/etc/puppetc/ssl/ca/ca_key.pem` (permissions: 0600)
- Signed certificates: `/etc/puppetc/ssl/ca/signed/`
- Auto-sign config: `/etc/puppetc/autosign.conf`

### Agent (puppetc-agent)

Puppet agent with mTLS authentication for secure catalog retrieval and application.

**Features:**
- Automatic certificate request (CSR) workflow on first run
- Client-side mTLS authentication with certificate validation
- HTTPS-only communication with server
- Secure certificate storage with proper permissions

```bash
# Run agent (connects to localhost:8140 via HTTPS)
# On first run: generates private key, creates CSR, submits to server
puppetc-agent

# Apply catalog resources
puppetc-agent -a

# No-op mode (show what would change)
puppetc-agent -n

# Specify server (HTTPS required)
puppetc-agent -s https://puppet:8140 -a
```

**Certificate Workflow:**
1. Agent checks for existing certificate in `/var/lib/puppetc/ssl/certs/`
2. If missing: generates 2048-bit RSA private key (stored with 0600 permissions)
3. Creates Certificate Signing Request (CSR) with certname
4. Submits CSR to server at `/puppet-ca/v1/certificate_request/:certname`
5. Server auto-signs based on policy (or queues for manual approval)
6. Agent receives and stores signed certificate
7. All subsequent requests use mTLS authentication

**Certificate Storage:**
- Client certificate: `/var/lib/puppetc/ssl/certs/<certname>.pem`
- Private key: `/var/lib/puppetc/ssl/private_keys/<certname>.pem` (permissions: 0600)
- CA certificate: `/var/lib/puppetc/ssl/ca/ca_crt.pem`

**Environment Variables:**
- `PUPPET_SERVER`: Server URL (e.g., `https://puppet:8140`)
- `PUPPET_SSL_DIR`: SSL directory (default: `/var/lib/puppetc/ssl`)
- `PUPPET_CA_PATH`: CA certificate path

### Exporting Facts from PuppetDB

To validate manifests for all your nodes, export facts from your existing PuppetDB:

**Quick export (from PuppetDB server):**
```bash
curl -s 'http://localhost:8080/pdb/query/v4/inventory' | \
  python3 -c 'import json,yaml,sys; d=json.load(sys.stdin); print(yaml.dump({"facts":{n["certname"]:n["facts"] for n in d}}))' \
  > allfacts.yaml
```

**Using the included script:**
```bash
# Local PuppetDB (HTTP, no auth)
./scripts/dump_puppetdb_facts.py -o allfacts.yaml

# Remote PuppetDB with SSL certificates
./scripts/dump_puppetdb_facts.py -H puppetdb.example.com -p 8081 --ssl \
  --cert /etc/puppetlabs/puppet/ssl/certs/$(hostname -f).pem \
  --key /etc/puppetlabs/puppet/ssl/private_keys/$(hostname -f).pem \
  --cacert /etc/puppetlabs/puppet/ssl/certs/ca.pem \
  -o allfacts.yaml

# Filter to specific nodes (PQL query)
./scripts/dump_puppetdb_facts.py --query '["~", "certname", "\\.prod\\."]' -o prod_facts.yaml
```

**Then validate all nodes:**
```bash
puppetc-compile --all-nodes -m modules/ -f allfacts.yaml manifests/site.pp
```

**Note:** PuppetDB typically listens on:
- `localhost:8080` - HTTP (no auth, only from localhost)
- `0.0.0.0:8081` - HTTPS (requires Puppet SSL certificates)

## Docker Development

Full server/agent setup using Docker Compose.

```bash
# Build all images
docker compose build

# Start server
docker compose up -d server

# Run agent (noop mode)
docker compose run --rm agent

# Run agent (apply mode)
docker compose run --rm agent -a

# View logs
docker compose logs -f server
```

Edit `puppetcode/manifests/site.pp` on your host - changes are reflected immediately.

## Demo: Web + Database Infrastructure

A complete demo showing puppetc managing nginx and MariaDB containers using official Puppet Forge modules.

> **Note:** This demo requires **Linux** with Docker. The containers use systemd which requires cgroup support not available on macOS/Windows Docker Desktop.

```bash
# Download required Puppet modules (stdlib, mysql)
./demo/download_modules.sh

# Build demo images
docker compose -f docker-compose.demo.yml build

# Start the infrastructure
docker compose -f docker-compose.demo.yml up -d

# Watch the logs (catalogs being compiled and applied)
docker compose -f docker-compose.demo.yml logs -f

# Test the web server
curl http://localhost:8080
```

**What happens:**
1. `puppetc-server` starts and waits for catalog requests
2. `web` container requests its catalog, receives nginx configuration
3. `db` container requests its catalog using `puppetlabs/mysql` module
4. Both nodes export their host entries to PuppetDB (`@@host`)
5. Both nodes collect exported hosts from PuppetDB (`Host <<| |>>`)
6. Agents apply resources: packages, config files, services, /etc/hosts entries

**Output:**
```html
<!DOCTYPE html>
<html>
<head><title>Puppet-C Demo</title></head>
<body>
<h1>Hello from Puppet-C!</h1>
<p>This page was deployed by puppetc-agent.</p>
<p>Server: web</p>
</body>
</html>
```

The demo uses:
- **web node**: Simple nginx config (inline manifests)
- **db node**: `puppetlabs/mysql` module with `mysql::server` class

Edit `demo/manifests/site.pp` and restart containers to see changes.

```bash
# Cleanup
docker compose -f docker-compose.demo.yml down
```

## Language Support

### What Works

- Classes, resources, nodes, defined types
- Conditionals: if/elsif/else, unless, case, ternary, selector
- Variable scoping, string interpolation, heredocs
- ERB templates (via embedded Ruby)
- EPP templates (native Puppet templating)
- Hiera lookups (YAML backend)
- Module autoloading
- Virtual resources (`@resource`), `realize()`, collectors (`<| |>`)
- Exported resources (`@@resource`), exported collectors (`<<| |>>`) with PuppetDB
- Resource overrides (`Type['title'] { attr => value }`)
- Resource chains (`->`, `~>`, `<-`, `<~`) for ordering and notification
- Array/hash append (`$arr += [val]`, `$hash += {k => v}`)
- Iterator functions: `each()`, `map()`, `filter()`, `reduce()`
- Deferred functions (`Deferred()` with agent-side evaluation)
- Pluginsync (server serves module plugins to agents)
- ~50 stdlib functions

### Implemented Functions

**Logging**: notice, info, warning, debug, err, crit, fail

**Strings**: split, join, chomp, strip, upcase, downcase, capitalize, match, regsubst

**Shell**: shell_escape, shell_join

**Arrays**: concat, flatten, unique, sort, reverse, first, last, length, member, range

**Hashes**: keys, values, has_key, merge

**Numeric**: abs, floor, ceil, round, sqrt, min, max

**Types**: is_string, is_array, is_hash, is_numeric, is_bool, defined

**Path**: basename, dirname, extname

**Crypto**: sha1, md5, base64

**Data**: lookup

**Iterators**: each, map, filter, reduce

**Resources**: realize, create_resources, ensure_resource

### Resource Providers (Agent)

| Resource | Description |
|----------|-------------|
| file | Files, directories, symlinks. Supports `puppet:///` URLs |
| package | Install/remove packages (apt, dnf) |
| service | Manage systemd services |
| exec | Execute commands with conditions |
| cron | Manage cron jobs |
| host | Manage /etc/hosts entries |
| group | Manage system groups |
| user | Manage system users |
| sysctl | Manage kernel parameters |
| mount | Manage filesystem mounts |
| ssh_authorized_key | Manage SSH public keys in authorized_keys |
| notify | Log messages |

### Known Limitations

- **Type matching**: `=~ Type` syntax parsed but not evaluated
- **All-nodes mode**: ERB templates skipped for faster CI/CD validation

## Security

Puppet-C implements production-grade security with mutual TLS authentication between agent and server.

### SSL/TLS Features

- **Mutual TLS (mTLS)**: Both agent and server authenticate using X.509 certificates
- **TLS 1.2+**: Modern TLS protocol with strong cipher suites
- **Certificate Authority**: Server acts as CA, signs agent certificates
- **Auto-Signing Modes**:
  - `none`: Manual approval required (most secure)
  - `policy`: External executable validates CSR
  - `whitelist`: Certname-based with wildcard support
  - `naive`: Auto-sign all requests (testing only)
- **Certificate Validation**: Full X.509 chain validation with hostname verification
- **Secure Storage**: Private keys stored with 0600 permissions

### Auto-Signing Configuration

Create `/etc/puppetc/autosign.conf`:

```ini
# Disable auto-signing (manual approval required)
autosign = none

# Policy-based (recommended for production)
autosign = policy
autosign_policy = /usr/local/bin/autosign_policy.sh

# Whitelist-based (certname patterns)
autosign = whitelist
autosign_whitelist = /etc/puppetc/autosign_whitelist.txt

# Naive mode (testing only - signs all requests)
autosign = naive
```

**Policy Script Example:**
```bash
#!/bin/bash
# Read CSR info from stdin (JSON format)
read -r csr_info

# Extract certname
certname=$(echo "$csr_info" | jq -r '.certname')

# Approve if certname matches pattern
if [[ "$certname" =~ ^(web|db)[0-9]+\.prod\.example\.com$ ]]; then
    exit 0  # Approve
else
    exit 1  # Deny
fi
```

**Whitelist File Example:**
```
# Exact certname matches
web1.example.com
db1.example.com

# Wildcard patterns
*.dev.example.com
test-*.example.com
```

### Certificate Validation

Agents validate server certificates against CA, and servers can validate client certificates:

- **Hostname verification**: Certificate CN/SAN must match server hostname
- **Chain validation**: Certificates must be signed by trusted CA
- **Expiry checks**: Expired certificates rejected automatically
- **No verification bypass**: `SSL_VERIFYPEER` always enabled

## Architecture

```
+-------------------------------------------------------------+
|                      Libraries                              |
+---------------------+---------------------------------------+
|  libpuppetc         |  libfacter_c                          |
|  - Tree-sitter      |  - Native fact collection             |
|  - AST              |  - JSON fact loading                  |
|  - Interpreter      |  - System info                        |
|  - Stdlib           |                                       |
|  - Hiera            |                                       |
|  - Catalog builder  |                                       |
|  - SSL/TLS (OpenSSL)|                                       |
|  - CA infrastructure|                                       |
+---------------------+---------------------------------------+
           |                        |
           v                        v
+-----------------+  +------------------+  +-----------------+
| puppetc-server  |  | puppetc-agent    |  | puppetc-compile |
|                 |  |                  |  |                 |
| - REST API      |  | - Collect facts  |  | - Parse/eval    |
|   (HTTPS/mTLS)  |  | - Request catalog|  | - JSON output   |
| - Compile       |  |   (HTTPS/mTLS)   |  | - Pretty output |
|   catalogs      |  | - Apply catalog  |  | - CI/CD mode    |
| - PuppetDB      |  | - CSR workflow   |  |                 |
|   (SQLite)      |  |                  |  |                 |
| - CA signing    |  |                  |  |                 |
+-----------------+  +------------------+  +-----------------+
```

## Why C?

C was chosen for:
- **Minimal runtime dependencies** - no JVM, no Go runtime, no Rust toolchain needed
- **Native Ruby integration** - Ruby's embedding API is written in C, so integration is direct and natural
- **Portability** - builds with standard toolchains on Linux and macOS

## Related Projects

Inspired by [language-puppet](https://github.com/bartavelle/language-puppet), a Haskell implementation with similar goals. Both projects provide fast, alternative implementations for validating Puppet manifests outside the Ruby toolchain.

## License

This project is open source. See LICENSE file for details.
