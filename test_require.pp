file { '/test':
  require => Package['apache2']
}