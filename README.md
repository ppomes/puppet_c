# Puppet-C

A C implementation of a Puppet language parser and interpreter using flex/bison.

**Note**: This is an experimental project. It implements a subset of the Puppet language and is not a replacement for the real Puppet.

## Building

Prerequisites:
- GCC, flex, bison
- Ruby development headers (for ERB templates)
- libyaml (for Hiera)
- libssl/openssl (for crypto functions)
- libmicrohttpd (for puppetc-server)
- libcurl (for puppetc-agent)
- libsqlite3 (for PuppetDB)

```bash
autoreconf -i
./configure
make
make check
```

## Docker

Build and run using Docker (no dependencies needed on host).

Sample manifests, modules, and hiera data are provided in `puppetcode/` (shared with Vagrant).

```bash
# Build images
docker-compose build

# Start server
docker-compose up -d server

# Run agent once (noop mode)
docker-compose run --rm agent

# Run agent once (apply mode)
docker-compose run --rm agent -a

# Interactive shell for multiple agent runs
docker-compose up -d agent-shell
docker-compose exec agent-shell bash
# Inside container:
#   puppetc-agent -n    # noop mode
#   puppetc-agent -a    # apply mode
```

Edit `puppetcode/manifests/site.pp` on your host - changes are reflected immediately in the server.

## Usage

```bash
# Parse only
./compiler/puppetc-compile manifest.pp

# Parse and evaluate
./compiler/puppetc-compile -e manifest.pp

# With facts
./compiler/puppetc-compile -e -f facts.json manifest.pp

# Compile to catalog JSON
./compiler/puppetc-compile -c manifest.pp

# JSON AST output
./compiler/puppetc-compile -j manifest.pp
```

Run `./compiler/puppetc-compile --help` for all options.

### Catalog Server

```bash
# Start server (pass base directory, not manifests/)
./server/puppetc-server -p 8140 /etc/puppet

# With PuppetDB enabled
./server/puppetc-server -p 8140 -P /var/lib/puppetc/puppetdb.sqlite /etc/puppet

# Compile catalog via API
curl -X POST http://localhost:8140/puppet/v4/catalog \
     -H 'Content-Type: application/json' \
     -d '{"certname": "node1.example.com", "facts": {"hostname": "node1"}}'
```

### PuppetDB

The server includes an embedded SQLite-based PuppetDB for storing facts and catalogs.

```bash
# Query all nodes
curl http://localhost:8140/pdb/query/v4/nodes

# Get facts for a node
curl http://localhost:8140/pdb/query/v4/facts/node1.example.com

# Get catalog for a node
curl http://localhost:8140/pdb/query/v4/catalogs/node1.example.com
```

### Native Fact Collection

```bash
# Show all facts
./facter/facter_c

# Specific facts
./facter/facter_c hostname ipaddress osfamily

# JSON output
./facter/facter_c -j
```

### Puppet Agent

```bash
# Run agent (connects to localhost:8140)
./agent/puppetc-agent

# Apply catalog resources
./agent/puppetc-agent -a

# No-op mode (show what would change)
./agent/puppetc-agent -n

# Specify server
./agent/puppetc-agent -s http://puppet:8140 -a

# Just show collected facts
./agent/puppetc-agent -f
```

## What Works

- Basic parsing of classes, resources, nodes, defines
- Conditionals: if/elsif/else, unless, case, ternary, selector
- Variable scoping and interpolation
- ERB templates (via embedded Ruby)
- Hiera lookups (YAML backend)
- Module autoloading
- Virtual resources (`@resource`), `realize()`, collectors (`<| |>`), and exported collectors (`<<| |>>`)
- Iterator functions: `each()`, `map()`, `filter()`, `reduce()`
- PuppetDB (SQLite): stores facts and catalogs, query API
- Many stdlib functions (see below)
- Resource providers for: file, package, service, exec, cron, host, group, user, sysctl, mount

## Known Limitations

### Parser Limitations

- Class instantiation with single quotes may fail due to parser ambiguity:
  ```puppet
  # Use double quotes:
  class { "apache": }
  ```
- **Lambda expressions**: Lambda blocks (`|$x| { ... }`) are parsed but statement-to-expression conversion is incomplete
- **Type matching**: The `=~ Type` syntax (e.g., `$var =~ String`) is not yet supported
- **Method-style calls**: Chained method calls with lambdas like `$array.each |$x| { }` are not yet supported
- **Function calls without parentheses**: Statements like `fail "message"` require parentheses: `fail("message")`

### Feature Limitations

- **Exported resources**: The `@@resource` syntax for exported resources is not supported
- **Resource relationships**: Chaining arrows (`->`, `~>`) have limited support
- **Incomplete stdlib coverage**: Many stdlib functions are implemented but not all

### Runtime Limitations

- **No pluginsync**: Custom facts and functions must be pre-installed
- **Limited error messages**: Parse and runtime errors may not always point to exact location
- **Single-threaded server**: The catalog server handles requests sequentially

## Implemented Functions

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

**Resources**: realize

## Implemented Resources

The agent supports the following resource types:

| Resource | Description |
|----------|-------------|
| file | Manage files, directories, and symlinks. Supports `puppet:///` URLs |
| package | Install/remove packages (apt, dnf) |
| service | Manage systemd services |
| exec | Execute commands with conditions |
| cron | Manage cron jobs |
| host | Manage /etc/hosts entries |
| group | Manage system groups |
| user | Manage system users |
| sysctl | Manage kernel parameters via /proc/sys and /etc/sysctl.d |
| mount | Manage filesystem mounts and /etc/fstab |
| notify | Log messages during catalog application |

## Roadmap

### Target Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Libraries                              │
├─────────────────────┬───────────────────────────────────────┤
│  libpuppetc         │  libfacter_c                          │
│  - Parser/Lexer     │  - Native fact collection             │
│  - AST              │  - JSON fact loading                  │
│  - Interpreter      │  - System info (hostname, os, etc.)   │
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
│   catalogs      │  │ - Apply catalog  │  │ - Catalog gen   │
│ - PuppetDB      │  │                  │  │                 │
│   (SQLite)      │  │                  │  │                 │
└─────────────────┘  └──────────────────┘  └─────────────────┘
```