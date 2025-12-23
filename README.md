# Puppet-C

A C implementation of a Puppet language parser and interpreter using flex/bison.

**Note**: This is an experimental project. It implements a subset of the Puppet language and is not a replacement for the real Puppet.

## Building

Prerequisites:
- GCC, flex, bison
- Ruby development headers (for ERB templates)
- libyaml (for Hiera)

```bash
autoreconf -i
./configure
make
make check
```

## Usage

```bash
# Parse only
./src/puppetc manifest.pp

# Parse and evaluate
./src/puppetc -e manifest.pp

# With facts
./src/puppetc -e -f facts.json manifest.pp

# JSON AST output
./src/puppetc -j manifest.pp
```

Run `./src/puppetc --help` for all options.

## What Works

- Basic parsing of classes, resources, nodes, defines
- Conditionals: if/elsif/else, unless, case, ternary, selector
- Variable scoping and interpolation
- ERB templates (via embedded Ruby)
- Hiera lookups (YAML backend)
- Module autoloading
- Many stdlib functions (see below)

## Known Limitations

- Class instantiation with single quotes may fail due to parser ambiguity:
  ```puppet
  # Use double quotes:
  class { "apache": }
  ```
- No resource catalog generation
- No PuppetDB support
- Incomplete stdlib coverage
- Many edge cases not handled

## Implemented Functions

**Logging**: notice, info, warning, debug, err, fail

**Strings**: split, join, chomp, strip, upcase, downcase, capitalize, match, regsubst

**Arrays**: concat, flatten, unique, sort, reverse, first, last, length, member, range

**Hashes**: keys, values, has_key, merge

**Numeric**: abs, floor, ceil, round, sqrt, min, max

**Types**: is_string, is_array, is_hash, is_numeric, is_bool, defined

**Path**: basename, dirname, extname

**Crypto**: sha1, md5, base64

**Data**: lookup
