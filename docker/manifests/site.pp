# Sample site.pp for docker testing
# This manifest demonstrates basic resource types

# Create a test file
file { '/tmp/puppet-managed':
  ensure  => present,
  content => 'This file is managed by puppetc!\n',
  mode    => '0644',
}

# Create a directory
file { '/tmp/puppet-dir':
  ensure => directory,
  mode   => '0755',
}

# Notify resource for testing
notify { 'puppet-hello':
  message => 'Hello from puppetc-server!',
}

# Exec resource - creates a file only if it doesn't exist
exec { 'create_exec_marker':
  command => '/bin/touch /tmp/exec-marker',
  creates => '/tmp/exec-marker',
}

# Exec with onlyif condition
exec { 'echo_hostname':
  command => '/bin/hostname > /tmp/hostname.txt',
  onlyif  => '/bin/test -d /tmp',
}
