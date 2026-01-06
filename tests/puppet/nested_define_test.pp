# Test: Nested defines in classes
# Tests that defines declared inside classes can be called with short names
# and that virtual resources inside nested defines are properly collected

notice("=== Testing Nested Defines in Classes ===")

# Test 1: Simple nested define with virtual resource
notice("Test 1: Simple nested define with virtual resource")
class myclass {
  define inner() {
    @notify { "virtual_${title}": message => "from inner"; }
  }

  inner { 'test1': }
}

include myclass
Notify<| |>

# Test 2: Nested define with parameters
notice("Test 2: Nested define with parameters")
class paramclass {
  define paraminner($msg) {
    @file { "/tmp/nested_${title}":
      ensure  => present,
      content => $msg,
    }
  }

  paraminner { 'file1': msg => 'hello' }
  paraminner { 'file2': msg => 'world' }
}

include paramclass
File<| |>

# Test 3: Deeply nested class with define
notice("Test 3: Deeply nested class structure")
class outer::middle {
  define deepinner($value) {
    @package { "pkg_${title}":
      ensure => $value,
    }
  }

  deepinner { 'vim': value => 'installed' }
}

include outer::middle
Package<| |>

# Test 4: Virtual defined type inside nested define
notice("Test 4: Virtual defined type inside nested define")
define mydefine($profile) {
  notice("mydefine ${title}: profile=${profile}")
}

class wrapper {
  define creator($uname, $profile) {
    @mydefine { $title:
      profile => $profile,
      tag     => $profile,
    }
  }

  creator { 'user1': uname => 'User One', profile => ['admin', 'dev'] }
  creator { 'user2': uname => 'User Two', profile => ['dev'] }
}

include wrapper
Mydefine<| tag == 'admin' |>

notice("=== Nested Define Tests Complete ===")
