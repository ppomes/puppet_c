# Test exec resource provider
class test_exec {
  # Test 1: Simple exec that creates a marker file
  exec { "create_exec_marker":
    command => "/bin/echo exec_test_passed > /tmp/test_exec_marker.txt",
    creates => "/tmp/test_exec_marker.txt",
  }

  # Test 2: Exec with cwd
  exec { "exec_cwd_test":
    command => "/bin/pwd > /tmp/test_exec_cwd.txt",
    cwd     => "/tmp",
    creates => "/tmp/test_exec_cwd.txt",
  }

  # Test 3: Another exec to verify multiple execs work
  exec { "exec_multi_test":
    command => "/bin/date > /tmp/test_exec_date.txt",
    creates => "/tmp/test_exec_date.txt",
  }
}
