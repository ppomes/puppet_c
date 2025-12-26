# Test host resource provider
class test_host {
  # Test 1: Add a host entry
  host { "testhost.example.com":
    ensure => present,
    ip     => "192.168.100.100",
  }

  # Test 2: Add another host entry
  host { "another.example.com":
    ensure => present,
    ip     => "10.0.0.50",
  }
}
