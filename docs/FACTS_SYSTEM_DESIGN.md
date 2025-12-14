# Facts System Design

## Overview

Implementation of a facts loading system compatible with PuppetDB JSON exports, providing node-specific facts as variables in the Puppet execution environment.

## Facts File Format

### JSON Structure (PuppetDB Compatible)

```json
[
  {
    "certname": "web01.example.com",
    "environment": "production",
    "facts": {
      "hostname": "web01",
      "operatingsystem": "Ubuntu", 
      "operatingsystemrelease": "20.04",
      "architecture": "x86_64",
      "ipaddress": "192.168.1.100",
      "fqdn": "web01.example.com",
      "memory": {
        "system": {
          "total": "4.00 GiB",
          "available": "2.50 GiB"
        }
      },
      "networking": {
        "interfaces": {
          "eth0": {
            "ip": "192.168.1.100",
            "mac": "00:16:3e:12:34:56"
          }
        }
      }
    }
  },
  {
    "certname": "db01.example.com",
    "environment": "production", 
    "facts": {
      "hostname": "db01",
      "operatingsystem": "CentOS",
      "operatingsystemrelease": "8",
      "architecture": "x86_64",
      "ipaddress": "192.168.1.101"
    }
  }
]
```

## Variable Namespace Enhancement

### Current Variable Storage
- Simple string-based hash table
- No namespace support
- Variables stored as `"variable_name" -> value`

### Enhanced Variable Storage 
- Support for qualified names: `"apache::vhost::port"`, `"::hostname"`
- Facts accessible via multiple patterns:
  - `$facts['hostname']` (hash syntax)
  - `$::hostname` (top-scope syntax)
  - `$hostname` (unqualified, if not shadowed)

### Variable Resolution Order
1. Local scope (`$var`)
2. Class scope (`$class::var`)  
3. Node scope (node-specific variables)
4. Global scope (`$::var`)
5. Facts (`$facts[...]` or `$::factname`)
6. Data providers (Hiera, etc.)
7. Default values

## Implementation Plan

### 1. Enhanced Variable Storage

```c
// Enhanced variable key structure
typedef struct {
    char *namespace;        // NULL for unqualified, "apache::vhost" for qualified
    char *name;            // Variable name
    puppet_var_scope_t scope; // LOCAL, CLASS, NODE, GLOBAL, FACT
} puppet_var_key_t;

// Facts storage
typedef struct {
    char *certname;        // Node name
    puppet_hash_t *facts;  // fact_name -> puppet_value_t mapping
    char *environment;     // Optional environment
} puppet_node_facts_t;

typedef struct {
    puppet_node_facts_t *nodes; // Array of node facts
    size_t node_count;
    puppet_hash_t *fact_index;  // certname -> node_facts mapping
} puppet_facts_db_t;
```

### 2. Facts Data Provider

```c
// Facts data provider implementation
puppet_value_t *puppet_facts_lookup(const char *key, puppet_env_t *env, void *data);
int puppet_facts_has_key(const char *key, puppet_env_t *env, void *data);
int puppet_facts_provider_init(void **data, const char *facts_file_path);
void puppet_facts_provider_cleanup(void *data);
```

### 3. JSON Parsing Integration

```c
// JSON facts file loader
puppet_facts_db_t *puppet_load_facts_file(const char *filepath);
int puppet_register_facts_for_node(puppet_env_t *env, const char *certname);
```

### 4. Variable Resolution Enhancement

```c
// Enhanced variable lookup with namespace support
puppet_value_t *puppet_variable_lookup_qualified(puppet_env_t *env, 
                                               const char *namespace,
                                               const char *name);

// Facts-specific lookups
puppet_value_t *puppet_facts_get(puppet_env_t *env, const char *fact_name);
puppet_value_t *puppet_facts_get_nested(puppet_env_t *env, const char *path); // e.g., "memory.system.total"
```

## Command Line Integration

### New Options
- `--facts /path/to/facts.json` - Load facts from JSON file
- `--node <certname>` - Select specific node (existing, enhanced to work with facts)

### Usage Examples

```bash
# Load facts and execute for specific node
./src/puppetc --facts /tmp/puppetdb_export.json --node web01.example.com --eval manifest.pp

# Load facts and execute default node with fact access
./src/puppetc --facts /tmp/facts.json --eval test_facts.pp
```

## Variable Access Patterns

### In Puppet Code

```puppet
# Access facts via $facts hash
$hostname = $facts['hostname']
$os = $facts['operatingsystem']
$ip = $facts['networking']['interfaces']['eth0']['ip']

# Access facts via top-scope variables  
$hostname = $::hostname
$os = $::operatingsystem

# Access nested facts with dot notation (future enhancement)
$memory_total = $facts['memory.system.total']

# Class variables with namespaces
class apache::vhost($port = 80) {
  $apache::vhost::listen_port = $port
  $config_file = "/etc/apache2/sites-enabled/${::hostname}.conf"
}
```

## Testing Strategy

### Test Files
- `tests/facts/simple_facts.json` - Basic facts for testing
- `tests/facts/complex_facts.json` - Nested facts and multiple nodes
- `tests/facts/test_facts_access.pp` - Puppet manifest testing fact access

### Test Scenarios
1. Load facts file and verify parsing
2. Node selection and fact filtering
3. Variable access via `$facts[...]` syntax
4. Variable access via `$::factname` syntax
5. Nested fact access
6. Integration with existing variable system
7. Memory management verification

## Future Enhancements

1. **Structured Facts**: Support for arrays and hashes in facts
2. **Fact Validation**: Type checking and validation of fact values
3. **Fact Interpolation**: Use facts in string interpolation
4. **External Fact Sources**: Plugin system for other fact sources
5. **Fact Caching**: Performance optimization for large fact databases
6. **Fact Functions**: Built-in functions for fact manipulation

## Integration Points

### Existing Systems
- **Variable System**: Enhance lookup chain to include facts
- **Data Providers**: Facts become a specialized data provider
- **Node Selection**: Existing `--node` option enhanced for fact-aware execution
- **Memory Management**: All fact storage uses puppet_* memory functions

### New Dependencies
- **JSON Parsing**: May need JSON library integration (or custom parser)
- **File I/O**: Robust file reading with error handling
- **Hash Functions**: Enhanced hash table for nested fact storage