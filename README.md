# Puppet Language Parser in C

This is a C implementation of the Puppet configuration language parser using flex and bison, designed to replace the Haskell implementation.

## Status

This implementation is feature-complete for core Puppet language parsing:

- ✅ Complete lexer (flex) for Puppet tokens
- ✅ Full parser grammar (bison) supporting all major Puppet syntax
- ✅ String interpolation and complex expressions
- ✅ Resource references, trailing commas, keyword attributes
- ✅ Define statements with parameter lists
- ✅ Class definitions, includes, and inheritance
- ✅ AST data structures with complete coverage
- ✅ JSON serialization of parsed manifests
- ✅ Command-line interface with options
- ✅ Comprehensive test suite (17 test files, all passing)
- ✅ Interpreter/evaluator with variable scoping
- ✅ ERB template support with Ruby integration
- ✅ Template function for manifest evaluation
- ✅ **Module autoloading system (NEW)**
- ✅ **Directory-based execution with site.pp support (NEW)**
- ✅ **Include statement with automatic class loading (NEW)**
- ⏳ Standard library functions (partial implementation)
- ⏳ PuppetDB integration (not yet implemented)
- ⏳ Hiera support (not yet implemented)

## Building

### Prerequisites

- Ruby 3.4+ development headers (for ERB template support)
- flex and bison for parser generation
- GCC or compatible C compiler

On macOS with Homebrew:
```bash
brew install ruby flex bison
```

On Ubuntu/Debian:
```bash
apt-get install ruby-dev flex bison build-essential
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

The parser includes a comprehensive test suite with 17 test files covering all major Puppet language features:

```bash
make test
```

This runs all tests and provides detailed pass/fail reporting. Tests are organized in the `tests/puppet/` directory and cover:
- Basic resources and classes
- Complex Apache module configurations  
- String interpolation and expressions
- ERB template processing
- Define statements with parameters
- Resource references and collections

For unit tests:
```bash
make test-unit
```

## Architecture

The implementation follows a traditional compiler architecture:

1. **Lexer** (`puppet.l`) - Tokenizes Puppet source code
2. **Parser** (`puppet.y`) - Builds an AST from tokens
3. **AST** (`puppet_ast.h/c`) - Represents the program structure
4. **Interpreter** (`puppet_interpreter.h/c`) - Evaluates the AST with variable scoping
5. **Module Loader** (`puppet_loader.h/c`) - Handles module autoloading and class resolution
6. **ERB Integration** (`puppet_erb.h/c`) - Ruby template processing with fallback
7. **Runtime** - Provides built-in functions and resource management

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
- **Fallback Renderer**: Custom template parser when Ruby ERB library fails
- **Variable Interpolation**: Supports `<%= @variable %>` syntax in templates
- **Error Handling**: Graceful degradation when Ruby initialization issues occur

See `docs/ERB_ARCHITECTURE.md` for detailed technical documentation.

## Key Differences from Haskell Implementation

- Uses manual memory management instead of garbage collection
- Imperative style vs functional programming
- Direct parser generation with flex/bison vs parser combinators
- Mutable data structures vs immutable ones

## Recent Improvements

This parser has been significantly enhanced and now successfully parses complex Puppet manifests:

- **Module Autoloading**: Full support for Puppet's standard directory structure with automatic class loading
- **Directory Mode**: Can now process entire Puppet code directories, not just single files
- **Include Statement**: Functional `include` statements that load classes from modules on demand
- **Complete Apache Module Support**: Successfully parses 154-line Apache configuration with all advanced features
- **Enhanced Parser Grammar**: Fixed parsing of trailing commas, string interpolation, and resource references
- **Robust Test Suite**: All 17 test cases pass, covering real-world Puppet scenarios
- **Proper Git Hygiene**: Binary files removed from tracking, comprehensive .gitignore
- **Organized Project Structure**: Tests properly organized in `tests/` directory structure

## Next Steps

The remaining work includes:

1. Expanding standard library functions beyond current template support
2. Adding resource catalog generation and validation  
3. Integrating with external services (PuppetDB, Hiera)
4. Performance optimization for large manifests
5. Enhanced ERB integration with proper Puppet scope handling

## Examples

### Basic Puppet Class

```puppet
class apache {
  package { 'httpd':
    ensure => installed,
  }
  
  service { 'httpd':
    ensure  => running,
    require => Package['httpd'],
  }
}

include apache
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