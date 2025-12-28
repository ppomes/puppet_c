# puppetc TODO

## Language Features

### Expression Types
- [ ] `PUPPET_EXPR_DOT` - Method calls (`$array.each`, `$string.length`)
- [x] `PUPPET_EXPR_LAMBDA` - Lambdas/closures (`|$x| { ... }`)
- [x] Expression-body lambdas (`|$x| { $x * 2 }`)

### Statement Types
- [ ] `PUPPET_STMT_DEFINE` - Defined types (`define mytype($param) { }`)
- [ ] `PUPPET_STMT_RESOURCE_DEFAULT` - Resource defaults (`File { mode => '0644' }`)
- [ ] `PUPPET_STMT_RESOURCE_OVERRIDE` - Resource override (`File['/x'] { attr +> val }`)
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
- [ ] Exported resources (`@@resource` syntax)
- [ ] Exported resource collectors (`<<| |>>`)
- [ ] Resource collection from PuppetDB

### Resource Relationships
- [x] `require` metaparameter (ordering only)
- [x] `before` metaparameter (ordering only)
- [ ] `notify` metaparameter (trigger refresh)
- [ ] `subscribe` metaparameter (trigger refresh)
- [ ] Refresh/restart on dependency change

### Type System
- [ ] Type aliases (`type MyType = String[1]`)
- [ ] Abstract types (`Variant`, `Optional`, `Enum`, etc.)
- [ ] Type validation on parameters
- [ ] Sensitive type

### Templates
- [x] ERB templates
- [ ] EPP templates (Embedded Puppet)

### Functions
- [x] ~50 built-in functions implemented
- [ ] Custom function definitions
- [x] `each()` iterator
- [x] `map()`, `filter()`, `reduce()` iterators
- [ ] `with()` function
- [ ] `assert_type()` function
- [ ] `new()` function for type instantiation

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
- [ ] augeas - Configuration file editing via Augeas
- [ ] yumrepo - Yum repository management
- [ ] apt::source - APT repository management
- [ ] firewall - iptables/nftables rules
- [ ] mailalias - Mail aliases (/etc/aliases)
- [ ] ssh_authorized_key - SSH public keys
- [ ] selboolean - SELinux booleans
- [ ] selmodule - SELinux modules
- [ ] scheduled_task - Windows scheduled tasks
- [ ] registry - Windows registry
- [ ] k5login - Kerberos .k5login files
- [ ] zone - Solaris zones
- [ ] zfs/zpool - ZFS filesystems

### Provider Improvements
- [ ] package: Support yum, dnf, zypper, pacman
- [ ] package: Version pinning (`ensure => '1.2.3'`)
- [ ] service: Support SysV init, upstart, OpenRC
- [ ] file: Recursive directory management
- [ ] file: `purge` parameter
- [ ] file: `recurselimit` parameter
- [ ] file: Windows ACL support

## Server Features

### Security
- [ ] SSL/TLS encryption
- [ ] Client certificate authentication
- [ ] Certificate signing (CA functionality)
- [ ] Certificate revocation (CRL)
- [ ] Autosign configuration

### PuppetDB Integration
- [ ] Store/query exported resources
- [ ] Store facts
- [ ] Store catalogs
- [ ] Store reports
- [ ] PuppetDB query functions (`puppetdb_query()`)

### Environments
- [ ] Multiple environment support
- [ ] Environment isolation
- [ ] Environment caching
- [ ] Code deployment

### Performance
- [ ] Catalog caching
- [ ] Incremental compilation
- [ ] Parallel manifest parsing
- [ ] Connection pooling

## Agent Features

### Reporting
- [ ] Report processor
- [ ] Report submission to server
- [ ] Report storage (local/PuppetDB)
- [ ] Transaction summary

### Daemon Mode
- [ ] Background daemon with interval
- [ ] Splay/randomization
- [ ] Lock file handling
- [ ] Pidfile management

### Features
- [ ] `--tags` filtering
- [ ] `--skip_tags` filtering
- [ ] `--environment` selection
- [ ] Pluginsync (download plugins from server)
- [ ] Custom facts from server

## Facter

### Missing Facts
- [ ] Cloud provider facts (AWS, GCP, Azure)
- [ ] Docker/container facts
- [ ] DMI/BIOS facts
- [ ] Disk facts (partitions, usage)
- [ ] More networking facts (routes, bonds)

### Features
- [ ] Custom facts from modules
- [ ] External facts (executable, JSON, YAML)
- [ ] Fact caching
- [ ] Fact blocking/filtering

## Hiera

### Current
- [x] YAML backend
- [x] Basic hierarchy
- [x] `lookup()` function

### Missing
- [ ] JSON backend
- [ ] HOCON backend
- [ ] eYAML (encrypted YAML)
- [ ] Custom backends
- [ ] `hiera_include()` function
- [ ] Deep merge strategies
- [ ] Lookup options in data

## Testing

### Current
- [x] Parser unit tests
- [x] Docker integration tests

### Needed
- [ ] Memory leak testing (Valgrind CI)
- [ ] Fuzzing (AFL/libFuzzer)
- [ ] Performance benchmarks
- [ ] Comparison tests with Ruby Puppet
- [ ] Coverage reporting

## Documentation

- [ ] Man pages
- [ ] API documentation (Doxygen)
- [ ] User guide
- [ ] Migration guide from Ruby Puppet

## Packaging

- [x] Debian packages
- [ ] RPM packages
- [ ] Homebrew formula
- [ ] Docker Hub images
- [ ] Static binary builds

## Compatibility

- [ ] Puppet 7 language features
- [ ] Puppet 8 language features
- [ ] Backwards compatibility mode
- [ ] Strict mode warnings
