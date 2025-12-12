# Main site.pp manifest with multiple node definitions
$global_var = "production"

# Default node - executed when no specific node is selected
node default {
  $default_message = "This is the default node"
  include myapp
}

# Web server nodes
node 'web01' {
  $role = "webserver"
  $environment = "production"
  
  include myapp
  include database::mysql
  
  package { 'nginx':
    ensure => installed,
  }
}

node 'web02' {
  $role = "webserver"
  $environment = "staging"
  
  include myapp
  
  package { 'apache2':
    ensure => installed,
  }
}

# Database server node
node 'db01' {
  $role = "database"
  $environment = "production"
  
  include database::mysql
  
  package { 'mysql-server':
    ensure => installed,
  }
  
  service { 'mysql':
    ensure => running,
    enable => true,
  }
}

# Load balancer node  
node 'lb01' {
  $role = "loadbalancer"
  $environment = "production"
  
  package { 'haproxy':
    ensure => installed,
  }
  
  service { 'haproxy':
    ensure => running,
    enable => true,
  }
}