# Puppet-C

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
- **language-puppet compatible**: Similar pretty output format and workflow

## Why C?

C was chosen for:
- **Minimal runtime dependencies** - no JVM, no Go runtime, no Rust toolchain needed
- **Native Ruby integration** - Ruby's embedding API is written in C, so integration is direct and natural
- **Portability** - builds with standard toolchains on Linux and macOS

## Related Projects

Inspired by [language-puppet](https://github.com/bartavelle/language-puppet), a Haskell implementation with similar goals. Both projects provide fast, alternative implementations for validating Puppet manifests outside the Ruby toolchain.

## Quick Start

```bash
# Compile a catalog for a node (pretty output)
./compiler/puppetc-compile -p -n mynode.example.com -m modules/ manifests/site.pp

# With facts file
./compiler/puppetc-compile -p -n mynode -m modules/ -f facts.yaml manifests/site.pp

# Validate ALL nodes in parallel (great for CI/CD)
./compiler/puppetc-compile --all-nodes -P -m modules/ -f allfacts.yaml manifests/site.pp
```

## Building

### Prerequisites

- GCC and standard build tools
- libtree-sitter
- Ruby 3.0-3.3 with development headers (for ERB templates)
- libyaml (for Hiera)
- libssl/openssl (for crypto functions)
- libmicrohttpd (for puppetc-server)
- libcurl (for puppetc-agent)
- libsqlite3 (for PuppetDB)

### Installing Dependencies

**Debian/Ubuntu:**
```bash
sudo apt-get install build-essential autoconf automake libtool \
  libtree-sitter-dev ruby3.2-dev libyaml-dev libssl-dev \
  libmicrohttpd-dev libcurl4-openssl-dev libsqlite3-dev
```

**macOS (Homebrew):**
```bash
brew install pkg-config tree-sitter ruby@3.3 libyaml openssl \
  libmicrohttpd curl sqlite3 autoconf automake libtool
```

### Building from Source

**Linux:**
```bash
./autogen.sh
./configure
make
make check
```

**macOS (with Homebrew):**
```bash
./autogen.sh
./configure \
  --with-treesitter=/opt/homebrew/opt/tree-sitter \
  --with-ruby=/opt/homebrew/opt/ruby@3.3 \
  --with-yaml=/opt/homebrew/opt/libyaml \
  --with-openssl=/opt/homebrew/opt/openssl \
  --with-microhttpd=/opt/homebrew/opt/libmicrohttpd \
  --with-curl=/opt/homebrew/opt/curl \
  --with-sqlite=/opt/homebrew/opt/sqlite3
make
make check
```

Note: On Intel Macs, use `/usr/local/opt/` instead of `/opt/homebrew/opt/`.

## Compiler Usage

The main tool for local development and CI/CD validation.

```bash
# Pretty output (human-readable, colored)
./compiler/puppetc-compile -p -n mynode.example.com -m modules/ manifests/site.pp

# With facts
./compiler/puppetc-compile -p -n mynode -m modules/ -f facts.yaml manifests/site.pp

# JSON catalog output
./compiler/puppetc-compile -c -n mynode -m modules/ manifests/site.pp

# Validate all nodes (CI/CD)
./compiler/puppetc-compile --all-nodes -m modules/ -f allfacts.yaml manifests/site.pp

# Parallel validation (much faster for many nodes)
./compiler/puppetc-compile --all-nodes -P -m modules/ -f allfacts.yaml manifests/site.pp

# Parse only (syntax check)
./compiler/puppetc-compile manifest.pp

# Verbose output (show variable assignments, debug info)
./compiler/puppetc-compile -v -p -n mynode manifests/site.pp
```

Run `./compiler/puppetc-compile --help` for all options.

### Pretty Output Example

```
notify/system_info:  testnode.example.com
  message => Host: testnode.example.com (192.168.1.10) - OS: Debian,

file//tmp/puppetc-demo:  testnode.example.com
  ensure => directory,
  mode => 0755,

file//tmp/puppetc-demo/system-info.txt:  testnode.example.com
  ensure => present,
  content => # System Information
Hostname: testnode
FQDN: testnode.example.com
,
  mode => 0644,

Total: 41 resources
```

## Docker

Build and run using Docker (no dependencies needed on host).

```bash
# Build images
docker-compose build

# Start server
docker-compose up -d server

# Run agent (noop mode)
docker-compose run --rm agent

# Run agent (apply mode)
docker-compose run --rm agent -a
```

Edit `puppetcode/manifests/site.pp` on your host - changes are reflected immediately.

## Other Tools

### Facter (facter_c)

Native fact collection, compatible with Puppet facts format.

```bash
# Show all facts
./facter/facter_c

# Specific facts
./facter/facter_c hostname ipaddress osfamily

# JSON output
./facter/facter_c -j
```

### Server (puppetc-server)

REST API server for catalog compilation, with embedded PuppetDB.

```bash
# Start server
./server/puppetc-server -p 8140 /etc/puppet

# With PuppetDB enabled
./server/puppetc-server -p 8140 -P /var/lib/puppetc/puppetdb.sqlite /etc/puppet

# Compile catalog via API
curl -X POST http://localhost:8140/puppet/v4/catalog \
     -H 'Content-Type: application/json' \
     -d '{"certname": "node1.example.com", "facts": {"hostname": "node1"}}'

# Query PuppetDB
curl http://localhost:8140/pdb/query/v4/nodes
curl http://localhost:8140/pdb/query/v4/facts/node1.example.com
```

### Agent (puppetc-agent)

Basic Puppet agent for applying catalogs.

```bash
# Run agent (connects to localhost:8140)
./agent/puppetc-agent

# Apply catalog resources
./agent/puppetc-agent -a

# No-op mode (show what would change)
./agent/puppetc-agent -n

# Specify server
./agent/puppetc-agent -s http://puppet:8140 -a
```

## Language Support

### What Works

- Classes, resources, nodes, defined types
- Conditionals: if/elsif/else, unless, case, ternary, selector
- Variable scoping, string interpolation, heredocs
- ERB templates (via embedded Ruby)
- Hiera lookups (YAML backend)
- Module autoloading
- Virtual resources (`@resource`), `realize()`, collectors (`<| |>`)
- Resource overrides (`Type['title'] { attr => value }`)
- Iterator functions: `each()`, `map()`, `filter()`, `reduce()`
- ~50 stdlib functions

### Implemented Functions

**Logging**: notice, info, warning, debug, err, fail

**Strings**: split, join, chomp, strip, upcase, downcase, capitalize, match, regsubst

**Arrays**: concat, flatten, unique, sort, reverse, first, last, length, member, range

**Hashes**: keys, values, has_key, merge

**Numeric**: abs, floor, ceil, round, sqrt, min, max

**Types**: is_string, is_array, is_hash, is_numeric, is_bool, defined

**Path**: basename, dirname, extname

**Crypto**: sha1, md5, base64

**Data**: lookup

**Iterators**: each, map, filter, reduce

**Resources**: realize, create_resources

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
| notify | Log messages |

### Known Limitations

- **Type matching**: `=~ Type` syntax parsed but not evaluated
- **Exported resources**: `@@resource` parsed but not sent to PuppetDB
- **Resource chains**: `->`, `~>` have limited support
- **Parallel mode**: ERB templates skipped (Ruby not thread-safe)
- **No pluginsync**: Custom facts/functions must be pre-installed

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Libraries                              │
├─────────────────────┬───────────────────────────────────────┤
│  libpuppetc         │  libfacter_c                          │
│  - Tree-sitter      │  - Native fact collection             │
│  - AST              │  - JSON fact loading                  │
│  - Interpreter      │  - System info                        │
│  - Stdlib           │                                       │
│  - Hiera            │                                       │
│  - Catalog builder  │                                       │
└─────────────────────┴───────────────────────────────────────┘
           │                        │
           ▼                        ▼
┌─────────────────┐  ┌──────────────────┐  ┌─────────────────┐
│ puppetc-server  │  │ puppetc-agent    │  │ puppetc-compile │
│                 │  │                  │  │                 │
│ - REST API      │  │ - Collect facts  │  │ - Parse/eval    │
│ - Compile       │  │ - Request catalog│  │ - JSON output   │
│   catalogs      │  │ - Apply catalog  │  │ - Pretty output │
│ - PuppetDB      │  │                  │  │ - CI/CD mode    │
│   (SQLite)      │  │                  │  │                 │
└─────────────────┘  └──────────────────┘  └─────────────────┘
```

## License

This project is open source. See LICENSE file for details.
