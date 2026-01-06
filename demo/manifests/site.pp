# Demo: Web + Database Infrastructure
# Managed by puppetc

node 'web' {
  notify { 'web_start': message => 'Configuring web server' }

  # Install nginx
  package { 'nginx':
    ensure => installed,
  }

  # Nginx config
  file { '/etc/nginx/sites-available/default':
    ensure  => file,
    content => "# Managed by Puppet
server {
    listen 80 default_server;
    server_name _;
    root /var/www/html;
    index index.html;

    location / {
        try_files \$uri \$uri/ =404;
    }

    location /status {
        stub_status on;
        allow 127.0.0.1;
    }
}
",
    require => Package['nginx'],
    notify  => Service['nginx'],
  }

  # Create web root
  file { '/var/www/html':
    ensure => directory,
  }

  file { '/var/www/html/index.html':
    ensure  => file,
    content => "<!DOCTYPE html>
<html>
<head><title>Puppet-C Demo</title></head>
<body>
<h1>Hello from Puppet-C!</h1>
<p>This page was deployed by puppetc-agent.</p>
<p>Server: ${facts['hostname']}</p>
</body>
</html>
",
    require => File['/var/www/html'],
  }

  # Start nginx
  service { 'nginx':
    ensure  => running,
    enable  => true,
    require => Package['nginx'],
  }

  notify { 'web_complete': message => 'Web server configured' }
}

node 'db' {
  notify { 'db_start': message => 'Configuring database server' }

  # Install MariaDB (MySQL-compatible)
  package { 'mariadb-server':
    ensure => installed,
  }

  # MariaDB config
  file { '/etc/mysql/mariadb.conf.d/99-puppet.cnf':
    ensure  => file,
    content => "# Managed by Puppet
[mysqld]
bind-address = 0.0.0.0
max_connections = 100
",
    require => Package['mariadb-server'],
    notify  => Service['mariadb'],
  }

  # Start MariaDB
  service { 'mariadb':
    ensure  => running,
    enable  => true,
    require => Package['mariadb-server'],
  }

  notify { 'db_complete': message => 'Database server configured' }
}

node default {
  notify { 'unknown_node':
    message => "Node ${facts['hostname']} has no specific configuration",
  }
}
