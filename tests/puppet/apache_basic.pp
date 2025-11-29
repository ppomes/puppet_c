# Very basic Apache test
$port = 80

class apache {
  package { 'apache2':
    ensure => installed
  }
}

include apache