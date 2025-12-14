# Test resource titles with variables and interpolation

# Variable as resource title
$config_file = '/etc/myapp.conf'
file { $config_file:
  ensure => present,
  content => 'test content',
}

# Interpolated string as resource title
$app_name = 'nginx'
$env = 'production'
file { "/etc/${app_name}/${env}.conf":
  ensure => present,
  owner => 'root',
}

# Single quoted literal (no interpolation)
file { '/var/log/app.log':
  ensure => present,
  mode => '0644',
}

# Multiple resources with interpolated titles
$users = ['alice', 'bob', 'charlie']
$base_dir = '/home'

# This would require loop support, but let's test individual cases
$user1 = 'alice'
user { $user1:
  ensure => present,
  home => "/home/${user1}",
}

# Package with variable name
$package_name = 'vim'
package { $package_name:
  ensure => installed,
}

# Service with interpolated name
$service_prefix = 'apache2'
service { "${service_prefix}-worker":
  ensure => running,
  enable => true,
}