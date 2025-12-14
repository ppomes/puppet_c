# Test duplicate resource detection

# This should work fine - different titles
file { '/etc/hosts':
  ensure => present,
}

file { '/etc/resolv.conf':
  ensure => present,
}

# Different types with same title should work
user { 'admin':
  ensure => present,
}

# This should detect a duplicate - same type and title
file { '/etc/hosts':
  ensure => absent,
  mode => '0644',
}

# Variables that evaluate to same title should also be detected
$config1 = '/etc/app.conf'
$config2 = '/etc/app.conf'

file { $config1:
  ensure => present,
}

file { $config2:
  ensure => absent,
}

# Interpolated string that creates duplicate
$hostname = 'web01'
file { "/var/log/${hostname}.log":
  ensure => present,
}

file { '/var/log/web01.log':
  ensure => present,
}