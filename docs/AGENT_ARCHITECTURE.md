# Agent Architecture

## Overview

The `puppetc-agent` is a lightweight Puppet agent that fetches catalogs from `puppetc-server` and applies resources to the local system. It supports both native C providers and Ruby providers from Puppet modules.

## Components

```
+------------------+     +------------------+     +------------------+
|  puppetc-agent   |---->|  puppetc-server  |---->|  Catalog JSON    |
+------------------+     +------------------+     +------------------+
        |                                                  |
        v                                                  v
+------------------+     +------------------+     +------------------+
|  Facter (C)      |     |  Ruby VM         |     |  Resource Graph  |
|  + Ruby facts    |     |  (providers)     |     |  (dependencies)  |
+------------------+     +------------------+     +------------------+
        |                        |                         |
        +------------------------+-------------------------+
                                 |
                                 v
                    +------------------------+
                    |   Provider Dispatch    |
                    |   (C or Ruby)          |
                    +------------------------+
                                 |
        +------------+-----------+-----------+------------+
        |            |           |           |            |
        v            v           v           v            v
    +-------+   +--------+  +--------+  +--------+  +---------+
    | file  |   |package |  |service |  |  exec  |  |  Ruby   |
    +-------+   +--------+  +--------+  +--------+  |providers|
                                                    +---------+
```

## Workflow

### 1. Initialization

```c
// Initialize facter for fact collection
facter_ctx_t *facts = facter_init();

// Initialize Ruby VM for custom facts and providers
agent_ruby_context_t *ruby_ctx = agent_ruby_init(libdir);
```

### 2. Fact Collection

Facts are collected from multiple sources:

1. **Native C facts** (facter_c): hostname, OS, network, memory
2. **Ruby custom facts**: Loaded from `<libdir>/facter/*.rb`

```c
// Collect native facts
facter_collect(facts);

// Run custom Ruby facts
agent_ruby_run_custom_facts(ruby_ctx, facts);
```

### 3. Catalog Request

The agent sends facts to the server and receives a catalog:

```
POST /puppet/v4/catalog
{
  "certname": "node.example.com",
  "environment": "production",
  "facts": { ... }
}
```

### 4. Dependency Resolution

Resources are sorted topologically based on:

- `require` - Must apply after dependency
- `before` - Must apply before dependent
- `notify` - Apply after, trigger refresh on change
- `subscribe` - Apply after, receive refresh on dependency change

### 5. Resource Application

Each resource is dispatched to the appropriate provider:

```c
apply_result_t resource_apply(resource_t *resource, apply_context_t *ctx) {
    // Try C provider first
    provider_t *provider = provider_find(resource->type, os_family);
    if (provider) {
        return provider->apply(resource, ctx);
    }

    // Fall back to Ruby provider
    return apply_ruby_provider(ruby_ctx, resource, "apply", ctx);
}
```

## Native C Providers

Located in `agent/provider_*.c`:

| Provider | File | Description |
|----------|------|-------------|
| file | provider_file.c | Files, directories, symlinks |
| package | provider_package.c | apt/dpkg package management |
| service | provider_service.c | systemd service management |
| exec | provider_exec.c | Command execution |
| cron | provider_cron.c | Cron job management |
| host | provider_host.c | /etc/hosts entries |
| user | provider_user.c | User account management |
| group | provider_group.c | Group management |
| sysctl | provider_sysctl.c | Kernel parameters |
| mount | provider_mount.c | Filesystem mounts |
| notify | provider_notify.c | Log messages |
| anchor | provider_anchor.c | Dependency anchors |

### Provider Interface

```c
typedef struct provider {
    const char *name;
    const char *resource_type;
    int os_family;
    apply_result_t (*apply)(const resource_t *resource, apply_context_t *ctx);
    resource_state_t (*check)(const resource_t *resource, apply_context_t *ctx);
    apply_result_t (*refresh)(const resource_t *resource, apply_context_t *ctx);
    void (*cleanup)(void);
} provider_t;
```

### Provider Registration

```c
void provider_file_register(void) {
    provider_register(&file_provider);
}
```

## Refresh Mechanism

When a resource with `notify` changes, it triggers a refresh on subscribers:

```c
// Track which resources changed
if (result == APPLY_CHANGED) {
    entries[idx].changed = true;
}

// After applying all resources, process refreshes
for (size_t i = 0; i < resource_count; i++) {
    if (has_pending_refresh(entries, i)) {
        provider->refresh(resource, ctx);
    }
}
```

## Command Line Options

```
puppetc-agent [OPTIONS]

Options:
  -s, --server URL     Server URL (default: http://localhost:8140)
  -a, --apply          Apply catalog (default: noop)
  -n, --noop           No-op mode (show what would change)
  -v, --verbose        Verbose output
  --ruby               Enable Ruby providers
  --libdir PATH        Ruby library path (default: /var/lib/puppetc/lib)
  --catalog-file FILE  Use local catalog file instead of server
```

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success, no changes or changes applied |
| 1 | Failure, one or more resources failed |
| 2 | Error, could not connect to server |

## See Also

- [RUBY_PROVIDER_INTEGRATION.md](RUBY_PROVIDER_INTEGRATION.md) - Ruby provider details
- [ERB_ARCHITECTURE.md](ERB_ARCHITECTURE.md) - Ruby VM embedding
