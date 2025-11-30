# Test package management and repository resources

package { 'nginx':
  ensure => installed,
}

package { 'mysql-server':
  ensure  => '8.0.32-1',
  require => Package['mysql-common'],
}

package { 'mysql-common':
  ensure => installed,
}

package { ['git', 'curl', 'wget']:
  ensure => latest,
}

# Repository management (would typically be handled by modules)
yumrepo { 'epel':
  ensure     => present,
  descr      => 'Extra Packages for Enterprise Linux',
  baseurl    => 'http://download.fedoraproject.org/pub/epel/8/$basearch',
  gpgcheck   => '1',
  gpgkey     => 'file:///etc/pki/rpm-gpg/RPM-GPG-KEY-EPEL-8',
  enabled    => '1',
}

# Archive extraction
archive { '/tmp/app.tar.gz':
  ensure          => present,
  source          => 'https://releases.example.com/app-1.2.3.tar.gz',
  extract         => true,
  extract_path    => '/opt/app',
  creates         => '/opt/app/bin/app',
}