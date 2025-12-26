# Cron resource examples
class cron_examples {
  # Daily log cleanup at 3:15 AM
  cron { 'cleanup-logs':
    command  => '/usr/local/bin/cleanup-logs.sh',
    user     => 'root',
    minute   => '15',
    hour     => '3',
    monthday => '*',
    month    => '*',
    weekday  => '*',
  }

  # Hourly health check
  cron { 'health-check':
    command => '/usr/local/bin/health-check.sh > /dev/null 2>&1',
    user    => 'root',
    minute  => '0',
  }

  # Weekly backup on Sundays at 2 AM
  cron { 'weekly-backup':
    command => '/usr/local/bin/backup.sh',
    user    => 'root',
    minute  => '0',
    hour    => '2',
    weekday => '0',
  }
}
