# Base system configuration class
# Uses facts for system-aware configuration

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

  # System info file using template
  file { '/tmp/puppetc-demo/system-info.txt':
    ensure  => present,
    content => template('base_config/system_info.erb'),
    mode    => '0644',
    require => File['/tmp/puppetc-demo'],
  }
}
