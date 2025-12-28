# Test: Virtual Resource Collectors
# Tests the <| |> collector syntax for realizing virtual resources

notice("=== Testing Virtual Resource Collectors ===")

# Test 1: Collect all virtual resources of a type
notice("Test 1: Collect all virtual users")
@user { 'collector_alice':
  ensure => present,
  uid    => 2001,
}
@user { 'collector_bob':
  ensure => present,
  uid    => 2002,
}
# This should realize both virtual users
User <| |>

# Test 2: Collect with equality filter
notice("Test 2: Collect with equality filter (ensure == present)")
@file { '/tmp/collector_present':
  ensure => present,
}
@file { '/tmp/collector_absent':
  ensure => absent,
}
# This should only realize the 'present' file
File <| ensure == present |>

# Test 3: Collect with inequality filter
notice("Test 3: Collect with inequality filter (ensure != absent)")
@package { 'collector_vim':
  ensure => installed,
}
@package { 'collector_emacs':
  ensure => installed,
}
@package { 'collector_nano':
  ensure => absent,
}
# This should realize vim and emacs, but not nano
Package <| ensure != absent |>

# Test 4: Collect with no matches (should be silent)
notice("Test 4: Collect non-existent type (should be silent)")
Notify <| |>  # No virtual notifies exist

# Test 5: Collect with string value filter
notice("Test 5: Collect with string value filter")
@service { 'collector_apache':
  ensure => running,
  enable => true,
}
@service { 'collector_nginx':
  ensure => stopped,
  enable => false,
}
# Should only realize apache
Service <| ensure == running |>

notice("=== Collector Tests Complete ===")
