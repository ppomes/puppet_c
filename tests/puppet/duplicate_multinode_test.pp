# Test duplicate detection across multiple nodes

node 'web01' {
  file { '/etc/hosts':
    ensure => present,
  }
  
  # This should be a duplicate within the same node
  file { '/etc/hosts':
    ensure => absent,
  }
  
  file { $facts['hostname']:
    ensure => present,
  }
}

node 'web02' {
  # This should NOT be a duplicate - different node catalog
  file { '/etc/hosts':
    ensure => present,
  }
  
  file { $facts['hostname']:
    ensure => present,
  }
}

node 'db01' {
  # Also should NOT be a duplicate
  file { '/etc/hosts':
    ensure => present,
  }
}