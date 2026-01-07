# Demo: Web + Database Infrastructure
# Managed by puppetc using official Puppet Forge modules

node 'web' {
  notify { 'web_start': message => 'Configuring web server' }

  # Install nginx (simple config - full nginx module has ERB limitations)
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

  # Log nginx config changes (refreshonly - only runs when notified)
  exec { 'log_nginx_reload':
    command     => '/bin/echo "Nginx config changed at $(date)" >> /var/log/puppet_nginx_reload.log',
    refreshonly => true,
    subscribe   => File['/etc/nginx/sites-available/default'],
  }

  notify { 'web_complete': message => 'Web server configured' }
}

node 'db' {
  notify { 'db_start': message => 'Configuring database server using mysql module' }

  # Use the official puppetlabs/mysql module
  # Note: mysql::db and some features use Deferred which isn't supported yet
  class { 'mysql::server':
    override_options => {
      'mysqld' => {
        'bind-address' => '0.0.0.0',
      },
    },
  }

  notify { 'db_complete': message => 'Database server configured with mysql module' }
}

node default {
  notify { 'unknown_node':
    message => "Node ${facts['hostname']} has no specific configuration",
  }
}
