# Virtual resource examples class
# Demonstrates virtual resources (@resource) and realize()

class virtual_examples {
  # Declare virtual file resources
  # These are NOT applied until realize() is called
  @file { '/tmp/puppetc-demo/virtual_user1.txt':
    ensure  => present,
    content => "File for user1 - realized\n",
    mode    => '0644',
  }

  @file { '/tmp/puppetc-demo/virtual_user2.txt':
    ensure  => present,
    content => "File for user2 - realized\n",
    mode    => '0644',
  }

  @file { '/tmp/puppetc-demo/virtual_user3.txt':
    ensure  => present,
    content => "File for user3 - NOT realized\n",
    mode    => '0644',
  }

  # Realize specific virtual resources
  # Only user1 and user2 files will be created
  realize(File['/tmp/puppetc-demo/virtual_user1.txt'])
  realize(File['/tmp/puppetc-demo/virtual_user2.txt'])
  # Note: virtual_user3.txt is NOT realized, so it won't be created

  # Virtual notify resources
  @notify { 'virtual_notify_1':
    message => 'Virtual notification 1 - realized',
  }

  @notify { 'virtual_notify_2':
    message => 'Virtual notification 2 - NOT realized',
  }

  realize(Notify['virtual_notify_1'])

  # Marker file to show the test ran
  file { '/tmp/puppetc-demo/virtual_test_complete.txt':
    ensure  => present,
    content => "Virtual resource examples completed successfully\n",
    mode    => '0644',
  }
}
