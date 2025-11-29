# Main site.pp manifest
# Demonstrates modular Apache configuration

# Global variables
$server_name = "web01.example.com"  
$listen_port = 80
$ssl_port = 443
$admin_email = "webmaster@example.com"
$document_root = "/var/www/html"

# Node configurations
node 'web01.example.com' {
  # Include the Apache module
  include apache
  # This would normally use apache::vhost syntax
  # For now we demonstrate with direct file resources
  
  file { '/var/www/example.com':
    ensure => directory,
    owner  => 'www-data',
    group  => 'www-data',
    mode   => '0755'
  }
  
  $vhost_content = template("modules/apache/templates/vhost.erb")
  
  file { '/etc/apache2/sites-available/example.com.conf':
    ensure  => file,
    content => $vhost_content,
    owner   => 'root',
    group   => 'root',
    mode    => '0644',
    require => Package['apache2'],
    notify  => Service['apache2']
  }
}

node default {
  # Default node just includes base Apache
  include apache
}