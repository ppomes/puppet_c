# Group resource examples
class group_examples {
  # Create a developers group
  group { 'developers':
    ensure => present,
  }

  # Create a group with specific GID
  group { 'webadmins':
    ensure => present,
    gid    => 1050,
  }

  # Create deploy group
  group { 'deploy':
    ensure => present,
    gid    => 1051,
  }
}
