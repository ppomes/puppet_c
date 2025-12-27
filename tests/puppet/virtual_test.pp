# Test virtual resources (@resource syntax) and realize()

notice("=== Virtual Resource Tests ===")

# Test 1: Basic virtual resource declaration
notice("Test 1: Declare virtual resources")
@user { 'virtual_alice':
  ensure => present,
  uid    => 1001,
  shell  => '/bin/bash',
}

@user { 'virtual_bob':
  ensure => present,
  uid    => 1002,
  shell  => '/bin/zsh',
}

notice("Virtual resources declared (should not appear in catalog yet)")

# Test 2: Realize a single virtual resource
notice("Test 2: Realize single virtual resource")
realize(User['virtual_alice'])

# Test 3: Realize multiple virtual resources
notice("Test 3: Realize multiple virtual resources")
@notify { 'virtual_msg1':
  message => 'This is a virtual notification 1',
}

@notify { 'virtual_msg2':
  message => 'This is a virtual notification 2',
}

realize(Notify['virtual_msg1'], Notify['virtual_msg2'])

# Test 4: Regular (non-virtual) resource should still work
notice("Test 4: Regular resource declaration")
notify { 'regular_notify':
  message => 'This is a regular notification',
}

# Test 5: Try to realize a non-existent virtual resource
notice("Test 5: Try to realize non-existent virtual resource")
realize(User['nonexistent_user'])

notice("=== Virtual Resource Tests Complete ===")
