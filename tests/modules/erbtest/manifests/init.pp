class erbtest($servername = 'app1', $vlan = '42') {
  $clients = ['10.0.0.1', '10.0.0.2', '10.0.0.3']
  $tags = { 'role' => 'web', 'tier' => 'frontend' }

  file { '/tmp/erbtest.cfg':
    content => template('erbtest/config.cfg.erb'),
  }
}
