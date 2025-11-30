# Test common system administration resources

user { 'webuser':
  ensure     => present,
  home       => '/home/webuser',
  shell      => '/bin/bash',
  managehome => true,
  groups     => ['users', 'wheel'],
}

group { 'developers':
  ensure => present,
  gid    => 1000,
}

cron { 'daily_backup':
  command => '/usr/bin/backup.sh',
  user    => 'root',
  hour    => 2,
  minute  => 30,
  weekday => [1, 2, 3, 4, 5],
}

cron { 'cleanup':
  command  => '/usr/bin/cleanup.sh',
  user     => 'webuser',
  hour     => '*/4',
  minute   => 0,
  ensure   => present,
}

mount { '/data':
  ensure  => mounted,
  device  => '/dev/sdb1',
  fstype  => 'ext4',
  options => 'defaults,noatime',
  atboot  => true,
}

host { 'database.example.com':
  ensure       => present,
  ip           => '192.168.1.100',
  host_aliases => ['db', 'mysql-server'],
}

host { 'cache.example.com':
  ensure => present,  
  ip     => '192.168.1.50',
}