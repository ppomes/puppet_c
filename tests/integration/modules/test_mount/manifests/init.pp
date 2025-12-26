# Test mount resource provider
class test_mount {
  # Test 1: Mount a tmpfs filesystem
  mount { "/mnt/test_tmpfs":
    ensure  => mounted,
    device  => "tmpfs",
    fstype  => "tmpfs",
    options => "size=10M,mode=1777",
  }

  # Test 2: Define a mount in fstab only (don't mount)
  mount { "/mnt/test_defined":
    ensure  => defined,
    device  => "tmpfs",
    fstype  => "tmpfs",
    options => "size=5M",
  }

  # Test 3: Ensure a mount is absent
  mount { "/mnt/test_absent":
    ensure => absent,
  }
}
