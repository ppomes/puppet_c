# Simple ERB template test using resource titles

node 'web01' {
  $service_name = 'apache2'
  $port = '80'
  $admin_email = 'webmaster@example.org'
  $ssl_enabled = true
  $ssl_cert = '/etc/ssl/certs/server.crt'
  $ssl_key = '/etc/ssl/private/server.key'
  
  # This is the file resource we want to see template output for
  file { 'apache-config':
    ensure  => present,
    path    => '/etc/apache2/sites-available/default',
    content => template('tests/puppet/apache_vhost_template.erb'),
    owner   => 'root',
    mode    => '0644',
  }
  
  # Another file resource
  file { 'hosts-file':
    ensure  => present,
    path    => '/etc/hosts',
    content => "127.0.0.1 localhost ${facts['hostname']}",
    mode    => '0644',
  }
}