# puppetc TODO

## Language Features

### Expression Types
- [ ] `PUPPET_EXPR_DOT` - Method calls (`$array.each`, `$string.length`)
- [x] `PUPPET_EXPR_LAMBDA` - Lambdas/closures (`|$x| { ... }`)
- [x] Expression-body lambdas (`|$x| { $x * 2 }`)

### Statement Types
- [x] `PUPPET_STMT_DEFINE` - Defined types (`define mytype($param) { }`)
- [~] `PUPPET_STMT_RESOURCE_DEFAULT` - Resource defaults (`File { mode => '0644' }`) - parsed, not applied
- [x] `PUPPET_STMT_RESOURCE_OVERRIDE` - Resource override (`File['/x'] { attr => val }`)
- [x] `PUPPET_STMT_RESOURCE_COLLECTOR` - Virtual collectors (`File <| ensure == present |>`)
- [ ] `PUPPET_STMT_RESOURCE_CHAIN` - Ordering arrows (`Package['x'] -> Service['y']`)
- [ ] `PUPPET_STMT_APPEND` - Array append (`$arr += ['value']`)
- [x] `PUPPET_STMT_REQUIRE` - `require class_name`
- [x] `PUPPET_STMT_CONTAIN` - `contain class_name`
- [ ] `PUPPET_STMT_TAG` - `tag 'tagname'`

### Virtual and Exported Resources
- [x] Virtual resources (`@resource` syntax)
- [x] `realize()` function
- [x] Virtual resource collectors (`<| |>` with filters)
- [~] Exported resources (`@@resource` syntax) - parsed, not stored to PuppetDB
- [ ] Exported resource collectors from PuppetDB (`<<| |>>`)

### Resource Relationships
- [x] `require` metaparameter (ordering only)
- [x] `before` metaparameter (ordering only)
- [ ] `notify` metaparameter (trigger refresh)
- [ ] `subscribe` metaparameter (trigger refresh)

### Type System
- [ ] Type aliases (`type MyType = String[1]`)
- [ ] Abstract types (`Variant`, `Optional`, `Enum`, etc.)
- [ ] Type validation on parameters
- [~] Sensitive type - Basic pass-through implemented, needs:
  - [ ] Sensitive value wrapper type (mask data in logs/output)
  - [ ] Compiler: Redact Sensitive values in catalog JSON
  - [ ] Server: Mask Sensitive values in stored catalogs/reports
  - [ ] Agent: Handle Sensitive values during application

### Templates
- [x] ERB templates
- [x] EPP templates (Embedded Puppet)

### Functions
- [x] ~100 stdlib functions implemented
- [x] `each()`, `map()`, `filter()`, `reduce()` iterators
- [x] `create_resources()` function
- [x] `deep_merge()`, `delete_undef_values()` hash functions
- [x] `prefix()`, `suffix()`, `union()`, `intersection()`, `difference()` array functions
- [x] `zip()`, `count()`, `shuffle()` array utilities
- [x] `swapcase()`, `squeeze()`, `shell_split()` string functions
- [x] `clamp()` numeric function
- [x] `any2bool()`, `bool2num()`, `num2bool()` type conversions
- [ ] Custom function definitions (`function mymod::myfunc() {}`)
- [ ] `with()` function
- [ ] `assert_type()` function
- [ ] `loadyaml()`, `parseyaml()`, `loadjson()`, `parsejson()` data loading

## Providers

### Implemented (11)
- [x] file
- [x] exec
- [x] notify
- [x] package (apt/dpkg)
- [x] service (systemd)
- [x] cron
- [x] host
- [x] user
- [x] group
- [x] sysctl
- [x] mount

### Missing
- [ ] augeas - Configuration file editing
- [ ] ssh_authorized_key - SSH public keys
- [ ] firewall - iptables/nftables rules
- [ ] mailalias - Mail aliases

### Provider Improvements
- [ ] package: Support yum, dnf, zypper
- [ ] package: Version pinning (`ensure => '1.2.3'`)
- [ ] service: Support SysV init, OpenRC
- [ ] file: Recursive directory management
- [ ] file: `purge` parameter

## Server Features

### Security
- [ ] SSL/TLS encryption
- [ ] Client certificate authentication

### PuppetDB (SQLite)
- [x] SQLite database backend
- [x] Store facts on catalog request
- [x] Store catalogs
- [x] Query nodes (`/pdb/query/v4/nodes`)
- [x] Query facts (`/pdb/query/v4/facts/<certname>`)
- [x] Query catalogs (`/pdb/query/v4/catalogs/<certname>`)
- [ ] Store/query exported resources
- [ ] Exported resource collectors (`<<| |>>`)

### Environments
- [ ] Multiple environment support
- [ ] Environment isolation

## Agent Features

### Reporting
- [ ] Report processor
- [ ] Report submission to server

### Daemon Mode
- [ ] Background daemon with interval
- [ ] Lock file handling

### Features
- [ ] `--tags` filtering
- [ ] `--environment` selection
- [ ] Pluginsync

## Facter

### Missing Facts
- [ ] Cloud provider facts (AWS, GCP, Azure)
- [ ] Docker/container facts
- [ ] Disk facts (partitions, usage)

### Features
- [ ] Custom facts from modules
- [ ] External facts (executable, JSON, YAML)

## Hiera

### Current
- [x] YAML backend
- [x] Basic hierarchy
- [x] `lookup()` function

### Missing
- [ ] JSON backend
- [ ] eYAML (encrypted YAML)
- [ ] Deep merge strategies
- [ ] `hiera_include()` function

## Testing

- [x] Parser unit tests
- [x] Docker integration tests
- [x] Stdlib function tests (`tests/test_stdlib_extra.sh`)
- [ ] Memory leak testing (Valgrind)
- [ ] Performance benchmarks
- [ ] Coverage reporting

## Packaging

- [x] Debian packages
- [ ] RPM packages
- [ ] Docker Hub images
