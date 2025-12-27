# Test virtual resources and realize()
class test_virtual {
  # Virtual file resources - not applied until realized
  @file { '/tmp/virtual_realized':
    ensure  => file,
    content => 'This virtual file was realized',
  }

  @file { '/tmp/virtual_not_realized':
    ensure  => file,
    content => 'This virtual file should NOT exist',
  }

  # Realize only one of the virtual files
  realize(File['/tmp/virtual_realized'])

  # Regular file to verify normal resources still work
  file { '/tmp/virtual_test_marker':
    ensure  => file,
    content => 'Virtual resource test completed',
  }

  # Virtual notify resources
  @notify { 'virtual_notify_realized':
    message => 'Virtual notify was realized',
  }

  @notify { 'virtual_notify_not_realized':
    message => 'This should not appear',
  }

  realize(Notify['virtual_notify_realized'])
}
