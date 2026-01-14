# Ruby Provider Integration

## Overview

The agent embeds a Ruby VM to support custom types and providers from Puppet modules (e.g., `puppetlabs/mysql`). This allows using existing Puppet Forge modules without reimplementing their providers in C.

## Architecture

```
+---------------------+
|   puppet_agent_ruby.c |
+---------------------+
         |
         v
+---------------------+     +------------------------+
|     Ruby VM         |---->|  Puppet Module Stubs   |
|   (libruby)         |     |  - Puppet::Provider    |
+---------------------+     |  - Puppet::Type        |
         |                  |  - Puppet::Resource    |
         v                  |  - Facter              |
+---------------------+     +------------------------+
|  Module Providers   |
|  mysql/lib/puppet/  |
|    provider/*.rb    |
+---------------------+
```

## Puppet Module Stubs

Since we don't load the full Puppet Ruby framework, we provide minimal stubs for the APIs that providers need.

### Puppet::Provider

```ruby
class Puppet::Provider
  attr_accessor :resource

  def initialize(params = {})
    @resource = Resource.new(params)
  end

  # Define shell commands
  def self.commands(cmds)
    cmds.each do |method_name, command|
      define_singleton_method(method_name) do |*args|
        cmd = [command] + args.flatten.compact
        shell_cmd = cmd.map { |c| Shellwords.shellescape(c.to_s) }.join(' ')
        result = `#{shell_cmd} 2>&1`
        raise Puppet::Error, "Command failed" unless $?.success?
        result
      end
    end
  end

  # Provider registry
  def self.register(type_name, provider_name, klass)
    @@providers["#{type_name}:#{provider_name}"] = klass
  end

  def self.provider(type_name, provider_name)
    @@providers["#{type_name}:#{provider_name}"]
  end
end
```

### Puppet::Type

```ruby
module Puppet::Type
  @@types = {}

  def self.type(name)
    @@types[name.to_sym]
  end

  def self.newtype(name, &block)
    klass = Class.new { ... }
    klass.instance_eval(&block)
    @@types[name.to_sym] = klass
  end
end
```

### Puppet::Resource

```ruby
class Puppet::Resource
  def initialize(params)
    @params = params
  end

  def [](key)
    @params[key.to_sym] || @params[key.to_s]
  end

  def value(key)
    self[key]
  end
end
```

### Facter

```ruby
module Facter
  @@facts = {}
  @@resolvers = {}

  def self.add(name, &block)
    resolver = Util::Resolution.new(name)
    resolver.instance_eval(&block)
    @@resolvers[name.to_s] = resolver
  end

  def self.value(name)
    @@facts[name] || resolve(name)
  end
end
```

## Loading Providers

### Directory Structure

```
/var/lib/puppetc/lib/
├── facter/
│   ├── mysql_version.rb      # Custom facts
│   └── service_provider.rb
└── puppet/
    ├── type/
    │   └── mysql_database.rb  # Type definitions
    └── provider/
        └── mysql_database/
            └── mysql.rb       # Provider implementation
```

### Load Sequence

1. **Type Loading**: `require 'puppet/type/mysql_database'`
2. **Provider Loading**: `require 'puppet/provider/mysql_database/mysql'`
3. **Provider Lookup**: `Puppet::Provider.provider(:mysql_database, :mysql)`

```c
// In puppet_agent_ruby.c
static int load_ruby_type(ctx, "mysql_database");
static int load_ruby_provider(ctx, "mysql_database", "mysql");
```

## Applying Ruby Resources

### Parameter Conversion

Resource parameters are converted from C to Ruby:

```c
// Build Ruby hash from resource parameters
char params_json[8192];
// Convert resource->params to JSON
snprintf(params_json, sizeof(params_json),
    "$puppet_params = JSON.parse('%s')", escaped_json);
rb_eval_string_protect(params_json, &state);
```

### Provider Invocation

```ruby
# Generated Ruby code for resource application
provider_class = Puppet::Provider.provider(:mysql_database, :mysql)
if provider_class
  provider = provider_class.new($puppet_params)
  if !provider.exists?
    provider.create
    'changed'
  else
    'unchanged'
  end
else
  'no_provider'
end
```

### Result Handling

```c
const char *result_str = StringValueCStr(ruby_result);
if (strcmp(result_str, "changed") == 0) {
    return RUBY_APPLY_CHANGED;
} else if (strcmp(result_str, "unchanged") == 0) {
    return RUBY_APPLY_SUCCESS;
} else if (strcmp(result_str, "no_provider") == 0) {
    print_error("No Ruby provider registered for %s", type_name);
    return RUBY_APPLY_FAILED;
}
```

## Custom Facts

### Fact Collection Mode

During fact collection, `Puppet::Type.type()` returns stub objects to prevent errors in facts that reference unloaded types:

```ruby
$puppet_collecting_facts = true  # Set during fact collection

module Puppet::Type
  def self.type(name)
    result = @@types[name.to_sym]
    return result if result
    # Return stub during fact collection to prevent nil errors
    $puppet_collecting_facts ? StubType.new : nil
  end
end
```

### StubType

```ruby
class StubType
  def method_missing(method, *args, &block)
    self  # Return self for method chaining
  end

  def [](key)
    nil
  end
end
```

## Supported Provider Methods

| Method | Description |
|--------|-------------|
| `exists?` | Check if resource exists |
| `create` | Create the resource |
| `destroy` | Remove the resource |
| `flush` | Apply pending changes |
| Property getters | Read current state |
| Property setters | Modify state |

## Error Handling

Ruby exceptions are caught and reported:

```c
VALUE ruby_result = rb_eval_string_protect(ruby_code, &state);
if (state != 0) {
    VALUE exception = rb_errinfo();
    VALUE message = rb_obj_as_string(exception);
    print_error("Ruby exception: %s", StringValueCStr(message));
    rb_set_errinfo(Qnil);  // Clear exception
}
```

## Thread Safety

The Ruby VM is protected by a mutex since Ruby is not thread-safe:

```c
static pthread_mutex_t agent_ruby_mutex = PTHREAD_MUTEX_INITIALIZER;

ruby_apply_result_t apply_ruby_provider(...) {
    pthread_mutex_lock(&agent_ruby_mutex);
    // ... Ruby operations ...
    pthread_mutex_unlock(&agent_ruby_mutex);
}
```

## Limitations

1. **Limited Puppet API**: Only essential stubs implemented
2. **No catalog functions**: `create_resources()` in Ruby not supported
3. **Single provider**: Only one provider per type supported

## Example: MySQL Module

The `puppetlabs/mysql` module works with these resources:

| Resource | Provider | Status |
|----------|----------|--------|
| mysql_database | mysql | Working |
| mysql_user | mysql | Working |
| mysql_grant | mysql | Working |
| mysql_datadir | mysql | Working |
| mysql_plugin | mysql | Working |

### Usage

```puppet
mysql_database { 'myapp':
  ensure  => present,
  charset => 'utf8mb4',
}

mysql_user { 'appuser@localhost':
  ensure        => present,
  password_hash => '*ABC123...',
}
```

## See Also

- [AGENT_ARCHITECTURE.md](AGENT_ARCHITECTURE.md) - Agent overview
- [ERB_ARCHITECTURE.md](ERB_ARCHITECTURE.md) - Ruby VM embedding patterns
