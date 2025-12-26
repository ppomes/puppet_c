# Test user resource provider
class test_user {
  # Test 1: Create a simple user
  user { "testuser1":
    ensure  => present,
    uid     => 5001,
    home    => "/home/testuser1",
    shell   => "/bin/bash",
    comment => "Test User 1",
  }

  # Test 2: Create a user with specific group
  user { "testuser2":
    ensure => present,
    uid    => 5002,
    gid    => "testgroup1",
    home   => "/home/testuser2",
    shell  => "/bin/sh",
  }

  # Test 3: User that should be absent
  user { "testuser_absent":
    ensure => absent,
  }
}
