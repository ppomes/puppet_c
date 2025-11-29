# Puppet Language Parser in C

This is a C implementation of the Puppet configuration language parser using flex and bison, designed to replace the Haskell implementation.

## Status

This is a work-in-progress implementation that currently includes:

- ✅ Complete lexer (flex) for Puppet tokens
- ✅ Parser grammar (bison) for Puppet syntax
- ✅ AST data structures
- ✅ JSON serialization of parsed manifests
- ✅ Command-line interface with options
- ✅ Basic build system
- ✅ Interpreter/evaluator with variable scoping
- ✅ ERB template support with Ruby integration
- ✅ Template function for manifest evaluation
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

Template processing example:
```bash
./bin/puppetc --eval tests/puppet/erb_test.pp
```

Help:
```bash
./bin/puppetc --help
```

## Testing

```bash
make test
```

## Architecture

The implementation follows a traditional compiler architecture:

1. **Lexer** (`puppet.l`) - Tokenizes Puppet source code
2. **Parser** (`puppet.y`) - Builds an AST from tokens
3. **AST** (`puppet_ast.h/c`) - Represents the program structure
4. **Interpreter** (`puppet_interpreter.h/c`) - Evaluates the AST with variable scoping
5. **ERB Integration** (`puppet_erb.h/c`) - Ruby template processing with fallback
6. **Runtime** - Provides built-in functions and resource management

### ERB Template Support

The parser includes comprehensive ERB template support through Ruby C API integration:

- **Ruby VM Embedding**: Initializes Ruby interpreter for full ERB functionality
- **Fallback Renderer**: Custom template parser when Ruby ERB library fails
- **Variable Interpolation**: Supports `<%= $variable %>` syntax in templates
- **Error Handling**: Graceful degradation when Ruby initialization issues occur

See `docs/ERB_ARCHITECTURE.md` for detailed technical documentation.

## Key Differences from Haskell Implementation

- Uses manual memory management instead of garbage collection
- Imperative style vs functional programming
- Direct parser generation with flex/bison vs parser combinators
- Mutable data structures vs immutable ones

## Next Steps

The remaining work includes:

1. Implementing the interpreter to evaluate the AST
2. Creating the runtime environment with variable scopes
3. Implementing all built-in Puppet functions
4. Adding resource catalog generation
5. Integrating with external services (PuppetDB, Hiera)
6. Extensive testing and validation

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
ServerName: <%= $server_name %>
Port: <%= $port %>
Debug: <%= $debug_enabled %>
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