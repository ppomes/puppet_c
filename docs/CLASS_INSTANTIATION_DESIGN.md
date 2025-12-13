# Class Instantiation with Variables - Design Document

## Goals

Implement complete class instantiation with parameters, default values, and variable resolution, preparing for future Hiera integration.

## Current State Analysis

### ✅ Already Implemented
- **Parameter Parsing**: `class name($param1, $param2 = 'default') { ... }`
- **AST Support**: `puppet_param_t` with name, type constraint, and default value
- **Basic Scoping**: Parent/child scope relationships with variable storage
- **Variable Storage**: Hash table-based variable storage in scopes

### ❌ Missing Components
1. **Class Instantiation Syntax**: `class { 'apache': port => 80 }`
2. **Parameter Resolution**: Match args to parameters with defaults
3. **Variable Lookup Chain**: Local → Class → Global → Hiera
4. **Enhanced Variable Assignment**: `$var = value` statements
5. **Variable Interpolation**: `"String with ${variable}"`

## Implementation Strategy

### Phase 1: Enhanced Variable Management 🔄

#### 1.1 Variable Lookup Chain Design
```
Variable Resolution Order:
1. Local Scope (current function/class)
2. Class Scope (if inside class)  
3. Node Scope (node-specific variables)
4. Global Scope (top-level variables)
5. Hiera Lookup (external data) - FUTURE
6. Default Value (from parameter definition)
```

#### 1.2 Variable Assignment Statements
```puppet
$port = 80                    # Simple assignment
$config = {                   # Hash assignment
  'ssl' => true,
  'workers' => 4
}
$files = ['a.txt', 'b.txt']  # Array assignment
```

#### 1.3 Variable Interpolation  
```puppet
$name = "apache"
$path = "/etc/${name}/conf"   # String interpolation
$message = "Port: ${port}"    # Variable substitution
```

#### 1.4 Hiera Integration Preparation
```c
// Hook for future Hiera integration
typedef struct puppet_hiera_provider {
    char *(*lookup)(const char *key, puppet_scope_t *scope);
    bool (*has_key)(const char *key, puppet_scope_t *scope);
} puppet_hiera_provider_t;

// Variable lookup with Hiera hook
puppet_value_t *puppet_variable_lookup(puppet_env_t *env, const char *name);
```

### Phase 2: Class Instantiation Mechanism

#### 2.1 Class Instantiation Syntax
```puppet
# Resource-style class instantiation
class { 'apache':
  port    => 443,
  ssl     => true,
  workers => $::processorcount,
}

# Include-style (parameters from Hiera)
include apache
include apache::ssl
```

#### 2.2 Parameter Resolution
```c
typedef struct {
    char *name;           // Parameter name
    puppet_value_t *value; // Provided value
    bool provided;        // Was value explicitly provided?
} puppet_class_arg_t;

// Match provided arguments to class parameters
int puppet_resolve_class_args(
    puppet_param_list_t *params,     // Class parameters  
    puppet_class_arg_t *args,        // Provided arguments
    size_t arg_count,
    puppet_env_t *env,               // For variable resolution
    puppet_value_t **resolved_values // Output: resolved parameter values
);
```

#### 2.3 Default Value Handling
```c
// Apply defaults for missing parameters
puppet_value_t *puppet_apply_parameter_defaults(
    puppet_param_t *param,
    puppet_env_t *env
);
```

## Detailed Implementation Plan

### Step 1: Enhanced Variable System

#### A. Extend Variable Assignment
```c
// In puppet_interpreter.h
typedef enum {
    PUPPET_VAR_LOCAL,     // $var in local scope
    PUPPET_VAR_CLASS,     // $::class::var 
    PUPPET_VAR_GLOBAL,    // $::var
    PUPPET_VAR_FACT       // $facts['name'] - system facts
} puppet_var_scope_t;

// Enhanced variable lookup
puppet_value_t *puppet_variable_lookup_enhanced(
    puppet_env_t *env,
    const char *name,
    puppet_var_scope_t scope_hint
);
```

#### B. Variable Assignment Statements
```c
// Add PUPPET_STMT_ASSIGNMENT to AST
typedef struct {
    puppet_string_t variable;  // Variable name
    puppet_expr_t *value;      // Value expression
    puppet_var_scope_t scope;  // Scope specification
} puppet_assignment_t;
```

#### C. String Interpolation
```c
// Support ${var} in strings
typedef struct {
    char **parts;             // String literal parts
    puppet_expr_t **exprs;    // Variable expressions
    size_t part_count;
} puppet_interpolated_string_t;
```

### Step 2: Class Instantiation Syntax

#### A. Grammar Extensions
```yacc
// New class instantiation syntax
class_resource:
    CLASS '{' STRING ':' attribute_list '}' {
        $$ = puppet_calloc(1, sizeof(puppet_stmt_t));
        $$->type = PUPPET_STMT_CLASS_INSTANCE;
        $$->data.class_instance.name = puppet_string_create($3);
        $$->data.class_instance.args = *$5;
    }
    ;
```

#### B. Parameter Matching
```c
// Match class arguments to parameters
typedef struct {
    puppet_string_t class_name;      // Class to instantiate
    puppet_attribute_t *args;        // Provided arguments
    size_t arg_count;
    puppet_stmt_t *resolved_class;   // Resolved class definition
} puppet_class_instance_t;
```

### Step 3: Hiera Integration Hooks

#### A. Data Provider Interface
```c
typedef struct puppet_data_provider {
    char *name;                    // Provider name (e.g., "hiera", "consul")
    
    // Core lookup function
    puppet_value_t *(*lookup)(
        const char *key,           // Variable name to look up
        puppet_env_t *env,         // Current environment/scope  
        void *provider_data        // Provider-specific data
    );
    
    // Check if key exists (optional optimization)
    bool (*has_key)(const char *key, puppet_env_t *env, void *data);
    
    // Initialize provider (load config, connect, etc.)
    int (*init)(void **provider_data, const char *config);
    
    // Cleanup provider
    void (*cleanup)(void *provider_data);
} puppet_data_provider_t;

// Register data provider for variable lookup
int puppet_register_data_provider(puppet_env_t *env, puppet_data_provider_t *provider);
```

#### B. Variable Lookup Chain
```c
puppet_value_t *puppet_variable_lookup_chain(puppet_env_t *env, const char *name) {
    puppet_value_t *value = NULL;
    
    // 1. Local scope
    value = puppet_scope_get_var(env->current_scope, name, false);
    if (value) return value;
    
    // 2. Parent scopes (class, node, global)
    value = puppet_scope_get_var(env->current_scope, name, true);
    if (value) return value;
    
    // 3. Data providers (Hiera, etc.)
    for (size_t i = 0; i < env->data_provider_count; i++) {
        value = env->data_providers[i]->lookup(name, env, env->provider_data[i]);
        if (value) return value;
    }
    
    // 4. Not found
    return NULL;
}
```

## Testing Strategy

### Unit Tests
- Variable assignment and lookup
- Parameter resolution with defaults
- Scope chain traversal
- String interpolation

### Integration Tests
```puppet
# Test file: class_instantiation.pp
class apache($port = 80, $ssl = false) {
  package { 'apache2': ensure => installed }
  service { 'apache2': 
    ensure => running,
    port   => $port,
  }
}

class { 'apache':
  port => 443,
  ssl  => true,
}

$custom_port = 8080
class { 'apache':
  port => $custom_port,
}
```

### Hiera Mock Testing
```c
// Mock Hiera provider for testing
puppet_value_t *mock_hiera_lookup(const char *key, puppet_env_t *env, void *data) {
    if (strcmp(key, "apache::port") == 0) {
        return puppet_value_create_number(443);
    }
    return NULL;
}
```

## Migration Strategy

1. **Extend Current System**: Build on existing parameter and scope infrastructure
2. **Backwards Compatibility**: Ensure existing parsing still works
3. **Incremental Testing**: Test each component independently
4. **Future-Proof Design**: Hiera hooks ready for external data integration

This design provides a solid foundation for Puppet-style class instantiation while preparing for advanced features like Hiera data lookups.