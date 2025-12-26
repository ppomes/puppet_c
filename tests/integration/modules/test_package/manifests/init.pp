# Test package resource provider
class test_package {
  # Test 1: Install a small package
  package { "nano":
    ensure => present,
  }

  # Test 2: Install another package
  package { "tree":
    ensure => installed,
  }

  # Test 3: Ensure a package is absent (one that's likely not installed)
  package { "telnet":
    ensure => absent,
  }
}
