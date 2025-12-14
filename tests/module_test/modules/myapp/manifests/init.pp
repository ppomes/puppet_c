class myapp {
  package { 'myapp':
    ensure => installed,
  }
  
  # Configuration file using ERB template
  file { 'myapp-config':
    path    => '/etc/myapp/config.conf',
    ensure  => present,
    content => template('tests/module_test/modules/myapp/templates/config.conf.erb'),
    owner   => 'root',
    group   => 'root',
    mode    => '0644',
    require => Package['myapp'],
  }
  
  service { 'myapp':
    ensure  => running,
    require => [Package['myapp'], File['myapp-config']],
  }
}