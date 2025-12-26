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
./src/puppetc manifest.pp

# Parse and evaluate
./src/puppetc -e manifest.pp

# With facts
./src/puppetc -e -f facts.json manifest.pp

# JSON AST output
./src/puppetc -j manifest.pp
```

Run `./src/puppetc --help` for all options.

### Catalog Server

```bash
# Start server
./server/puppetc-server -p 8140 /etc/puppet/manifests

# Compile catalog via API
curl -X POST http://localhost:8140/puppet/v4/catalog \
     -H 'Content-Type: application/json' \
     -d '{"certname": "node1.example.com"}'
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
- Many stdlib functions (see below)
- Resource providers for: file, package, service, exec, cron, host, group, user

## Known Limitations

### Parser Limitations

- Class instantiation with single quotes may fail due to parser ambiguity:
  ```puppet
  # Use double quotes:
  class { "apache": }
  ```
- **Lambda expressions**: Lambda blocks (`|$x| { ... }`) are parsed but statement-to-expression conversion is incomplete
- **Parameterized types**: Type parameters like `Array[String]` or `Optional[Integer]` are not yet implemented

### Feature Limitations

- **Virtual resources**: The `@resource` syntax for virtual resources is not supported. The `realize()` function exists but cannot realize virtual resources
- **Resource collectors**: The `<| |>` and `<<| |>>` collector syntax is not implemented
- **Resource relationships**: Chaining arrows (`->`, `~>`) have limited support
- **No PuppetDB support**: No integration with PuppetDB for exported resources or queries
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
│ puppetc-server  │  │ puppetc-agent    │  │ puppetc         │
│                 │  │                  │  │ (debug tool)    │
│ - REST API      │  │ - Collect facts  │  │ - Parse/eval    │
│ - Compile       │  │ - Request catalog│  │ - JSON output   │
│   catalogs      │  │ - Apply catalog  │  │ - Template debug│
└─────────────────┘  └──────────────────┘  └─────────────────┘
```