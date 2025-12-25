# Comprehensive Puppet manifest example for puppetc
# Demonstrates: variables, facts, conditionals, templates, all resource types

# ============================================================================
# Global Variables
# ============================================================================
$app_name = 'myapp'
$app_version = '1.0.0'
$admin_email = 'admin@example.com'
$debug_mode = true

# ============================================================================
# Class: base_config
# Basic system configuration using facts
# ============================================================================
class base_config {
  # Use facts for system-aware configuration
  $os_family = $facts['os']['family']
  $hostname = $facts['hostname']
  $fqdn = $facts['fqdn']
  $ip = $facts['networking']['ip']
  $memory_mb = $facts['memory']['system']['total_bytes']

  # Notify with system info
  notify { 'system_info':
    message => "Host: ${fqdn} (${ip}) - OS: ${os_family}",
  }

  # Create app directory structure
  file { '/tmp/puppetc-demo':
    ensure => directory,
    mode   => '0755',
  }

  file { '/tmp/puppetc-demo/logs':
    ensure  => directory,
    mode    => '0755',
    require => File['/tmp/puppetc-demo'],
  }

  # System info file with interpolated content
  file { '/tmp/puppetc-demo/system-info.txt':
    ensure  => present,
    content => "# System Information\nHostname: ${hostname}\nFQDN: ${fqdn}\nIP: ${ip}\nOS: ${os_family}\n",
    mode    => '0644',
    require => File['/tmp/puppetc-demo'],
  }
}

# ============================================================================
# Class: app_config
# Application configuration with conditionals
# ============================================================================
class app_config {
  # OS-specific package manager detection
  $os_family = $facts['os']['family']

  case $os_family {
    'Debian': {
      $pkg_manager = 'apt'
      $config_dir = '/etc/myapp'
    }
    'RedHat': {
      $pkg_manager = 'yum'
      $config_dir = '/etc/myapp'
    }
    default: {
      $pkg_manager = 'unknown'
      $config_dir = '/tmp/myapp-config'
    }
  }

  notify { 'pkg_info':
    message => "Using package manager: ${pkg_manager}",
  }

  # Create config directory
  file { $config_dir:
    ensure => directory,
    mode   => '0755',
  }

  # Main config file with interpolated content
  file { "${config_dir}/app.conf":
    ensure  => present,
    content => "# App Configuration\n[general]\napp_name = ${app_name}\nversion = ${app_version}\nadmin = ${admin_email}\n\n[system]\npkg_manager = ${pkg_manager}\n",
    mode    => '0644',
    require => File[$config_dir],
  }

  # Debug config only if debug_mode is enabled
  if $debug_mode {
    file { "${config_dir}/debug.conf":
      ensure  => present,
      content => "# Debug configuration\nlog_level=DEBUG\nverbose=true\n",
      mode    => '0644',
      require => File[$config_dir],
    }

    notify { 'debug_enabled':
      message => 'Debug mode is ENABLED',
    }
  }
}

# ============================================================================
# Class: exec_examples
# Exec resource examples with conditions
# ============================================================================
class exec_examples {
  # Create marker file only if it doesn't exist
  exec { 'create_marker':
    command => '/bin/touch /tmp/puppetc-demo/first-run.marker',
    creates => '/tmp/puppetc-demo/first-run.marker',
    require => File['/tmp/puppetc-demo'],
  }

  # Log current date
  exec { 'log_date':
    command => '/bin/date >> /tmp/puppetc-demo/logs/runs.log',
    require => File['/tmp/puppetc-demo/logs'],
  }

  # Conditional exec with onlyif
  exec { 'check_and_log':
    command => '/bin/echo "Directory exists" >> /tmp/puppetc-demo/logs/checks.log',
    onlyif  => '/bin/test -d /tmp/puppetc-demo',
    require => File['/tmp/puppetc-demo/logs'],
  }

  # Conditional exec - only run if file doesn't exist
  exec { 'init_if_missing':
    command => '/bin/echo "Initialized" > /tmp/puppetc-demo/initialized.txt',
    creates => '/tmp/puppetc-demo/initialized.txt',
    require => File['/tmp/puppetc-demo'],
  }
}

# ============================================================================
# Class: file_examples
# Various file resource examples
# ============================================================================
class file_examples {
  # Simple file with inline content
  file { '/tmp/puppetc-demo/hello.txt':
    ensure  => present,
    content => "Hello from puppetc!\nVersion: ${app_version}\n",
    mode    => '0644',
    require => File['/tmp/puppetc-demo'],
  }

  # File with interpolated facts
  $hostname = $facts['hostname']
  file { '/tmp/puppetc-demo/hostname.txt':
    ensure  => present,
    content => "This host is: ${hostname}\n",
    mode    => '0644',
    require => File['/tmp/puppetc-demo'],
  }

  # Symlink example
  file { '/tmp/puppetc-link':
    ensure => link,
    target => '/tmp/puppetc-demo',
  }
}

# ============================================================================
# Node definitions
# ============================================================================

# Vagrant agent node
node 'puppet-agent' {
  notify { 'welcome':
    message => "Configuring puppet-agent with puppetc - App: ${app_name} v${app_version}",
  }

  include base_config
  include app_config
  include exec_examples
  include file_examples

  notify { 'complete':
    message => 'Puppet run completed successfully!',
  }
}

# Default node - catches any unmatched node names
node default {
  notify { 'welcome':
    message => "Configuring node with puppetc - App: ${app_name} v${app_version}",
  }

  include base_config
  include app_config
  include exec_examples
  include file_examples

  notify { 'complete':
    message => 'Puppet run completed successfully!',
  }
}
