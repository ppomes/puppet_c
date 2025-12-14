# ERB Template Output Test

node 'web01' {
  $service_name = 'apache2'
  $port = '80'
  $admin_email = 'admin@example.com'
  $ssl_enabled = false
  $ssl_cert = '/etc/ssl/certs/server.crt'
  $ssl_key = '/etc/ssl/private/server.key'
  
  # File resource using ERB template
  file { '/etc/apache2/sites-available/default':
    ensure  => present,
    content => template('tests/puppet/apache_vhost_template.erb'),
    owner   => 'root',
    group   => 'root',
    mode    => '0644',
  }
  
  # Regular file resource
  file { '/etc/hosts':
    ensure  => present,
    content => "127.0.0.1 localhost ${hostname}",
    mode    => '0644',
  }
}