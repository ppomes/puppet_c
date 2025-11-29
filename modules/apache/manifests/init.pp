# apache/manifests/init.pp
# Main Apache class

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
    content => template("apache/apache2.conf.erb"),
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

  # Remove default site
  exec { 'a2dissite 000-default':
    command => '/usr/sbin/a2dissite 000-default',
    onlyif  => '/usr/bin/test -L /etc/apache2/sites-enabled/000-default.conf',
    require => Package['apache2'],
    notify  => Service['apache2'],
  }
}