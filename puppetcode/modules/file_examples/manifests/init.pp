# File resource examples class
# Demonstrates various file resource types

class file_examples {
  # Simple file with inline content
  file { '/tmp/puppetc-demo/hello.txt':
    ensure  => present,
    content => "Hello from puppetc!\n",
    mode    => '0644',
    require => File['/tmp/puppetc-demo'],
  }

  # File with interpolated facts
  $hostname = $facts['hostname']
  file { '/tmp/puppetc-demo/hostname.txt':
    ensure  => present,
    content => "This host is: ${hostname}\n",
    mode    => '0644',
    require => File['/tmp/puppetc-demo'],
  }

  # Symlink example
  file { '/tmp/puppetc-link':
    ensure => link,
    target => '/tmp/puppetc-demo',
  }
}
