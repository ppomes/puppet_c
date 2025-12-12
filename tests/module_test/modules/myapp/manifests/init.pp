class myapp {
  package { 'myapp':
    ensure => installed,
  }
  
  service { 'myapp':
    ensure  => running,
    require => Package['myapp'],
  }
}