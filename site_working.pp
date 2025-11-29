# Main site.pp manifest - Working version
# Demonstrates template-based Apache configuration

# Global variables
$server_name = "web01.example.com"  
$listen_port = 80
$ssl_port = 443  
$admin_email = "webmaster@example.com"
$document_root = "/var/www/html"

# Node configurations
node 'web01.example.com' {
  # Apache package installation
  package { 'apache2':
    ensure => installed
  }
  
  # Apache service management
  service { 'apache2':
    ensure => running,
    enable => true,
    require => Package['apache2']
  }
  
  # Generate and install Apache configuration
  $apache_config = template("modules/apache/templates/apache2.conf.erb")
  
  file { '/etc/apache2/apache2.conf':
    ensure  => file,
    content => $apache_config,
    owner   => 'root',
    group   => 'root',
    mode    => '0644',
    require => Package['apache2'],
    notify  => Service['apache2']
  }
  
  # Virtual host for example.com
  file { '/var/www/example.com':
    ensure => directory,
    owner  => 'www-data',
    group  => 'www-data', 
    mode   => '0755'
  }
  
  # Generate virtual host configuration
  $vhost_config = template("modules/apache/templates/vhost.erb")
  
  file { '/etc/apache2/sites-available/example.com.conf':
    ensure  => file,
    content => $vhost_config,
    owner   => 'root', 
    group   => 'root',
    mode    => '0644',
    require => Package['apache2'],
    notify  => Service['apache2']
  }
}

node default {
  # Default node configuration
  package { 'apache2':
    ensure => installed
  }
  
  service { 'apache2':
    ensure => running,
    enable => true,
    require => Package['apache2']
  }
}