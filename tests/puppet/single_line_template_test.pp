# Single line template test

node 'web01' {
  $service_name = 'apache2'
  
  file { '/etc/apache2/service.conf':
    ensure  => present,
    content => "Service: ${service_name} on ${hostname} running ${operatingsystem}",
    mode    => '0644',
  }
}