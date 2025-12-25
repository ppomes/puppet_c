# Exec resource examples class
# Demonstrates exec with various conditions

class exec_examples {
  # Create marker file only if it doesn't exist
  exec { 'create_marker':
    command => '/bin/touch /tmp/puppetc-demo/first-run.marker',
    creates => '/tmp/puppetc-demo/first-run.marker',
    require => File['/tmp/puppetc-demo'],
  }

  # Log current date
  exec { 'log_date':
    command => '/bin/date >> /tmp/puppetc-demo/logs/runs.log',
    require => File['/tmp/puppetc-demo/logs'],
  }

  # Conditional exec with onlyif
  exec { 'check_and_log':
    command => '/bin/echo "Directory exists" >> /tmp/puppetc-demo/logs/checks.log',
    onlyif  => '/bin/test -d /tmp/puppetc-demo',
    require => File['/tmp/puppetc-demo/logs'],
  }

  # Conditional exec - only run if file doesn't exist
  exec { 'init_if_missing':
    command => '/bin/echo "Initialized" > /tmp/puppetc-demo/initialized.txt',
    creates => '/tmp/puppetc-demo/initialized.txt',
    require => File['/tmp/puppetc-demo'],
  }
}
