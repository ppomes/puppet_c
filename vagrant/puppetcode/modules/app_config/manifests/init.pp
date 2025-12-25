# Application configuration class
# Demonstrates conditionals and OS-specific configuration

class app_config (
  $app_name = 'myapp',
  $app_version = '1.0.0',
  $admin_email = 'admin@example.com',
  $debug_mode = true,
) {
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

  # Main config file using template
  file { "${config_dir}/app.conf":
    ensure  => present,
    content => template('app_config/app.conf.erb'),
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
