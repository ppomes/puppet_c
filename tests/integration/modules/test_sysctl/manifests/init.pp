# Test sysctl resource provider
class test_sysctl {
  # Test 1: Set a safe sysctl value (kernel hostname is safe to modify)
  sysctl { "kernel.hostname":
    value     => "puppetc-test-host",
    permanent => false,
  }

  # Test 2: Set vm.swappiness (safe to modify)
  sysctl { "vm.swappiness":
    value     => "10",
    permanent => true,
  }

  # Test 3: Set net.core.somaxconn
  sysctl { "net.core.somaxconn":
    value     => "1024",
    permanent => false,
  }
}
