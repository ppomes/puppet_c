# All metaparam refs point to existing resources/classes.
class svc {
  notify { "svc-active": message => "ok" }
}

node default {
  include svc
  file { '/tmp/source':
    ensure => present,
  }
  exec { 'subscriber':
    command => '/bin/true',
    subscribe => [File['/tmp/source']],
    require   => Class['svc'],
  }
}
