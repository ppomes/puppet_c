# Test file resource provider
class test_file {
  # Test 1: Create a simple file with content
  file { '/tmp/test_file_content.txt':
    ensure  => file,
    content => "Hello from puppetc integration test\n",
    mode    => '0644',
  }

  # Test 2: Create a directory
  file { '/tmp/test_dir':
    ensure => directory,
    mode   => '0755',
  }

  # Test 3: Create file inside directory
  file { '/tmp/test_dir/nested.txt':
    ensure  => file,
    content => "Nested file content\n",
  }

  # Test 4: Create a symlink
  file { '/tmp/test_symlink':
    ensure => link,
    target => '/tmp/test_file_content.txt',
  }

  # Test 5: File from puppet:/// URL
  file { '/tmp/test_file_source.txt':
    ensure => file,
    source => 'puppet:///modules/test_file/source_file.txt',
  }

  # Test 6: File to be removed (create marker first)
  file { '/tmp/test_file_absent.txt':
    ensure => absent,
  }
}
