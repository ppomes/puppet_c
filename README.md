# Puppet Language Parser in C

This is a C implementation of the Puppet configuration language parser using flex and bison, designed to replace the Haskell implementation.

## Status

This implementation is feature-complete for core Puppet language parsing:

- ✅ Complete lexer (flex) for Puppet tokens
- ✅ Full parser grammar (bison) supporting all major Puppet syntax
- ✅ String interpolation and complex expressions
- ✅ Resource references, trailing commas, keyword attributes
- ✅ Define statements with parameter lists and defaults
- ✅ Class definitions, includes, and inheritance
- ✅ **Class instantiation with parameters and defaults (`class { 'name': param => value }`)**
- ✅ **Enhanced variable system with lookup chain (Local → Class → Node → Global → Facts → Hiera)**
- ✅ **Facts loading from JSON files with --facts option (facter and PuppetDB formats)**
- ✅ **Comprehensive memory management with leak detection**
- ✅ AST data structures with complete coverage
- ✅ JSON serialization of parsed manifests
- ✅ Command-line interface with options
- ✅ Comprehensive test suite (25+ test files, all passing)
- ✅ Interpreter/evaluator with advanced variable scoping
- ✅ ERB template support with Ruby integration
- ✅ Template function for manifest evaluation
- ✅ **Module autoloading system**
- ✅ **Directory-based execution with site.pp support**
- ✅ **Include statement with automatic class loading**
- ✅ **Full Hiera support with YAML data backend**
- ✅ **lookup() function for hierarchical data retrieval**
- ✅ **Core Puppet functions (fail, notice, info, warning, debug, err, defined, tag, tagged)**
- ✅ **Conditional statements (if/elsif/else, unless, case)**
- ✅ **Ternary conditional expressions (`$x ? 'yes' : 'no'`)**
- ✅ **Selector expressions (`$os ? { 'linux' => 'apt', default => 'unknown' }`)**
- ⏳ Additional standard library functions (partial implementation)
- ⏳ PuppetDB integration (not yet implemented)

## Building

### Prerequisites

- Ruby 3.4+ development headers (for ERB template support)
- libyaml development headers (required for Hiera support)
- flex and bison for parser generation
- GCC or compatible C compiler

On macOS with Homebrew:
```bash
brew install ruby flex bison libyaml
```

On Ubuntu/Debian:
```bash
apt-get install ruby-dev libyaml-dev flex bison build-essential
```

### Compilation

```bash
make
```

The build system automatically detects Ruby headers and enables ERB support when available.

## Usage

### Single File Mode

Basic parsing:
```bash
./bin/puppetc manifest.pp
```

JSON output:
```bash
./bin/puppetc --json manifest.pp
./bin/puppetc -j manifest.pp -o output.json
```

Manifest evaluation with variables:
```bash
./bin/puppetc --eval manifest.pp
```

Evaluation with facts loading:
```bash
./bin/puppetc --eval --facts facts.json manifest.pp
```

Evaluation with Hiera data:
```bash
./bin/puppetc --eval --hiera-data /path/to/data manifest.pp
```

### Template Output Mode

Display the rendered ERB template content for a specific file resource:
```bash
./bin/puppetc --eval --node <node_name> --template '<resource_title>' manifest.pp
```

Example - if your manifest contains:
```puppet
node 'webserver' {
  file { '/etc/apache2/sites-available/mysite.conf':
    content => template('apache/vhost.erb'),
  }
}
```

You can view the rendered template with:
```bash
./bin/puppetc --eval --node webserver --template '/etc/apache2/sites-available/mysite.conf' site.pp
```

This will evaluate the manifest and output the rendered ERB template content for that file resource.

### Directory Mode (Module Support)

Parse a Puppet directory structure:
```bash
./bin/puppetc /path/to/puppet/code
```

This will:
1. Load `manifests/site.pp` as the main manifest
2. Auto-load classes from `modules/` when they are included
3. Resolve class names like `apache::vhost` to `modules/apache/manifests/vhost.pp`

Evaluate with module loading:
```bash
./bin/puppetc --eval /path/to/puppet/code
```

With facts and modules:
```bash
./bin/puppetc --eval --facts facts.json /path/to/puppet/code
```

Custom modules path:
```bash
./bin/puppetc --modules /custom/modules/path --eval /path/to/code
```

### Module Structure Example

Standard Puppet directory layout:
```
puppet_code/
├── manifests/
│   └── site.pp          # Main entry point
└── modules/
    ├── apache/
    │   └── manifests/
    │       ├── init.pp   # Defines 'apache' class
    │       └── vhost.pp  # Defines 'apache::vhost' class
    └── mysql/
        └── manifests/
            └── init.pp   # Defines 'mysql' class
```

Example site.pp with includes:
```puppet
# manifests/site.pp
$environment = "production"

include apache
include apache::vhost
include mysql
```

Help:
```bash
./bin/puppetc --help
```

## Testing

The parser includes a comprehensive test suite with 25+ test files covering all major Puppet language features:

```bash
make test
```

### Variable System Tests

Enhanced variable system with dedicated test runner:

```bash
./run_variable_tests.sh
```

This runs specialized tests for:
- Variable scoping and lookup chain
- Class instantiation with parameters
- Default parameter handling
- Variable arithmetic and expressions
- Edge cases and error handling

### Core Language Tests

Standard language tests in `tests/puppet/` directory:

```bash
make test
```

Covers:
- Basic resources and classes
- Complex Apache module configurations  
- String interpolation and expressions
- ERB template processing
- Define statements with parameters
- Resource references and collections
- Class instantiation syntax
- Advanced variable resolution

For unit tests:
```bash
make test-unit
```

## Architecture

The implementation follows a traditional compiler architecture:

1. **Lexer** (`puppet.l`) - Tokenizes Puppet source code
2. **Parser** (`puppet.y`) - Builds an AST from tokens
3. **AST** (`puppet_ast.h/c`) - Represents the program structure
4. **Interpreter** (`puppet_interpreter.h/c`) - Evaluates the AST with variable scoping and facts integration
5. **Module Loader** (`puppet_loader.h/c`) - Handles module autoloading and class resolution
6. **Facts System** (`puppet_json_parser.h/c`) - JSON facts loading from facter and PuppetDB formats
7. **ERB Integration** (`puppet_erb.h/c`) - Ruby template processing
8. **Hiera Integration** (`puppet_hiera.h/c`) - Hierarchical data lookup with YAML support
9. **Standard Library** (`puppet_stdlib.h/c`) - Core Puppet functions including lookup()
10. **Runtime** - Provides built-in functions and resource management

### Module Loading System

The parser implements Puppet's standard module autoloading conventions:

- **Class Name Resolution**: Maps `class::subclass` to `modules/class/manifests/subclass.pp`
- **Include Support**: `include` statements automatically load classes from modules
- **Directory Mode**: Pass a directory to load `manifests/site.pp` and modules
- **Caching**: Loaded classes are cached to prevent duplicate loading
- **Backward Compatible**: Single file mode continues to work as before

### ERB Template Support

The parser includes comprehensive ERB template support through Ruby C API integration:

- **Ruby VM Embedding**: Initializes Ruby interpreter for full ERB functionality
- **Variable Interpolation**: Supports full ERB syntax including `<%= @variable %>` and conditionals
- **Puppet Integration**: Variables and facts automatically exported to ERB context
- **Error Handling**: Robust Ruby VM initialization with stdin blocking prevention

See `docs/ERB_ARCHITECTURE.md` for detailed technical documentation.

### Hiera Data Lookup Support

The parser includes full Hiera support for hierarchical data management:

- **YAML Backend**: Complete YAML parsing for all data types (strings, numbers, booleans, arrays, hashes)
- **lookup() Function**: Standard Puppet lookup function with default value support
- **Data Provider Architecture**: Extensible system for multiple data sources
- **Command-line Integration**: `--hiera-data` option to specify data directory
- **Automatic Registration**: Hiera provider automatically registered with environment
- **Caching**: Lookup results cached for performance

Example usage:
```puppet
$db_password = lookup('database::password', 'default_password')
$config = lookup('myapp::config')
```

## Key Differences from Haskell Implementation

- Uses manual memory management instead of garbage collection
- Imperative style vs functional programming
- Direct parser generation with flex/bison vs parser combinators
- Mutable data structures vs immutable ones

## Recent Improvements

This parser has been significantly enhanced and now successfully parses complex Puppet manifests:

### Core Language Features
- **Class Instantiation**: Full `class { 'name': param => value }` syntax with parameter matching
- **Enhanced Variable System**: Complete lookup chain supporting Local → Class → Node → Global → Facts → Data Provider scopes
- **Facts Loading**: JSON facts support for both facter and PuppetDB formats with `--facts` command-line option
- **Default Parameters**: Support for default values in class and define parameters
- **Advanced Memory Management**: Comprehensive tracking and leak detection throughout the codebase

### Module and Loading System
- **Module Autoloading**: Full support for Puppet's standard directory structure with automatic class loading
- **Directory Mode**: Can now process entire Puppet code directories, not just single files
- **Include Statement**: Functional `include` statements that load classes from modules on demand
- **Complete Apache Module Support**: Successfully parses 154-line Apache configuration with all advanced features

### Infrastructure and Quality
- **Enhanced Parser Grammar**: Fixed parsing of trailing commas, string interpolation, and resource references
- **Robust Test Suite**: 25+ test cases pass, including specialized variable system tests
- **Data Provider Interface**: Infrastructure ready for Hiera integration and external data sources
- **Proper Git Hygiene**: Binary files removed from tracking, comprehensive .gitignore
- **Organized Project Structure**: Tests properly organized with dedicated test runners

## Known Limitations

### Class Instantiation with Single Quotes

Due to GLR parser ambiguity between class instantiation and resource declaration syntax, class instantiation using single-quoted class names may cause parse errors:

```puppet
# This may cause "syntax is ambiguous" error:
class { 'apache': port => 80, }

# Use double quotes instead:
class { "apache": port => 80, }
```

This limitation only affects the `class { 'name': }` syntax. Class definitions, includes, and other features work normally with both quote styles.

## Implemented Stdlib Functions

### Core Functions
- **Logging**: `notice`, `info`, `warning`, `debug`, `err`, `fail`
- **Type Checking**: `defined`, `tag`, `tagged`, `realize`

### String Functions
- `capitalize`, `chomp`, `chop`, `downcase`, `upcase`
- `lstrip`, `rstrip`, `strip`
- `split`, `join`
- `match`, `regsubst`

### Array Functions
- `concat`, `delete`, `delete_at`, `first`, `last`
- `flatten`, `reverse`, `sort`, `unique`
- `empty`, `length`, `size`, `member`
- `range`

### Hash Functions
- `keys`, `values`, `has_key`, `merge`, `delete`

### Numeric Functions
- `abs`, `ceil`, `ceiling`, `floor`, `round`, `sqrt`
- `max`, `min`

### Type Inspection
- `is_array`, `is_bool`, `is_boolean`, `is_float`
- `is_hash`, `is_integer`, `is_numeric`, `is_string`

### Path Functions
- `basename`, `dirname`, `extname`

### Crypto Functions
- `sha1`, `md5`, `base64`

### Data Functions
- `lookup` (Hiera integration)

## Next Steps

The remaining work includes:

1. Expanding standard library functions beyond current template support
2. Adding resource catalog generation and validation  
3. Integrating with external services (PuppetDB, Hiera)
4. Performance optimization for large manifests
5. Advanced Ruby gem support for extended ERB functionality

## Examples

### Facts Loading and Usage

Create a facts file (`facts.json`):
```json
{
  "hostname": "web01",
  "operatingsystem": "Ubuntu",
  "operatingsystemrelease": "20.04",
  "architecture": "x86_64",
  "environment": "production"
}
```

Puppet manifest using facts (`manifest.pp`):
```puppet
# Facts are automatically available as variables
if $operatingsystem == "Ubuntu" {
  $package_name = "apache2"
} else {
  $package_name = "httpd"
}

$deployment_info = "Deploying to ${hostname} running ${operatingsystem} ${operatingsystemrelease}"
```

Run with facts:
```bash
./bin/puppetc --eval --facts facts.json manifest.pp
```

### Basic Puppet Class

```puppet
class apache($port = 80, $ssl = false, $workers = 2) {
  package { 'httpd':
    ensure => installed,
  }
  
  service { 'httpd':
    ensure  => running,
    require => Package['httpd'],
  }
  
  # Use class parameters
  file { '/etc/httpd/conf.d/port.conf':
    content => "Listen ${port}\n",
  }
}

# Class instantiation with parameters
class { 'apache':
  port    => 8080,
  ssl     => true,
  workers => 4,
}
```

### ERB Template Usage

Puppet manifest (`template_example.pp`):
```puppet
$server_name = "web01.example.com"
$port = 8080
$debug_enabled = true

$config_content = template("config/apache.erb")
```

Template file (`config/apache.erb`):
```erb
ServerName: <%= @server_name %>
Port: <%= @port %>
Debug: <%= @debug_enabled %>
```

When evaluated with `./bin/puppetc --eval template_example.pp`, the template function interpolates variables and produces:
```
ServerName: web01.example.com
Port: 8080
Debug: true
```

The parser can successfully parse this syntax and output structured JSON:

```json
{
  "type": "program", 
  "statements": [
    {
      "type": "class_definition",
      "name": "apache",
      "parameters": [],
      "body": [
        {
          "type": "resource",
          "resource_type": "package",
          "style": "normal",
          "instances": [...]
        }
      ]
    }
  ]
}
```

This JSON output can be consumed by other tools, used for analysis, or serve as input for code generation.