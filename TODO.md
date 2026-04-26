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
- [x] `PUPPET_STMT_RESOURCE_CHAIN` - Ordering arrows (`Package['x'] -> Service['y']`)
- [x] `PUPPET_STMT_APPEND` - Array/hash append (`$arr += ['value']`, `$hash += {key => val}`)
- [x] `PUPPET_STMT_REQUIRE` - `require class_name`
- [x] `PUPPET_STMT_CONTAIN` - `contain class_name`
- [x] `PUPPET_STMT_TAG` - `tag 'tagname'` (applies tags to resources in scope)

### Virtual and Exported Resources
- [x] Virtual resources (`@resource` syntax)
- [x] `realize()` function
- [x] Virtual resource collectors (`<| |>` with filters)
- [x] Exported resources (`@@resource` syntax) - stored to PuppetDB
- [x] Exported resource collectors from PuppetDB (`<<| |>>`)

### Resource Relationships
- [x] `require` metaparameter (ordering)
- [x] `before` metaparameter (ordering)
- [x] `notify` metaparameter (ordering + refresh)
- [x] `subscribe` metaparameter (ordering + refresh)
- [x] Refresh triggers (service restart, exec refresh)
- [x] `refreshonly` parameter for exec resources

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
- [x] ERB templates — native C engine for the common subset (instance vars, `scope.lookupvar`/`scope[]`, indexing, comments, `<%-` / `-%>` trim markers), embedded Ruby fallback for the rest. Cached AST shared across nodes.
- [x] EPP templates (Embedded Puppet)

#### Native ERB engine — extensions
The native renderer (`compiler/puppet_erb_native.c`) currently covers ~2/3 of templates seen in our corpus; the rest hit the Ruby fallback. Plausible additions in order of payoff vs. complexity:
- [ ] `"#{expr}"` interpolated strings (single-line, no nested blocks)
- [ ] Boolean / comparison operators in expressions (`==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`, `!`)
- [ ] `<% if EXPR %> ... <% else %> ... <% end %>` conditional blocks
- [ ] `.length`, `.size`, `.empty?`, `.to_s` method calls on supported types
- [ ] `<% @arr.each do |x| %> ... <% end %>` loops (most common remaining shape)

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
- [x] ssh_authorized_key - SSH public keys
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
- [x] SSL/TLS encryption (TLS 1.2+, HTTPS endpoints)
- [x] Client certificate authentication (mTLS with X.509)
- [x] Certificate Authority (CA) infrastructure
- [x] CSR signing workflow
- [x] Auto-signing (policy/whitelist/naive modes)

### PuppetDB (SQLite)
- [x] SQLite database backend
- [x] Store facts on catalog request
- [x] Store catalogs
- [x] Query nodes (`/pdb/query/v4/nodes`)
- [x] Query facts (`/pdb/query/v4/facts/<certname>`)
- [x] Query catalogs (`/pdb/query/v4/catalogs/<certname>`)
- [x] Store/query exported resources
- [x] Exported resource collectors (`<<| |>>`)

### Environments
- [ ] Multiple environment support
- [ ] Environment isolation

## Agent Features

### Security
- [x] mTLS client authentication with certificate validation
- [x] Automatic CSR generation and submission workflow
- [x] Certificate storage with secure file permissions (0600 for keys)
- [x] HTTPS-only communication with server
- [x] Certificate validation (hostname, chain, expiry)

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
- [x] Multi-suite check targets: parallel-consistency, multinode-isolation, class-loader, hiera-advanced, catalog-refs, source-URL, unknown-type, template-fidelity
- [x] Memory leak testing (Valgrind) — `make check-valgrind` with suppressions for libruby/libssl/libtree-sitter init noise
- [ ] Native-vs-Ruby ERB parity test — render a fixed template set with both engines, assert byte-for-byte equality (today `template-fidelity` only checks Ruby vs reference)
- [ ] Performance benchmarks (in tree, with reproducible corpus)
- [ ] Coverage reporting

## Compiler internals

### Refactor — Phase 1 (done)
- [x] Pull shared, read-mostly state into `puppet_program_state_t` (loader, facts_db, data providers, deadcode tracker, top-level statements, ruby type registries, skip_erb, verbose). Per-node `puppet_env_t` now holds a pointer + `owns_prog` flag, cloned envs share the same `prog`.
- [x] Decouple ERB skip from `-P` mode. Native ERB engine renders the common subset thread-safely; templates outside the subset are marshalled to a single Ruby daemon thread (puppetresources-style — see `Puppet/Runner/Erb.hs::templateDaemon`). libruby is only ever called from that one OS thread, so `-P` is safe and faithful. `skip_erb` is now an explicit opt-in only.

### Refactor — Phase 2 (pending)
- [ ] Split per-compile state into a dedicated `puppet_compile_t` (resources collected during this node's compile, scope chain, virtual/exported pools, deferred function calls). Today `puppet_env_clone_for_node` shares some pointers it shouldn't.
- [ ] Re-attempt moving class/define/node registries to shared `prog`. The earlier attempt segfaulted because the hash carriers point into AST other workers may inspect — needs either a deep-copy boundary or a per-compile registry layered over a shared one.

### Hygiene
- [ ] Drop or revisit the `// TODO Phase 1F` comments in `compiler/puppet_interpreter.c` left from the reverted Phase 1F migration (class_definitions / define_types / node_definitions to shared `prog`).
- [ ] The native ERB cache is unbounded and never freed (lifetime = process). Fine for one-shot CLI runs but `puppetc-server` will need an eviction policy if it ever serves many distinct templates.

## Packaging

- [x] Debian packages
- [ ] RPM packages
- [ ] Docker Hub images
