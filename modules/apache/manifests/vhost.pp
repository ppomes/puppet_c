# apache/manifests/vhost.pp
# Defined type for Apache virtual hosts

define apache::vhost(
  $port         = 80,
  $docroot      = "/var/www/${name}",
  $server_name  = $name,
  $server_alias = undef,
  $ssl          = false,
  $ssl_cert     = undef,
  $ssl_key      = undef,
) {
  # Create document root directory
  file { $docroot:
    ensure => directory,
    owner  => 'www-data',
    group  => 'www-data',
    mode   => '0755',
  }

  # Generate virtual host configuration
  file { "/etc/apache2/sites-available/${name}.conf":
    ensure  => file,
    content => template("apache/vhost.erb"),
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