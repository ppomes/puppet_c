# Test group resource provider
class test_group {
  # Test 1: Create a simple group
  group { "testgroup1":
    ensure => present,
  }

  # Test 2: Create a group with specific GID
  group { "testgroup2":
    ensure => present,
    gid    => 5001,
  }

  # Test 3: Group that should be absent
  group { "testgroup_absent":
    ensure => absent,
  }
}
