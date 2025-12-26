# User resource examples
class user_examples {
  # Create a deploy user
  user { 'deploy':
    ensure     => present,
    uid        => 1100,
    gid        => 'deploy',
    home       => '/home/deploy',
    shell      => '/bin/bash',
    comment    => 'Deployment User',
    managehome => true,
  }

  # Create an application user
  user { 'appuser':
    ensure  => present,
    uid     => 1101,
    home    => '/opt/app',
    shell   => '/bin/bash',
    comment => 'Application User',
  }

  # System user with no login shell
  user { 'myservice':
    ensure  => present,
    uid     => 999,
    home    => '/var/lib/myservice',
    shell   => '/usr/sbin/nologin',
    comment => 'My Service Account',
  }
}
