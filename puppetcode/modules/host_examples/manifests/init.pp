# Host resource examples
class host_examples {
  # Add a host entry
  host { 'myserver':
    ensure       => present,
    ip           => '192.168.1.100',
    host_aliases => 'myserver.local',
  }

  # Add database server entry
  host { 'dbserver':
    ensure => present,
    ip     => '192.168.1.50',
  }

  # Add cache server with multiple aliases
  host { 'cache':
    ensure       => present,
    ip           => '192.168.1.60',
    host_aliases => 'redis memcached',
  }
}
