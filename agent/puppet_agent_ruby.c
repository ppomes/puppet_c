/**
 * @file puppet_agent_ruby.c
 * @brief Ruby VM embedding for puppetc-agent
 *
 * Provides Ruby support for custom types, providers, and facts.
 * Based on compiler/puppet_erb.c Ruby embedding patterns.
 *
 * NOTE: ruby.h must be included BEFORE other headers due to macro conflicts
 */

#include <ruby.h>
#include <ruby/encoding.h>
#include <ruby/version.h>

#include "puppet_agent_ruby.h"
#include "puppet_memory.h"
#include "color_output.h"
#include "facter.h"
#include "puppet_provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>

/* Global Ruby context (Ruby can only be initialized once per process) */
static agent_ruby_context_t *global_agent_ruby_ctx = NULL;

/* Global mutex for Ruby VM access (Ruby is not thread-safe) */
static pthread_mutex_t agent_ruby_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ============================================================================
 * Ruby VM Initialization
 * ============================================================================ */

agent_ruby_context_t *agent_ruby_init(const char *libdir) {
    pthread_mutex_lock(&agent_ruby_mutex);

    /* Return existing context if already initialized */
    if (global_agent_ruby_ctx && global_agent_ruby_ctx->initialized) {
        pthread_mutex_unlock(&agent_ruby_mutex);
        return global_agent_ruby_ctx;
    }

    agent_ruby_context_t *ctx = puppet_calloc(1, sizeof(agent_ruby_context_t));
    if (!ctx) {
        pthread_mutex_unlock(&agent_ruby_mutex);
        return NULL;
    }

    ctx->libdir = libdir ? puppet_strdup(libdir) : puppet_strdup("/var/lib/puppetc/lib");

    /* Initialize Ruby VM */
    {
        int argc = 1;
        char *argv[] = {"puppetc-agent", NULL};
        char **ruby_argv = argv;

        ruby_sysinit(&argc, &ruby_argv);
        ruby_init();
        ruby_init_loadpath();

        /* Suppress "already initialized constant" warnings */
        rb_eval_string("$VERBOSE = nil");

        /* Use ruby_options() with -e "nil" to avoid stdin blocking */
        char *ruby_specific_argv[] = {"puppetc-agent", "-e", "nil", NULL};
        void *node = ruby_options(3, ruby_specific_argv);
        if (!node) {
            print_warning("Ruby options processing failed");
        }
    }

    /* Define comprehensive Puppet module namespace for types and providers */
    int state = 0;
    (void)rb_eval_string_protect(
        "# Puppet translation function stub (_)\n"
        "# In real Puppet this handles i18n, here we just return the string\n"
        "def _(msg)\n"
        "  msg\n"
        "end\n"
        "\n"
        "module Puppet\n"
        "  class Error < StandardError; end\n"
        "  class ParseError < Error; end\n"
        "  class DevError < Error; end\n"
        "  \n"
        "  # Puppet settings accessor (Puppet[:setting])\n"
        "  @settings = {\n"
        "    vardir: '/var/lib/puppet',\n"
        "    environmentpath: '/etc/puppet/environments',\n"
        "    server: ENV['PUPPET_SERVER'] || 'puppet',\n"
        "    confdir: '/etc/puppet',\n"
        "    rundir: '/var/run/puppet',\n"
        "    logdir: '/var/log/puppet',\n"
        "  }\n"
        "  def self.[](key)\n"
        "    @settings[key.to_sym]\n"
        "  end\n"
        "  def self.[]=(key, value)\n"
        "    @settings[key.to_sym] = value\n"
        "  end\n"
        "  \n"
        "  # Puppet::Pops for Sensitive type handling\n"
        "  module Pops\n"
        "    module Types\n"
        "      module PSensitiveType\n"
        "        class Sensitive\n"
        "          def initialize(value)\n"
        "            @value = value\n"
        "          end\n"
        "          def unwrap\n"
        "            @value\n"
        "          end\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "  \n"
        "  module Util\n"
        "    module Package\n"
        "      def self.versioncmp(a, b)\n"
        "        return 0 if a == b\n"
        "        return -1 if a.nil?\n"
        "        return 1 if b.nil?\n"
        "        a.to_s <=> b.to_s\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "  \n"
        "  # Resource wrapper that supports both hash access and .value method\n"
        "  class Resource\n"
        "    def initialize(params)\n"
        "      @params = params\n"
        "    end\n"
        "    def [](key)\n"
        "      @params[key.to_sym] || @params[key.to_s]\n"
        "    end\n"
        "    def []=(key, val)\n"
        "      @params[key.to_sym] = val\n"
        "    end\n"
        "    def value(key)\n"
        "      self[key]\n"
        "    end\n"
        "    def to_hash\n"
        "      @params\n"
        "    end\n"
        "    def noop?\n"
        "      false\n"
        "    end\n"
        "  end\n"
        "  \n"
        "  # Base Provider class that mysql and other providers inherit from\n"
        "  class Provider\n"
        "    @@providers = {}\n"
        "    attr_accessor :resource\n"
        "    \n"
        "    def initialize(params = {})\n"
        "      @params = params.is_a?(Resource) ? params.to_hash : params\n"
        "      @property_hash = @params.dup\n"
        "      @resource = params.is_a?(Resource) ? params : Resource.new(params)\n"
        "    end\n"
        "    \n"
        "    def self.initvars\n"
        "      @commands_cache ||= {}\n"
        "    end\n"
        "    \n"
        "    def self.commands(cmds)\n"
        "      @commands_cache ||= {}\n"
        "      cmds.each do |method_name, command|\n"
        "        @commands_cache[method_name] = command\n"
        "        define_singleton_method(method_name) do |*args|\n"
        "          cmd = [command] + args.flatten.compact\n"
        "          result = `#{cmd.map { |c| c.to_s.include?(' ') ? \"'#{c}'\" : c }.join(' ')} 2>&1`\n"
        "          raise Puppet::Error, \"Command failed: #{cmd.join(' ')}\" unless $?.success?\n"
        "          result\n"
        "        end\n"
        "        define_method(method_name) do |*args|\n"
        "          self.class.send(method_name, *args)\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "    \n"
        "    def self.optional_commands(cmds)\n"
        "      @commands_cache ||= {}\n"
        "      cmds.each do |method_name, command|\n"
        "        # Only define if command exists\n"
        "        cmd_path = `which #{command} 2>/dev/null`.strip\n"
        "        next if cmd_path.empty?\n"
        "        @commands_cache[method_name] = command\n"
        "        define_singleton_method(method_name) do |*args|\n"
        "          cmd = [command] + args.flatten.compact\n"
        "          result = `#{cmd.map { |c| c.to_s.include?(' ') ? \"'#{c}'\" : c }.join(' ')} 2>&1`\n"
        "          raise Puppet::Error, \"Command failed: #{cmd.join(' ')}\" unless $?.success?\n"
        "          result\n"
        "        end\n"
        "        define_method(method_name) do |*args|\n"
        "          self.class.send(method_name, *args)\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "    \n"
        "    def self.mk_resource_methods\n"
        "      # Creates getter/setter methods for properties - simplified stub\n"
        "    end\n"
        "    \n"
        "    def self.desc(text)\n"
        "      @description = text\n"
        "    end\n"
        "    \n"
        "    def self.instances\n"
        "      []\n"
        "    end\n"
        "    \n"
        "    def self.prefetch(resources)\n"
        "      # Default prefetch does nothing\n"
        "    end\n"
        "    \n"
        "    def exists?\n"
        "      @property_hash[:ensure] == :present\n"
        "    end\n"
        "    \n"
        "    def name\n"
        "      @params['name'] || @params[:name]\n"
        "    end\n"
        "    \n"
        "    # Logging methods\n"
        "    def debug(msg); end\n"
        "    def info(msg); $stderr.puts \"Info: #{msg}\"; end\n"
        "    def notice(msg); $stderr.puts \"Notice: #{msg}\"; end\n"
        "    def warning(msg); $stderr.puts \"Warning: #{msg}\"; end\n"
        "    def err(msg); $stderr.puts \"Error: #{msg}\"; end\n"
        "    def self.debug(msg); end\n"
        "    def self.info(msg); $stderr.puts \"Info: #{msg}\"; end\n"
        "    def self.notice(msg); $stderr.puts \"Notice: #{msg}\"; end\n"
        "    def self.warning(msg); $stderr.puts \"Warning: #{msg}\"; end\n"
        "    def self.err(msg); $stderr.puts \"Error: #{msg}\"; end\n"
        "    \n"
        "    def self.provider(type_name, provider_name)\n"
        "      key = \"#{type_name}::#{provider_name}\"\n"
        "      @@providers[key]\n"
        "    end\n"
        "    \n"
        "    def self.register(type_name, provider_name, klass)\n"
        "      key = \"#{type_name}::#{provider_name}\"\n"
        "      @@providers[key] = klass\n"
        "    end\n"
        "  end\n"
        "  \n"
        "  # Type module with newtype DSL\n"
        "  module Type\n"
        "    @@types = {}\n"
        "    \n"
        "    def self.type(name)\n"
        "      @@types[name.to_sym]\n"
        "    end\n"
        "    \n"
        "    def self.newtype(name, &block)\n"
        "      type_name = name.to_sym\n"
        "      klass = Class.new do\n"
        "        @type_name = type_name\n"
        "        @providers = {}\n"
        "        @params = {}\n"
        "        @properties = {}\n"
        "        \n"
        "        class << self\n"
        "          attr_reader :type_name, :providers, :params, :properties\n"
        "          \n"
        "          def ensurable\n"
        "            @params[:ensure] = { default: :present }\n"
        "          end\n"
        "          \n"
        "          def newparam(name, opts = {}, &block)\n"
        "            @params[name] = opts\n"
        "          end\n"
        "          \n"
        "          def newproperty(name, opts = {}, &block)\n"
        "            @properties[name] = opts\n"
        "          end\n"
        "          \n"
        "          def autorequire(type, &block)\n"
        "            # Store autorequire info\n"
        "          end\n"
        "          \n"
        "          def provide(provider_name, opts = {}, &block)\n"
        "            parent = opts[:parent] || Puppet::Provider\n"
        "            provider_class = Class.new(parent)\n"
        "            provider_class.class_eval(&block) if block\n"
        "            @providers[provider_name] = provider_class\n"
        "            Puppet::Provider.register(@type_name, provider_name, provider_class)\n"
        "            provider_class\n"
        "          end\n"
        "          \n"
        "          def desc(text)\n"
        "            @description = text\n"
        "          end\n"
        "        end\n"
        "      end\n"
        "      klass.instance_eval(&block) if block\n"
        "      @@types[type_name] = klass\n"
        "      klass\n"
        "    end\n"
        "  end\n"
        "end\n",
        &state);

    if (state != 0) {
        print_warning("Failed to define Puppet module (state=%d)", state);
    }

    /* Define minimal Facter module for custom facts */
    (void)rb_eval_string_protect(
        "module Facter\n"
        "  @@facts = { 'root_home' => ENV['HOME'] || '/root' }\n"
        "  @@resolvers = {}\n"
        "  \n"
        "  # Facter::Core for modern facts API\n"
        "  module Core\n"
        "    module Execution\n"
        "      def self.execute(cmd, opts = {})\n"
        "        `#{cmd} 2>/dev/null`.strip\n"
        "      end\n"
        "      def self.which(cmd)\n"
        "        result = `which #{cmd} 2>/dev/null`.strip\n"
        "        result.empty? ? nil : result\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "  \n"
        "  # Facter::Util for utility classes\n"
        "  module Util\n"
        "    class Resolution\n"
        "      attr_reader :name\n"
        "      def initialize(name)\n"
        "        @name = name\n"
        "        @value_block = nil\n"
        "        @weight = 0\n"
        "      end\n"
        "      def setcode(string = nil, &block)\n"
        "        if block\n"
        "          @value_block = block\n"
        "        elsif string\n"
        "          @value_block = -> { `#{string} 2>/dev/null`.strip }\n"
        "        end\n"
        "      end\n"
        "      def confine(conditions = {}); end\n"
        "      def has_weight(weight); @weight = weight; end\n"
        "      def resolve\n"
        "        @value_block ? @value_block.call : nil\n"
        "      rescue => e\n"
        "        $stderr.puts \"Facter: Error resolving #{@name}: #{e.message}\"\n"
        "        nil\n"
        "      end\n"
        "    end\n"
        "  end\n"
        "  \n"
        "  def self.add(name, options = {}, &block)\n"
        "    resolver = Util::Resolution.new(name.to_s)\n"
        "    resolver.instance_eval(&block) if block\n"
        "    @@resolvers[name.to_s] = resolver\n"
        "  end\n"
        "  \n"
        "  def self.value(name)\n"
        "    resolve(name.to_s)\n"
        "  end\n"
        "  \n"
        "  def self.[](name)\n"
        "    value(name)\n"
        "  end\n"
        "  \n"
        "  def self.resolve(name)\n"
        "    return @@facts[name] if @@facts.key?(name)\n"
        "    resolver = @@resolvers[name]\n"
        "    if resolver\n"
        "      @@facts[name] = resolver.resolve\n"
        "    end\n"
        "    @@facts[name]\n"
        "  end\n"
        "  \n"
        "  def self.all_facts\n"
        "    @@resolvers.each { |name, _| resolve(name) }\n"
        "    @@facts\n"
        "  end\n"
        "  \n"
        "  def self.clear\n"
        "    @@facts.clear\n"
        "    @@resolvers.clear\n"
        "  end\n"
        "  \n"
        "  # Legacy FactResolver for backward compatibility\n"
        "  FactResolver = Util::Resolution\n"
        "end\n",
        &state);

    if (state != 0) {
        print_warning("Failed to define Facter module (state=%d)", state);
    }

    /* Define common Puppet functions for Deferred evaluation */
    (void)rb_eval_string_protect(
        "require 'digest/sha1'\n"
        "\n"
        "# mysql::password function - hash password for MySQL\n"
        "# Handles Deferred('mysql::password', [password])\n"
        "module MysqlFunctions\n"
        "  def self.password(password, sensitive = false)\n"
        "    # Handle Sensitive wrapper\n"
        "    password = password.unwrap if password.respond_to?(:unwrap)\n"
        "    \n"
        "    # If already hashed, return as-is\n"
        "    if password.to_s =~ /^\\*[A-F0-9]{40}$/\n"
        "      return password\n"
        "    end\n"
        "    \n"
        "    # Empty password\n"
        "    return '' if password.to_s.empty?\n"
        "    \n"
        "    # MySQL native password hash: *SHA1(SHA1(password))\n"
        "    \"*#{Digest::SHA1.hexdigest(Digest::SHA1.digest(password.to_s)).upcase}\"\n"
        "  end\n"
        "end\n"
        "\n"
        "# Make mysql::password callable as a method\n"
        "def mysql_password(*args)\n"
        "  MysqlFunctions.password(*args)\n"
        "end\n",
        &state);

    if (state != 0) {
        print_warning("Failed to define Puppet functions (state=%d)", state);
    }

    /* Register stub require hooks for puppet/* paths that custom facts might need */
    (void)rb_eval_string_protect(
        "# Stub require for puppet paths that we provide internally\n"
        "$puppet_stubs_loaded = ['puppet/type', 'puppet/provider', 'puppet/util',\n"
        "                        'puppet/util/execution', 'puppet/type/service',\n"
        "                        'puppet/type/package']\n"
        "\n"
        "module Kernel\n"
        "  alias_method :original_require, :require\n"
        "  def require(name)\n"
        "    if $puppet_stubs_loaded.include?(name)\n"
        "      return false  # Already 'loaded'\n"
        "    end\n"
        "    original_require(name)\n"
        "  end\n"
        "end\n",
        &state);

    if (state != 0) {
        print_warning("Failed to define require stubs (state=%d)", state);
    }

    /* Add libdir to load path if it exists */
    if (ctx->libdir) {
        struct stat st;
        if (stat(ctx->libdir, &st) == 0 && S_ISDIR(st.st_mode)) {
            char ruby_code[512];
            snprintf(ruby_code, sizeof(ruby_code),
                     "$LOAD_PATH.unshift('%s')", ctx->libdir);
            (void)rb_eval_string_protect(ruby_code, &state);
        }
    }

    ctx->initialized = 1;
    global_agent_ruby_ctx = ctx;

    pthread_mutex_unlock(&agent_ruby_mutex);
    return ctx;
}

void agent_ruby_cleanup(agent_ruby_context_t *ctx) {
    if (!ctx) return;

    pthread_mutex_lock(&agent_ruby_mutex);

    /* ruby_cleanup(0) is intentionally skipped - causes state corruption */
    ctx->initialized = 0;

    if (ctx->libdir) {
        puppet_free(ctx->libdir);
        ctx->libdir = NULL;
    }

    if (global_agent_ruby_ctx == ctx) {
        global_agent_ruby_ctx = NULL;
    }

    puppet_free(ctx);
    pthread_mutex_unlock(&agent_ruby_mutex);
}

/* ============================================================================
 * Load Path Management
 * ============================================================================ */

int agent_ruby_add_loadpath(agent_ruby_context_t *ctx, const char *path) {
    if (!ctx || !ctx->initialized || !path) return -1;

    pthread_mutex_lock(&agent_ruby_mutex);

    char ruby_code[512];
    snprintf(ruby_code, sizeof(ruby_code),
             "$LOAD_PATH.unshift('%s') unless $LOAD_PATH.include?('%s')",
             path, path);

    int state = 0;
    (void)rb_eval_string_protect(ruby_code, &state);

    pthread_mutex_unlock(&agent_ruby_mutex);
    return state == 0 ? 0 : -1;
}

/* ============================================================================
 * Custom Facts
 * ============================================================================ */

int agent_ruby_run_custom_facts(agent_ruby_context_t *ctx, void *facts_ptr) {
    facter_ctx_t *facts = (facter_ctx_t *)facts_ptr;
    if (!ctx || !ctx->initialized || !facts) return -1;

    pthread_mutex_lock(&agent_ruby_mutex);

    int facts_added = 0;
    int state = 0;

    /* Build path to facter directory */
    char facter_path[512];
    snprintf(facter_path, sizeof(facter_path), "%s/facter", ctx->libdir);

    /* Check if facter directory exists */
    DIR *dir = opendir(facter_path);
    if (!dir) {
        pthread_mutex_unlock(&agent_ruby_mutex);
        return 0;  /* No custom facts, not an error */
    }

    /* Load all .rb files in facter directory */
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len > 3 && strcmp(entry->d_name + len - 3, ".rb") == 0) {
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", facter_path, entry->d_name);

            /* Load the fact file */
            char ruby_code[1100];
            snprintf(ruby_code, sizeof(ruby_code), "load '%s'", full_path);
            (void)rb_eval_string_protect(ruby_code, &state);

            if (state != 0) {
                print_warning("Failed to load custom fact: %s", entry->d_name);
                /* Get exception details */
                VALUE exception = rb_errinfo();
                if (!NIL_P(exception)) {
                    VALUE message = rb_obj_as_string(exception);
                    print_warning("Ruby exception: %s", StringValueCStr(message));
                }
                rb_set_errinfo(Qnil);  /* Clear the exception */
                state = 0;  /* Continue with other facts */
            }
        }
    }
    closedir(dir);

    /* Resolve all facts and add to facter context */
    VALUE all_facts = rb_eval_string_protect("Facter.all_facts", &state);
    if (state == 0 && TYPE(all_facts) == T_HASH) {
        /* Iterate over hash */
        VALUE keys = rb_funcall(all_facts, rb_intern("keys"), 0);
        long keys_len = RARRAY_LEN(keys);

        for (long i = 0; i < keys_len; i++) {
            VALUE key = rb_ary_entry(keys, i);
            VALUE val = rb_hash_aref(all_facts, key);

            const char *fact_name = StringValueCStr(key);

            /* Convert Ruby value to string and add to facter */
            if (!NIL_P(val)) {
                VALUE str_val = rb_obj_as_string(val);
                const char *fact_value = StringValueCStr(str_val);
                facter_set_string(facts, fact_name, fact_value);
                facts_added++;
            }
        }
    }

    pthread_mutex_unlock(&agent_ruby_mutex);
    return facts_added;
}

/* ============================================================================
 * Type and Provider Discovery
 * ============================================================================ */

bool agent_ruby_has_type(agent_ruby_context_t *ctx, const char *type_name) {
    if (!ctx || !ctx->initialized || !type_name) return false;

    /* Check if type file exists */
    char type_path[512];
    snprintf(type_path, sizeof(type_path), "%s/puppet/type/%s.rb",
             ctx->libdir, type_name);

    struct stat st;
    return stat(type_path, &st) == 0 && S_ISREG(st.st_mode);
}

bool agent_ruby_has_provider(agent_ruby_context_t *ctx,
                             const char *type_name,
                             const char *provider_name) {
    if (!ctx || !ctx->initialized || !type_name) return false;

    char provider_path[512];
    if (provider_name) {
        snprintf(provider_path, sizeof(provider_path),
                 "%s/puppet/provider/%s/%s.rb",
                 ctx->libdir, type_name, provider_name);
    } else {
        /* Check for default provider */
        snprintf(provider_path, sizeof(provider_path),
                 "%s/puppet/provider/%s",
                 ctx->libdir, type_name);

        /* Check if directory exists */
        struct stat st;
        if (stat(provider_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
            return false;
        }

        /* Find first .rb file in directory */
        DIR *dir = opendir(provider_path);
        if (!dir) return false;

        bool found = false;
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            size_t len = strlen(entry->d_name);
            if (len > 3 && strcmp(entry->d_name + len - 3, ".rb") == 0) {
                found = true;
                break;
            }
        }
        closedir(dir);
        return found;
    }

    struct stat st;
    return stat(provider_path, &st) == 0 && S_ISREG(st.st_mode);
}

/* ============================================================================
 * Provider Execution
 * ============================================================================ */

/**
 * Load a Ruby type if not already loaded
 */
static int load_ruby_type(agent_ruby_context_t *ctx, const char *type_name) {
    char ruby_code[1024];
    int state = 0;

    /* Check if already loaded */
    snprintf(ruby_code, sizeof(ruby_code),
             "Puppet::Type.type(:%s) != nil", type_name);
    VALUE loaded = rb_eval_string_protect(ruby_code, &state);
    if (state == 0 && RTEST(loaded)) {
        return 0;  /* Already loaded */
    }

    /* Load the type file with error capture */
    snprintf(ruby_code, sizeof(ruby_code),
             "begin\n"
             "  require 'puppet/type/%s'\n"
             "  'ok'\n"
             "rescue LoadError => e\n"
             "  $stderr.puts \"Ruby LoadError (type): #{e.message}\"\n"
             "  'load_error'\n"
             "rescue => e\n"
             "  $stderr.puts \"Ruby Error (type): #{e.class}: #{e.message}\"\n"
             "  'error'\n"
             "end",
             type_name);
    VALUE result = rb_eval_string_protect(ruby_code, &state);

    if (state != 0) {
        return -1;
    }

    /* Check if the load was successful */
    if (TYPE(result) == T_STRING) {
        const char *result_str = StringValueCStr(result);
        if (strcmp(result_str, "ok") != 0) {
            return -1;
        }
    }

    return 0;
}

/**
 * Load a Ruby provider if not already loaded
 */
static int load_ruby_provider(agent_ruby_context_t *ctx,
                              const char *type_name,
                              const char *provider_name) {
    char ruby_code[1024];
    int state = 0;

    /* Load the provider file with error capture */
    snprintf(ruby_code, sizeof(ruby_code),
             "begin\n"
             "  require 'puppet/provider/%s/%s'\n"
             "  'ok'\n"
             "rescue LoadError => e\n"
             "  $stderr.puts \"Ruby LoadError: #{e.message}\"\n"
             "  'load_error'\n"
             "rescue => e\n"
             "  $stderr.puts \"Ruby Error: #{e.class}: #{e.message}\"\n"
             "  'error'\n"
             "end",
             type_name, provider_name ? provider_name : "default");
    VALUE result = rb_eval_string_protect(ruby_code, &state);

    if (state != 0) {
        return -1;
    }

    /* Check if the load was successful */
    if (TYPE(result) == T_STRING) {
        const char *result_str = StringValueCStr(result);
        if (strcmp(result_str, "ok") != 0) {
            return -1;
        }
    }

    return 0;
}

/**
 * Find the default provider for a type
 */
static char *find_default_provider(agent_ruby_context_t *ctx, const char *type_name) {
    char provider_dir[512];
    snprintf(provider_dir, sizeof(provider_dir),
             "%s/puppet/provider/%s", ctx->libdir, type_name);

    DIR *dir = opendir(provider_dir);
    if (!dir) return NULL;

    char *provider_name = NULL;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len > 3 && strcmp(entry->d_name + len - 3, ".rb") == 0) {
            /* Use first .rb file as default provider */
            provider_name = puppet_malloc(len - 2);
            strncpy(provider_name, entry->d_name, len - 3);
            provider_name[len - 3] = '\0';
            break;
        }
    }
    closedir(dir);
    return provider_name;
}

ruby_apply_result_t agent_ruby_call_provider(agent_ruby_context_t *ctx,
                                             const char *type_name,
                                             const char *provider_name,
                                             const char *action,
                                             const void *resource_ptr,
                                             void *apply_ctx_ptr) {
    const resource_t *resource = (const resource_t *)resource_ptr;
    apply_context_t *apply_ctx = (apply_context_t *)apply_ctx_ptr;

    if (!ctx || !ctx->initialized || !type_name || !action || !resource) {
        return RUBY_APPLY_FAILED;
    }

    pthread_mutex_lock(&agent_ruby_mutex);

    int state = 0;
    ruby_apply_result_t result = RUBY_APPLY_FAILED;

    /* Find default provider if not specified */
    char *actual_provider = NULL;
    if (provider_name) {
        actual_provider = puppet_strdup(provider_name);
    } else {
        actual_provider = find_default_provider(ctx, type_name);
        if (!actual_provider) {
            print_error("No provider found for type: %s", type_name);
            pthread_mutex_unlock(&agent_ruby_mutex);
            return RUBY_APPLY_FAILED;
        }
    }

    /* Load type and provider */
    if (load_ruby_type(ctx, type_name) != 0) {
        print_error("Failed to load Ruby type: %s", type_name);
        puppet_free(actual_provider);
        pthread_mutex_unlock(&agent_ruby_mutex);
        return RUBY_APPLY_FAILED;
    }

    if (load_ruby_provider(ctx, type_name, actual_provider) != 0) {
        print_error("Failed to load Ruby provider: %s/%s", type_name, actual_provider);
        puppet_free(actual_provider);
        pthread_mutex_unlock(&agent_ruby_mutex);
        return RUBY_APPLY_FAILED;
    }

    /* Build Ruby code to instantiate and apply resource */
    /* This is a simplified implementation - real Puppet has more complex provider API */

    /* Set resource parameters as Ruby hash (using symbol keys for Puppet compatibility) */
    rb_eval_string_protect("$puppet_params = {}", &state);

    /* Add title */
    VALUE params_hash = rb_gv_get("$puppet_params");
    VALUE title_key = ID2SYM(rb_intern("name"));
    VALUE title_val = rb_str_new2(resource->title);
    rb_hash_aset(params_hash, title_key, title_val);

    /* Add other parameters using symbol keys */
    /* Skip empty string values - treat them as nil/unset */
    for (size_t i = 0; i < resource->param_count; i++) {
        const char *param_value = resource->params[i].value;
        /* Skip empty strings - they should be treated as nil */
        if (!param_value || param_value[0] == '\0') {
            continue;
        }
        VALUE key = ID2SYM(rb_intern(resource->params[i].name));
        VALUE val = rb_str_new2(param_value);
        rb_hash_aset(params_hash, key, val);
    }

    /* Execute the action */
    char ruby_code[2048];
    if (strcmp(action, "exists") == 0) {
        snprintf(ruby_code, sizeof(ruby_code),
            "begin\n"
            "  provider_class = Puppet::Provider.provider(:%s, :%s)\n"
            "  if provider_class\n"
            "    provider = provider_class.new($puppet_params)\n"
            "    provider.exists? ? 'present' : 'absent'\n"
            "  else\n"
            "    'unknown'\n"
            "  end\n"
            "rescue => e\n"
            "  $stderr.puts \"Ruby provider error: #{e.message}\"\n"
            "  'error'\n"
            "end\n",
            type_name, actual_provider);
    } else if (strcmp(action, "create") == 0 || strcmp(action, "apply") == 0) {
        if (apply_ctx && apply_ctx->noop) {
            result = RUBY_APPLY_SKIPPED;
            print_resource_noop(type_name, resource->title, "ensure", "would be applied (Ruby)");
            puppet_free(actual_provider);
            pthread_mutex_unlock(&agent_ruby_mutex);
            return result;
        }

        snprintf(ruby_code, sizeof(ruby_code),
            "begin\n"
            "  provider_class = Puppet::Provider.provider(:%s, :%s)\n"
            "  if provider_class\n"
            "    provider = provider_class.new($puppet_params)\n"
            "    if !provider.exists?\n"
            "      provider.create\n"
            "      'changed'\n"
            "    else\n"
            "      'unchanged'\n"
            "    end\n"
            "  else\n"
            "    'no_provider'\n"
            "  end\n"
            "rescue => e\n"
            "  $stderr.puts \"Ruby provider error: #{e.message}\"\n"
            "  $stderr.puts e.backtrace.first(5).join(\"\\n\")\n"
            "  'error'\n"
            "end\n",
            type_name, actual_provider);
    } else if (strcmp(action, "destroy") == 0) {
        if (apply_ctx && apply_ctx->noop) {
            result = RUBY_APPLY_SKIPPED;
            print_resource_noop(type_name, resource->title, "ensure", "would be removed (Ruby)");
            puppet_free(actual_provider);
            pthread_mutex_unlock(&agent_ruby_mutex);
            return result;
        }

        snprintf(ruby_code, sizeof(ruby_code),
            "begin\n"
            "  provider_class = Puppet::Provider.provider(:%s, :%s)\n"
            "  if provider_class\n"
            "    provider = provider_class.new($puppet_params)\n"
            "    if provider.exists?\n"
            "      provider.destroy\n"
            "      'changed'\n"
            "    else\n"
            "      'unchanged'\n"
            "    end\n"
            "  else\n"
            "    'no_provider'\n"
            "  end\n"
            "rescue => e\n"
            "  $stderr.puts \"Ruby provider error: #{e.message}\"\n"
            "  'error'\n"
            "end\n",
            type_name, actual_provider);
    } else if (strcmp(action, "refresh") == 0) {
        if (apply_ctx && apply_ctx->noop) {
            result = RUBY_APPLY_SKIPPED;
            print_resource_noop(type_name, resource->title, "refresh", "would be refreshed (Ruby)");
            puppet_free(actual_provider);
            pthread_mutex_unlock(&agent_ruby_mutex);
            return result;
        }

        snprintf(ruby_code, sizeof(ruby_code),
            "begin\n"
            "  provider_class = Puppet::Provider.provider(:%s, :%s)\n"
            "  if provider_class\n"
            "    provider = provider_class.new($puppet_params)\n"
            "    if provider.respond_to?(:refresh)\n"
            "      provider.refresh\n"
            "      'changed'\n"
            "    else\n"
            "      'no_refresh'\n"
            "    end\n"
            "  else\n"
            "    'no_provider'\n"
            "  end\n"
            "rescue => e\n"
            "  $stderr.puts \"Ruby provider error: #{e.message}\"\n"
            "  'error'\n"
            "end\n",
            type_name, actual_provider);
    } else {
        print_error("Unknown action: %s", action);
        puppet_free(actual_provider);
        pthread_mutex_unlock(&agent_ruby_mutex);
        return RUBY_APPLY_FAILED;
    }

    VALUE ruby_result = rb_eval_string_protect(ruby_code, &state);

    if (state != 0) {
        print_error("Ruby execution failed for %s[%s]", type_name, resource->title);
        VALUE exception = rb_errinfo();
        if (!NIL_P(exception)) {
            VALUE message = rb_obj_as_string(exception);
            print_error("Ruby exception: %s", StringValueCStr(message));
        }
        rb_set_errinfo(Qnil);
        result = RUBY_APPLY_FAILED;
    } else {
        const char *result_str = StringValueCStr(ruby_result);
        if (strcmp(result_str, "changed") == 0) {
            print_resource_change(type_name, resource->title, "ensure", "applied (Ruby provider)");
            result = RUBY_APPLY_CHANGED;
        } else if (strcmp(result_str, "unchanged") == 0 ||
                   strcmp(result_str, "present") == 0) {
            result = RUBY_APPLY_SUCCESS;
        } else if (strcmp(result_str, "absent") == 0) {
            result = RUBY_APPLY_SUCCESS;
        } else if (strcmp(result_str, "no_provider") == 0) {
            print_error("No Ruby provider registered for %s", type_name);
            result = RUBY_APPLY_FAILED;
        } else if (strcmp(result_str, "error") == 0) {
            result = RUBY_APPLY_FAILED;
        } else {
            result = RUBY_APPLY_SUCCESS;
        }
    }

    puppet_free(actual_provider);
    pthread_mutex_unlock(&agent_ruby_mutex);
    return result;
}

/* ============================================================================
 * Function Calls (for Deferred)
 * ============================================================================ */

char *agent_ruby_call_function(agent_ruby_context_t *ctx,
                               const char *func_name,
                               const char *args_json) {
    if (!ctx || !ctx->initialized || !func_name) return NULL;

    pthread_mutex_lock(&agent_ruby_mutex);

    int state = 0;
    char ruby_code[2048];

    /* Convert Puppet function name to Ruby method name (:: -> _) */
    char ruby_func_name[256];
    const char *src = func_name;
    char *dst = ruby_func_name;
    char *dst_end = ruby_func_name + sizeof(ruby_func_name) - 1;

    while (*src && dst < dst_end) {
        if (src[0] == ':' && src[1] == ':') {
            *dst++ = '_';
            src += 2;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';

    /* Simple function call - args handling is simplified */
    if (args_json && strlen(args_json) > 0) {
        snprintf(ruby_code, sizeof(ruby_code),
            "begin\n"
            "  require 'json'\n"
            "  args = JSON.parse('%s')\n"
            "  result = %s(*args)\n"
            "  result.to_s\n"
            "rescue => e\n"
            "  \"<Deferred:%s:error:#{e.message}>\"\n"
            "end\n",
            args_json, ruby_func_name, func_name);
    } else {
        snprintf(ruby_code, sizeof(ruby_code),
            "begin\n"
            "  result = %s()\n"
            "  result.to_s\n"
            "rescue => e\n"
            "  \"<Deferred:%s:error:#{e.message}>\"\n"
            "end\n",
            ruby_func_name, func_name);
    }

    VALUE ruby_result = rb_eval_string_protect(ruby_code, &state);

    char *result = NULL;
    if (state == 0) {
        result = puppet_strdup(StringValueCStr(ruby_result));
    } else {
        /* Return placeholder on error */
        char buf[256];
        snprintf(buf, sizeof(buf), "<Deferred:%s:failed>", func_name);
        result = puppet_strdup(buf);
    }

    pthread_mutex_unlock(&agent_ruby_mutex);
    return result;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

bool agent_ruby_available(agent_ruby_context_t *ctx) {
    return ctx && ctx->initialized;
}

const char *agent_ruby_version(void) {
    static char version[64] = {0};
    if (version[0] == '\0') {
        snprintf(version, sizeof(version), "Ruby %d.%d.%d",
                 RUBY_API_VERSION_MAJOR,
                 RUBY_API_VERSION_MINOR,
                 RUBY_API_VERSION_TEENY);
    }
    return version;
}
