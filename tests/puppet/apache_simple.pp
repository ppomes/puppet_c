# Simplified Apache configuration for parser testing

# Global variables
$web_port = 80
$admin_email = "admin@example.com"
$doc_root = "/var/www/html"

# Apache class definition
class apache {
  # Install Apache package
  package { 'apache2':
    ensure => installed
  }

  # Configure Apache service
  service { 'apache2':
    ensure => running,
    enable => true,
    require => Package['apache2']
  }

  # Main config file
  file { '/etc/apache2/apache2.conf':
    ensure => file,
    content => template("tests/puppet/apache2.conf.erb"),
    owner => 'root',
    group => 'root',
    mode => '0644',
    require => Package['apache2'],
    notify => Service['apache2']
  }
}

# Simple virtual host definition  
define vhost($docroot, $port) {
  file { $docroot:
    ensure => directory,
    owner => 'www-data',
    group => 'www-data',
    mode => '0755'
  }

  $config = template("tests/puppet/apache_vhost.erb")
  
  file { "/etc/apache2/sites-available/${name}.conf":
    ensure => file,
    content => $config,
    owner => 'root',
    group => 'root',
    mode => '0644'
  }
}

# Node configuration
node 'web01' {
  include apache
  
  vhost { 'example':
    docroot => '/var/www/example',
    port => $web_port
  }
}

node default {
  include apache
  
  vhost { 'default':
    docroot => $doc_root,
    port => $web_port
  }
}