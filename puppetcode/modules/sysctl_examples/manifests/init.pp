# Sysctl resource examples
class sysctl_examples {
  # Enable IP forwarding
  sysctl { 'net.ipv4.ip_forward':
    value     => '1',
    permanent => true,
  }

  # Increase max open files
  sysctl { 'fs.file-max':
    value => '2097152',
  }

  # Network tuning
  sysctl { 'net.core.somaxconn':
    value => '65535',
  }

  # Disable IPv6 (if not needed)
  sysctl { 'net.ipv6.conf.all.disable_ipv6':
    value => '0',
  }

  # Swappiness - prefer RAM over swap
  sysctl { 'vm.swappiness':
    value => '10',
  }
}
