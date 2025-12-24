# Sample Puppet manifest for testing
# Edit this file on your host, changes are immediately available in the VM

file { '/tmp/puppet-managed.txt':
  ensure  => present,
  content => "Managed by puppetc!\n",
  mode    => '0644',
}

file { '/tmp/puppet-test-dir':
  ensure => directory,
  mode   => '0755',
}

notify { 'hello':
  message => 'Hello from puppetc-server!',
}

exec { 'log_run':
  command => '/bin/date >> /tmp/puppet-runs.log',
}

exec { 'create_marker':
  command => '/bin/touch /tmp/first-run-marker',
  creates => '/tmp/first-run-marker',
}
