# Module Support in Puppet C Parser

## Current Status

The Puppet C parser currently supports:
- ✅ Basic module structure (manifests, templates, files directories)
- ✅ Module templates via `template("module/template.erb")` function
- ✅ Include statements for classes
- ✅ File and package resources
- ✅ Node definitions and inheritance
- ✅ Variable scoping and interpolation

## Limitations

The parser currently has these limitations that will be addressed in future versions:

### 1. Namespace Separator (`::`)
- **Current**: Does not parse `apache::vhost` style class/type names
- **Workaround**: Use underscore naming like `apache_vhost`
- **Future**: Add qualified name support to parser grammar

### 2. Trailing Commas
- **Current**: Parser errors on trailing commas in lists
- **Workaround**: Remove trailing commas from parameter lists and attributes
- **Future**: Make grammar more permissive for trailing commas

### 3. Default Parameters
- **Current**: Cannot parse default values in define parameters
- **Workaround**: Use only required parameters
- **Future**: Add default parameter support to grammar

### 4. Variable Interpolation in Defaults
- **Current**: Cannot use `$name` in default parameter values
- **Workaround**: Set defaults in the define body
- **Future**: Support variable references in defaults

### 5. Top-scope Variables
- **Current**: No support for `$::variable` syntax
- **Workaround**: Use regular variables
- **Future**: Add scope resolution operator support

## Module Directory Structure

Standard Puppet module layout is supported:

```
modules/
└── apache/
    ├── manifests/
    │   ├── init.pp      # Main class: class apache { }
    │   └── vhost.pp     # Defined type: define apache::vhost { }
    ├── templates/
    │   ├── apache2.conf.erb
    │   └── vhost.erb
    ├── files/
    └── lib/
```

## Using Modules

### With Current Parser

```puppet
# In site.pp
include apache

# Use templates from modules
$config = template("modules/apache/templates/vhost.erb")

# Direct resource declarations work
file { '/etc/apache2/apache2.conf':
  content => template("modules/apache/templates/apache2.conf.erb")
}
```

### Future Support (Planned)

```puppet
# Namespaced class inclusion
include apache::ssl

# Parameterized class declaration  
class { 'apache':
  port => 8080,
}

# Defined type with namespace
apache::vhost { 'example.com':
  docroot => '/var/www/example',
  ssl     => true,
}
```

## Roadmap

1. **Phase 1**: Add `::` token support in resource type names
2. **Phase 2**: Support trailing commas in all contexts
3. **Phase 3**: Default parameters with expressions
4. **Phase 4**: Full scope resolution operators
5. **Phase 5**: Parameterized class declarations

## Current Best Practices

Until full module support is implemented:

1. **Use underscores** instead of `::` in names
2. **Remove trailing commas** from all lists
3. **Set defaults in define bodies** not parameters
4. **Use full template paths** like `"modules/modname/templates/file.erb"`
5. **Test incrementally** with simple manifests first

The parser already provides powerful template processing and basic module support, making it suitable for many real-world configurations while these enhancements are implemented.