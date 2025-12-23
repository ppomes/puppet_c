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
