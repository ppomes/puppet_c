# site.pp - Main Puppet manifest for Apache web server setup
# This demonstrates a realistic configuration management scenario

# Global variables
$web_server_port = 80
$ssl_port = 443
$admin_email = "admin@example.com"
$document_root = "/var/www/html"

# Define a custom type for Apache virtual hosts
define apache_vhost(
  $port,
  $docroot,
  $server_name,
  $server_alias,
  $ssl,
  $ssl_cert,
  $ssl_key
) {
  # Create document root directory
  file { $docroot:
    ensure => directory,
    owner  => 'www-data',
    group  => 'www-data',
    mode   => '0755'
  }

  # Generate virtual host configuration
  $vhost_config = template("tests/puppet/apache_vhost.erb")
  
  file { "/etc/apache2/sites-available/${name}.conf":
    ensure  => file,
    content => $vhost_config,
    owner   => 'root',
    group   => 'root',
    mode    => '0644',
    require => Package['apache2'],
    notify  => Service['apache2'],
  }

  # Enable the site
  exec { "a2ensite ${name}":
    command => "/usr/sbin/a2ensite ${name}",
    creates => "/etc/apache2/sites-enabled/${name}.conf",
    require => File["/etc/apache2/sites-available/${name}.conf"],
    notify  => Service['apache2'],
  }
}

# Apache module class
class apache {
  # Install Apache package
  package { 'apache2':
    ensure => installed,
  }

  # Configure main Apache service
  service { 'apache2':
    ensure     => running,
    enable     => true,
    hasrestart => true,
    hasstatus  => true,
    require    => Package['apache2'],
  }

  # Main Apache configuration
  file { '/etc/apache2/apache2.conf':
    ensure  => file,
    content => template("tests/puppet/apache2.conf.erb"),
    owner   => 'root',
    group   => 'root',
    mode    => '0644',
    require => Package['apache2'],
    notify  => Service['apache2'],
  }

  # Enable required modules
  exec { 'a2enmod rewrite':
    command => '/usr/sbin/a2enmod rewrite',
    creates => '/etc/apache2/mods-enabled/rewrite.load',
    require => Package['apache2'],
    notify  => Service['apache2'],
  }

  if $ssl_port == 443 {
    exec { 'a2enmod ssl':
      command => '/usr/sbin/a2enmod ssl',
      creates => '/etc/apache2/mods-enabled/ssl.load',
      require => Package['apache2'],
      notify  => Service['apache2'],
    }
  }

  # Remove default site
  exec { 'a2dissite 000-default':
    command => '/usr/sbin/a2dissite 000-default',
    onlyif  => '/usr/bin/test -L /etc/apache2/sites-enabled/000-default.conf',
    require => Package['apache2'],
    notify  => Service['apache2'],
  }
}

# Node-specific configurations
node 'web01.example.com' {
  include apache

  # Production website
  apache_vhost { 'example.com':
    port        => $web_server_port,
    docroot     => '/var/www/example.com',
    server_name => 'example.com',
    server_alias => ['www.example.com'],
  }

  # SSL version
  apache_vhost { 'example.com-ssl':
    port        => $ssl_port,
    docroot     => '/var/www/example.com',
    server_name => 'example.com',
    server_alias => ['www.example.com'],
    ssl         => true,
    ssl_cert    => '/etc/ssl/certs/example.com.crt',
    ssl_key     => '/etc/ssl/private/example.com.key',
  }
}

node 'web02.example.com' {
  include apache

  # Development website
  apache_vhost { 'dev.example.com':
    port        => $web_server_port,
    docroot     => '/var/www/dev',
    server_name => 'dev.example.com',
  }

  # Testing environment
  apache_vhost { 'test.example.com':
    port        => 8080,
    docroot     => '/var/www/test',
    server_name => 'test.example.com',
  }
}

# Default node configuration
node default {
  include apache

  # Simple default vhost
  apache_vhost { 'default':
    port        => $web_server_port,
    docroot     => $document_root,
    server_name => 'localhost',
  }
}